/*
 * nikel: CMDQ "op_meta" job interface (ioctls 20-24) for the cmdq v2 driver.
 * See cmdq_mdp_meta.h for the why.
 *
 * Each job's metas are translated into GCE instructions with the normal
 * cmdq_record helpers, then submitted exactly like the old
 * CMDQ_IOCTL_ASYNC_JOB_EXEC path did (scenario CMDQ_SCENARIO_USER_MDP,
 * cmdqCoreSubmitTaskAsync), so thread selection, clocks, timeouts and
 * clean-up on close behave as before. The job id handed back to user space
 * is the task pointer, checked with cmdq_core_get_task_ptr() on wait, again
 * like the old path.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2.
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>
#include <linux/errno.h>
#include <linux/jiffies.h>

#include "cmdq_def.h"
#include "cmdq_core.h"
#include "cmdq_record.h"
#include "cmdq_mdp_meta.h"

/*
 * From cmdq_record.c. Not taken from cmdq_record_private.h: its prototype
 * of cmdq_append_command() is stale (4 arguments, the function has 6).
 */
int32_t cmdq_append_command(cmdqRecHandle handle, CMDQ_CODE_ENUM code,
			    uint32_t arg_a, uint32_t arg_b,
			    uint32_t arg_a_type, uint32_t arg_b_type);
int32_t cmdq_op_finalize_command(cmdqRecHandle handle, bool loop);

#define MDPM_TAG "[cmdq-meta] "

/* metas per job: a full HD tiled frame is a few thousand, keep a wide margin */
#define MDPM_MAX_META_COUNT	0x20000

/*
 * Engine ids used in op_meta, as libdpframework (MT6797 build) numbers them
 * in DpCommandRecorder::getEngine(): MDP blocks 0x14000000 + id * 0x1000,
 * id 11 is the display mutex (base from the cmdq device tree data, the same
 * value the library reads back with CMDQ_IOCTL_QUERY_DTS), 12/13 are the
 * image (DIP) blocks.
 */
#define MDPM_ENGINE_MM_MUTEX	11
static const uint32_t mdpm_engine_base[] = {
	0x14000000, 0x14001000, 0x14002000, 0x14003000,	/*  0 -  3 */
	0x14004000, 0x14005000, 0x14006000, 0x14007000,	/*  4 -  7 */
	0x14008000, 0x14009000, 0x1400a000, 0,		/*  8 - 11 */
	0x15020000, 0x15022000,				/* 12 - 13 */
};

static uint32_t mdpm_reg(uint16_t engine, uint16_t offset)
{
	uint32_t base;

	if (engine >= ARRAY_SIZE(mdpm_engine_base))
		return 0;
	if (engine == MDPM_ENGINE_MM_MUTEX)
		base = cmdq_core_get_whole_DTS_Data()->MDPBaseAddress[CMDQ_MDP_PA_BASE_MM_MUTEX];
	else
		base = mdpm_engine_base[engine];
	if (!base)
		return 0;
	return base + offset;
}

/* ------------------------------------------------------------------ */
/* readback slots (ioctls 22-24): id = slot << 16 | index              */

#define MDPM_RB_SLOTS		256
#define MDPM_RB_MAX_COUNT	0x1000
#define MDPM_RB_ID_SHIFT	16
#define MDPM_RB_INDEX_MASK	0xffff

struct mdpm_rb_slot {
	dma_addr_t pa;
	uint32_t count;
	struct file *owner;
};

static struct mdpm_rb_slot mdpm_rb[MDPM_RB_SLOTS];
static DEFINE_MUTEX(mdpm_rb_lock);

/* 0 on a bad id */
static dma_addr_t mdpm_rb_pa(uint32_t id)
{
	uint32_t slot = id >> MDPM_RB_ID_SHIFT;
	uint32_t index = id & MDPM_RB_INDEX_MASK;
	dma_addr_t pa = 0;

	if (slot >= MDPM_RB_SLOTS)
		return 0;
	mutex_lock(&mdpm_rb_lock);
	if (mdpm_rb[slot].pa && index < mdpm_rb[slot].count)
		pa = mdpm_rb[slot].pa + index * sizeof(uint32_t);
	mutex_unlock(&mdpm_rb_lock);
	return pa;
}

static long mdpm_rb_alloc(struct file *pFile, unsigned long param)
{
	struct mdp_readback req;
	dma_addr_t pa = 0;
	int i, err;

	if (copy_from_user(&req, (void __user *)param, sizeof(req)))
		return -EFAULT;
	if (!req.count || req.count > MDPM_RB_MAX_COUNT)
		return -EINVAL;

	err = cmdqCoreAllocWriteAddress(req.count, &pa);
	if (err || !pa) {
		CMDQ_ERR(MDPM_TAG "alloc %u slots failed: %d\n", req.count, err);
		return err ? err : -ENOMEM;
	}

	mutex_lock(&mdpm_rb_lock);
	for (i = 0; i < MDPM_RB_SLOTS; i++)
		if (!mdpm_rb[i].pa)
			break;
	if (i == MDPM_RB_SLOTS) {
		mutex_unlock(&mdpm_rb_lock);
		cmdqCoreFreeWriteAddress(pa);
		CMDQ_ERR(MDPM_TAG "no free readback slot\n");
		return -ENOMEM;
	}
	mdpm_rb[i].pa = pa;
	mdpm_rb[i].count = req.count;
	mdpm_rb[i].owner = pFile;
	mutex_unlock(&mdpm_rb_lock);

	req.start_id = (uint32_t)i << MDPM_RB_ID_SHIFT;
	if (copy_to_user((void __user *)param, &req, sizeof(req))) {
		mutex_lock(&mdpm_rb_lock);
		mdpm_rb[i].pa = 0;
		mdpm_rb[i].count = 0;
		mdpm_rb[i].owner = NULL;
		mutex_unlock(&mdpm_rb_lock);
		cmdqCoreFreeWriteAddress(pa);
		return -EFAULT;
	}
	return 0;
}

static long mdpm_rb_free(struct file *pFile, unsigned long param)
{
	struct mdp_readback req;
	uint32_t slot;
	dma_addr_t pa = 0;

	if (copy_from_user(&req, (void __user *)param, sizeof(req)))
		return -EFAULT;
	slot = req.start_id >> MDPM_RB_ID_SHIFT;
	if (slot >= MDPM_RB_SLOTS)
		return -EINVAL;

	mutex_lock(&mdpm_rb_lock);
	if (mdpm_rb[slot].pa && mdpm_rb[slot].owner == pFile) {
		pa = mdpm_rb[slot].pa;
		mdpm_rb[slot].pa = 0;
		mdpm_rb[slot].count = 0;
		mdpm_rb[slot].owner = NULL;
	}
	mutex_unlock(&mdpm_rb_lock);

	if (!pa)
		return -EINVAL;
	cmdqCoreFreeWriteAddress(pa);
	return 0;
}

/* copy readback slot values to user space; count 0 is fine */
static long mdpm_rb_read(const struct mdp_read_readback *req)
{
	uint32_t *ids = NULL, *values = NULL;
	uint32_t i;
	long ret = 0;

	if (!req->count)
		return 0;
	if (req->count > CMDQ_MAX_DUMP_REG_COUNT || !req->ids || !req->ret_values)
		return -EINVAL;

	ids = kcalloc(req->count, sizeof(uint32_t), GFP_KERNEL);
	values = kcalloc(req->count, sizeof(uint32_t), GFP_KERNEL);
	if (!ids || !values) {
		ret = -ENOMEM;
		goto out;
	}
	if (copy_from_user(ids, (void __user *)(unsigned long)req->ids,
			   req->count * sizeof(uint32_t))) {
		ret = -EFAULT;
		goto out;
	}
	for (i = 0; i < req->count; i++) {
		dma_addr_t pa = mdpm_rb_pa(ids[i]);

		if (!pa) {
			ret = -EINVAL;
			goto out;
		}
		values[i] = cmdqCoreReadWriteAddress(pa);
	}
	if (copy_to_user((void __user *)(unsigned long)req->ret_values, values,
			 req->count * sizeof(uint32_t)))
		ret = -EFAULT;
out:
	kfree(ids);
	kfree(values);
	return ret;
}

void cmdq_mdp_meta_release(struct file *pFile)
{
	int i;

	mutex_lock(&mdpm_rb_lock);
	for (i = 0; i < MDPM_RB_SLOTS; i++) {
		if (mdpm_rb[i].pa && mdpm_rb[i].owner == pFile) {
			cmdqCoreFreeWriteAddress(mdpm_rb[i].pa);
			mdpm_rb[i].pa = 0;
			mdpm_rb[i].count = 0;
			mdpm_rb[i].owner = NULL;
		}
	}
	mutex_unlock(&mdpm_rb_lock);
}

/* ------------------------------------------------------------------ */
/* metas -> GCE instructions                                           */

static int32_t mdpm_wfe_raw(cmdqRecHandle h, CMDQ_EVENT_ENUM event, uint32_t arg_b)
{
	int32_t hw;

	if (event < 0 || event >= CMDQ_SYNC_TOKEN_MAX)
		return -EINVAL;
	hw = cmdq_core_get_event_value(event);
	if (hw < 0)
		return -EINVAL;
	return cmdq_append_command(h, CMDQ_CODE_RAW, (CMDQ_CODE_WFE << 24) | hw,
				   arg_b, 0, 0);
}

static int32_t mdpm_translate(cmdqRecHandle h, const struct op_meta *m)
{
	uint32_t reg, from;
	dma_addr_t pa;

	switch (m->op) {
	case CMDQ_MOP_WRITE:
		reg = mdpm_reg(m->engine, m->offset);
		if (!reg)
			return -EINVAL;
		return cmdq_op_write_reg(h, reg, m->value, m->mask);

	case CMDQ_MOP_READ:
		reg = mdpm_reg(m->engine, m->offset);
		pa = mdpm_rb_pa(m->readback_id);
		if (!reg || !pa)
			return -EINVAL;
		return cmdq_op_read_reg_to_mem(h, pa, 0, reg);

	case CMDQ_MOP_POLL:
		reg = mdpm_reg(m->engine, m->offset);
		if (!reg)
			return -EINVAL;
		return cmdq_op_poll(h, reg, m->value, m->mask);

	case CMDQ_MOP_WAIT:
		return cmdq_op_wait(h, (CMDQ_EVENT_ENUM)m->event);

	case CMDQ_MOP_WAIT_NO_CLEAR:
		return cmdq_op_wait_no_clear(h, (CMDQ_EVENT_ENUM)m->event);

	case CMDQ_MOP_CLEAR:
		return cmdq_op_clear_event(h, (CMDQ_EVENT_ENUM)m->event);

	case CMDQ_MOP_SET:
		return cmdq_op_set_event(h, (CMDQ_EVENT_ENUM)m->event);

	case CMDQ_MOP_ACQUIRE:
		/* wait until the token is 0, then set it to 1 */
		return mdpm_wfe_raw(h, (CMDQ_EVENT_ENUM)m->event,
				    (1 << 31) | (1 << 16) | (1 << 15));

	case CMDQ_MOP_WRITE_FROM_REG:
		reg = mdpm_reg(m->engine, m->offset);
		from = mdpm_reg(m->from_engine, m->from_offset);
		if (!reg || !from)
			return -EINVAL;
		/* the GPR helpers take the GPR lock themselves for these */
		if (cmdq_core_subsys_from_phys_addr(reg) == CMDQ_SPECIAL_SUBSYS_ADDR ||
		    cmdq_core_subsys_from_phys_addr(from) == CMDQ_SPECIAL_SUBSYS_ADDR)
			return -EINVAL;
		if (cmdq_op_wait(h, CMDQ_SYNC_TOKEN_GPR_SET_4) ||
		    cmdq_op_read_to_data_register(h, from, CMDQ_DATA_REG_DEBUG) ||
		    cmdq_op_write_from_data_register(h, CMDQ_DATA_REG_DEBUG, reg) ||
		    cmdq_op_set_event(h, CMDQ_SYNC_TOKEN_GPR_SET_4))
			return -EFAULT;
		return 0;

	case CMDQ_MOP_NOP:
		return 0;

	case CMDQ_MOP_WRITE_FD:		/* not produced by this library */
	case CMDQ_MOP_WRITE_SEC:	/* secure path is not supported */
	default:
		return -EINVAL;
	}
}

static long mdpm_exec(struct file *pFile, unsigned long param)
{
	struct mdp_submit job;
	struct op_meta *metas = NULL;
	struct hw_meta *hw = NULL;
	uint32_t *reg_addrs = NULL;
	cmdqRecHandle h = NULL;
	cmdqCommandStruct desc;
	struct TaskStruct *pTask = NULL;
	mm_segment_t old_fs;
	uint32_t i;
	long ret;

	if (copy_from_user(&job, (void __user *)param, sizeof(job)))
		return -EFAULT;

	if (!job.meta_count || job.meta_count > MDPM_MAX_META_COUNT ||
	    job.read_count_v1 > CMDQ_MAX_DUMP_REG_COUNT) {
		CMDQ_ERR(MDPM_TAG "bad job: metas %u, reads %u\n",
			 job.meta_count, job.read_count_v1);
		return -EINVAL;
	}
	if (job.secData.is_secure) {
		CMDQ_ERR(MDPM_TAG "secure MDP jobs are not supported\n");
		return -EINVAL;
	}

	metas = vmalloc(job.meta_count * sizeof(*metas));
	if (!metas)
		return -ENOMEM;
	if (copy_from_user(metas, (void __user *)(unsigned long)job.metas,
			   job.meta_count * sizeof(*metas))) {
		ret = -EFAULT;
		goto out;
	}

	/* registers the library wants back after the job (read_v1) */
	if (job.read_count_v1) {
		hw = kcalloc(job.read_count_v1, sizeof(*hw), GFP_KERNEL);
		reg_addrs = kcalloc(job.read_count_v1, sizeof(uint32_t), GFP_KERNEL);
		if (!hw || !reg_addrs) {
			ret = -ENOMEM;
			goto out;
		}
		if (copy_from_user(hw, (void __user *)(unsigned long)job.hw_metas_read_v1,
				   job.read_count_v1 * sizeof(*hw))) {
			ret = -EFAULT;
			goto out;
		}
		for (i = 0; i < job.read_count_v1; i++) {
			reg_addrs[i] = mdpm_reg(hw[i].engine, hw[i].offset);
			if (!reg_addrs[i]) {
				CMDQ_ERR(MDPM_TAG "bad read_v1[%u]: engine %u offset 0x%x\n",
					 i, hw[i].engine, hw[i].offset);
				ret = -EINVAL;
				goto out;
			}
		}
	}

	ret = cmdq_task_create(CMDQ_SCENARIO_USER_MDP, &h);
	if (ret)
		goto out;
	cmdq_task_set_engine(h, job.engine_flag);
	h->priority = job.priority;

	for (i = 0; i < job.meta_count; i++) {
		ret = mdpm_translate(h, &metas[i]);
		if (ret) {
			CMDQ_ERR(MDPM_TAG "meta[%u] op %u engine %u offset 0x%x value 0x%x mask 0x%x: %ld\n",
				 i, metas[i].op, metas[i].engine, metas[i].offset,
				 metas[i].value, metas[i].mask, ret);
			ret = -EINVAL;
			goto out;
		}
	}

	ret = cmdq_op_finalize_command(h, false);
	if (ret)
		goto out;

	memset(&desc, 0, sizeof(desc));
	desc.scenario = CMDQ_SCENARIO_USER_MDP;
	desc.priority = job.priority;
	desc.engineFlag = job.engine_flag;
	desc.pVABase = (cmdqU32Ptr_t)(unsigned long)h->pBuffer;
	desc.blockSize = h->blockSize;
	desc.regRequest.count = job.read_count_v1;
	desc.regRequest.regAddresses = (cmdqU32Ptr_t)(unsigned long)reg_addrs;
	desc.privateData = (cmdqU32Ptr_t)(unsigned long)pFile->private_data;

	/*
	 * A USER_MDP task has its command buffer copied with copy_from_user();
	 * ours was built in kernel memory, so open the address limit for the
	 * submit. Everything else in the request is already kernel memory.
	 */
	old_fs = get_fs();
	set_fs(KERNEL_DS);
	ret = cmdqCoreSubmitTaskAsync(&desc, NULL, 0, &pTask);
	set_fs(old_fs);

	if (ret < 0 || !pTask) {
		CMDQ_ERR(MDPM_TAG "submit failed: %ld\n", ret);
		ret = ret < 0 ? ret : -EFAULT;
		goto out;
	}
	pTask->regCountUserSpace = job.read_count_v1;
	pTask->regUserToken = 0;

	job.job_id = (unsigned long)pTask;
	if (copy_to_user((void __user *)param, &job, sizeof(job))) {
		/* the task runs anyway; let the core reclaim it */
		cmdqCoreAutoReleaseTask(pTask);
		ret = -EFAULT;
		goto out;
	}
	ret = 0;
out:
	if (h)
		cmdq_task_destroy(h);
	kfree(reg_addrs);
	kfree(hw);
	vfree(metas);
	return ret;
}

static long mdpm_wait(unsigned long param)
{
	struct mdp_wait res;
	struct TaskStruct *pTask;
	cmdqRegValueStruct regs;
	uint32_t user_count, n;
	long ret, ret2;

	if (copy_from_user(&res, (void __user *)param, sizeof(res)))
		return -EFAULT;

	pTask = cmdq_core_get_task_ptr((void *)(unsigned long)res.job_id);
	if (!pTask) {
		CMDQ_ERR(MDPM_TAG "invalid job 0x%llx\n", res.job_id);
		return -EFAULT;
	}
	if (pTask->regCount > CMDQ_MAX_DUMP_REG_COUNT)
		return -EINVAL;

	user_count = pTask->regCountUserSpace;
	regs.count = pTask->regCount;
	regs.regValues = (cmdqU32Ptr_t)(unsigned long)
		kcalloc(pTask->regCount ? pTask->regCount : 1, sizeof(uint32_t), GFP_KERNEL);
	if (!regs.regValues)
		return -ENOMEM;

	ret = cmdqCoreWaitResultAndReleaseTask(pTask, &regs,
					       msecs_to_jiffies(CMDQ_DEFAULT_TIMEOUT_MS));
	pTask = NULL;	/* released */
	if (ret < 0) {
		CMDQ_ERR(MDPM_TAG "job 0x%llx failed: %ld\n", res.job_id, ret);
		goto out;
	}

	/* register values requested at submit (read_v1) */
	n = min(user_count, res.read_v1_result.count);
	if (n && res.read_v1_result.ret_values &&
	    copy_to_user((void __user *)(unsigned long)res.read_v1_result.ret_values,
			 CMDQ_U32_PTR(regs.regValues), n * sizeof(uint32_t))) {
		ret = -EFAULT;
		goto out;
	}

	/* readback slots written by READ metas */
	ret2 = mdpm_rb_read(&res.read_result);
	if (ret2)
		ret = ret2;
out:
	kfree(CMDQ_U32_PTR(regs.regValues));
	return ret;
}

bool cmdq_mdp_meta_is_ioctl(unsigned int code)
{
	switch (code) {
	case CMDQ_IOCTL_ASYNC_EXEC:
	case CMDQ_IOCTL_ASYNC_WAIT:
	case CMDQ_IOCTL_ALLOC_READBACK_SLOTS:
	case CMDQ_IOCTL_FREE_READBACK_SLOTS:
	case CMDQ_IOCTL_READ_READBACK_SLOTS:
		return true;
	default:
		return false;
	}
}

long cmdq_mdp_meta_ioctl(struct file *pFile, unsigned int code, unsigned long param)
{
	struct mdp_read_readback req;

	switch (code) {
	case CMDQ_IOCTL_ASYNC_EXEC:
		return mdpm_exec(pFile, param);
	case CMDQ_IOCTL_ASYNC_WAIT:
		return mdpm_wait(param);
	case CMDQ_IOCTL_ALLOC_READBACK_SLOTS:
		return mdpm_rb_alloc(pFile, param);
	case CMDQ_IOCTL_FREE_READBACK_SLOTS:
		return mdpm_rb_free(pFile, param);
	case CMDQ_IOCTL_READ_READBACK_SLOTS:
		if (copy_from_user(&req, (void __user *)param, sizeof(req)))
			return -EFAULT;
		return mdpm_rb_read(&req);
	default:
		return -ENOIOCTLCMD;
	}
}
