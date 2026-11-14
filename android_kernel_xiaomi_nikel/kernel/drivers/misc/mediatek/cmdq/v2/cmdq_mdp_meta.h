/*
 * nikel: CMDQ "op_meta" job interface (ioctls 20-24) for the cmdq v2 driver.
 *
 * Newer MediaTek libdpframework builds (the one in SHIFT's MT6797 vendor,
 * used by our Oreo and Pie builds) no longer hand the kernel ready-made GCE
 * instructions. They send a list of 16-byte "op_meta" records (write reg,
 * poll, wait event, ...) and let the kernel turn them into instructions.
 * The 3.18 cmdq v2 driver only knows the old ioctls 1-12, so every MDP job
 * (video colour conversion, rotation, HWC blits) was rejected.
 *
 * The structures below follow MediaTek's mdp_def_ex.h (kernel 4.14) and were
 * checked against the SHIFT library: mdp_submit is 0x78 bytes, mdp_wait 0x30.
 * Layout is identical for 32- and 64-bit callers.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2.
 */

#ifndef __CMDQ_MDP_META_H__
#define __CMDQ_MDP_META_H__

#include <linux/types.h>
#include <linux/ioctl.h>
#include <linux/fs.h>

enum CMDQ_META_OP {
	CMDQ_MOP_WRITE = 0,
	CMDQ_MOP_READ,
	CMDQ_MOP_POLL,
	CMDQ_MOP_WAIT,
	CMDQ_MOP_WAIT_NO_CLEAR,
	CMDQ_MOP_CLEAR,
	CMDQ_MOP_SET,
	CMDQ_MOP_ACQUIRE,
	CMDQ_MOP_WRITE_FD,
	CMDQ_MOP_WRITE_FROM_REG,
	CMDQ_MOP_WRITE_SEC,
	CMDQ_MOP_NOP,
};

struct op_meta {
	uint8_t op;
	uint16_t engine;		/* library engine id, see cmdq_mdp_meta.c */
	union {
		uint16_t offset;	/* register offset inside the engine */
		uint16_t event;		/* CMDQ_EVENT_ENUM (software id) */
	};
	union {
		uint32_t value;
		uint32_t readback_id;
		uint32_t fd;
		uint32_t sec_handle;
		struct {
			uint16_t from_offset;
			uint16_t from_engine;
		};
	};
	union {
		uint32_t mask;
		uint32_t fd_offset;
		uint32_t sec_index;
	};
};

struct hw_meta {
	uint16_t offset;
	uint16_t engine;
};

/* same size and layout as cmdqSecDataStruct without CONFIG_MTK_CMDQ_TAB */
struct mdp_meta_sec_data {
	bool is_secure;
	uint32_t addrMetadataCount;
	unsigned long long addrMetadatas;
	uint32_t addrMetadataMaxCount;
	uint64_t enginesNeedDAPC;
	uint64_t enginesNeedPortSecurity;
	int32_t waitCookie;
	bool resetExecCnt;
};

struct mdp_submit {
	unsigned long long metas;	/* [IN] struct op_meta[] */
	uint32_t meta_count;
	uint32_t priority;
	uint64_t engine_flag;
	uint32_t prop_size;		/* pmqos data, ignored here */
	unsigned long long prop_addr;
	uint64_t readback_ext;
	uint32_t read_count_v1;		/* [IN] registers to read at job end */
	unsigned long long hw_metas_read_v1;	/* [IN] struct hw_meta[] */
	uint64_t job_id;		/* [OUT] */
	struct mdp_meta_sec_data secData;
};

struct mdp_read_v1 {
	uint32_t count;
	unsigned long long ret_values;	/* [OUT] u32[] */
};

struct mdp_read_readback {
	uint32_t count;
	unsigned long long ids;		/* [IN] u32 readback ids */
	unsigned long long ret_values;	/* [OUT] u32[] */
};

struct mdp_wait {
	uint64_t job_id;
	struct mdp_read_v1 read_v1_result;
	struct mdp_read_readback read_result;
};

struct mdp_readback {
	uint32_t count;			/* number of u32 */
	uint32_t start_id;		/* [OUT] on alloc, [IN] on free */
};

#define CMDQ_IOCTL_ASYNC_EXEC \
	_IOW('x', 20, struct mdp_submit)
#define CMDQ_IOCTL_ASYNC_WAIT \
	_IOR('x', 21, struct mdp_wait)
#define CMDQ_IOCTL_ALLOC_READBACK_SLOTS \
	_IOW('x', 22, struct mdp_readback)
#define CMDQ_IOCTL_FREE_READBACK_SLOTS \
	_IOW('x', 23, struct mdp_readback)
#define CMDQ_IOCTL_READ_READBACK_SLOTS \
	_IOW('x', 24, struct mdp_read_readback)

/* called from cmdq_driver.c */
bool cmdq_mdp_meta_is_ioctl(unsigned int code);
long cmdq_mdp_meta_ioctl(struct file *pFile, unsigned int code,
			 unsigned long param);
void cmdq_mdp_meta_release(struct file *pFile);

#endif /* __CMDQ_MDP_META_H__ */
