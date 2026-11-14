/*
 * nikel: fingerprint HAL wrapper (fingerprint.mt6797).
 *
 * The sensor HALs nikel shipped with are from MIUI on Android 6.0 and speak
 * fingerprint HAL 2.0. Oreo's android.hardware.biometrics.fingerprint@2.1
 * service only accepts 2.1 devices. The two differ in a few places, which
 * this wrapper bridges:
 *   - device version: reported as 2.1;
 *   - enumerate(): 2.0 took a buffer (and MIUI's FPC HAL leaves it empty),
 *     2.1 reports templates by callback. The wrapper keeps its own list of
 *     enrolled finger ids per group, next to the template database, and
 *     reports that;
 *   - remove(): 2.1 expects "remaining templates" in the callback and
 *     fid 0 to mean "all"; done here with the same list;
 *   - the 2.0 HAL leaves the removed.remaining_templates field undefined.
 *
 * Enrolment authorization (FPC only): MIUI's HAL hands the auth token from
 * gatekeeper to the FPC trusted app, which checks its HMAC with the key of
 * the Trustonic keymaster. Our build still uses Android's software
 * gatekeeper (all-zero key), so that check always fails and enrolment stops
 * with HW_UNAVAILABLE. The HAL has a built-in way around it, used by MIUI's
 * own tools: with persist.sys.enroll.status=1 and its internal "token
 * required" flag clear, it skips the check. The wrapper clears that flag
 * after pre_enroll(), for the exact MIUI build it knows (checked by size).
 * Settings still asks for the PIN/pattern before enrolling. This goes away
 * once the hardware gatekeeper/keymaster are in use.
 *
 * Which vendor HAL is loaded is picked from ro.boot.fpsensor, which nikel's
 * bootloader sets after probing the sensor ("fpc", "gdx" for Goodix,
 * "lc" for the Leadcore/fpsensor part), the same way MIUI's fingerprintd did.
 * Only FPC is wired up so far; the others are listed for later.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the Apache License, Version 2.0.
 */

#define LOG_TAG "nikel-fingerprint"

#include <dlfcn.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <stdarg.h>
#include <time.h>

#include <cutils/properties.h>
#include <hardware/fingerprint.h>
#include <hardware/hardware.h>
#include <log/log.h>

#ifdef __LP64__
#define VENDOR_DIR "/vendor/lib64/hw/"
#else
#define VENDOR_DIR "/vendor/lib/hw/"
#endif

#define MAX_FIDS 16
#define TEE_WAIT_MS 30000
/* boot-time log that survives logcat's pruning of busy system-uid logs */
#define NIKEL_FP_LOG "/data/fpc/nikel_wrapper.log"
#define OPEN_RETRY_STEP_MS 500

struct backend {
	const char *fpsensor;	/* ro.boot.fpsensor value */
	const char *lib;	/* vendor HAL, renamed so hw_get_module never picks it */
	/* enrol-token workaround, see top: lib size and flag offset from HMI */
	off_t auth_flag_lib_size;
	size_t auth_flag_off;
};

static const struct backend backends[] = {
	/* MIUI V10.2.2.0.MBFCNXM lib64/hw/fingerprint.mt6797.so: HMI at
	 * 0x19008, the flag is the first byte of .bss at 0x19140 */
	{ "fpc", VENDOR_DIR "fpc_fingerprint.miui.so", 38848, 0x138 },
	/* { "gdx", VENDOR_DIR "gf_fingerprint.miui.so" },   Goodix: later */
	/* { "lc",  VENDOR_DIR "fpsensor_fingerprint.miui.so" }, later */
};


static void nlog(int prio, const char *fmt, ...)
{
	char buf[256];
	struct timespec ts;
	va_list ap;
	FILE *f;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	__android_log_print(prio, LOG_TAG, "%s", buf);

	f = fopen(NIKEL_FP_LOG, "a");
	if (f) {
		clock_gettime(CLOCK_BOOTTIME, &ts);
		fprintf(f, "[%5ld.%03ld] %s\n", (long)ts.tv_sec, ts.tv_nsec / 1000000, buf);
		fclose(f);
	}
}
#define LOGE(...) nlog(ANDROID_LOG_ERROR, __VA_ARGS__)
#define LOGW(...) nlog(ANDROID_LOG_WARN, __VA_ARGS__)
#define LOGI(...) nlog(ANDROID_LOG_INFO, __VA_ARGS__)

/*
 * Wait until the Trustonic daemon answers (mcOpenDevice succeeds) before
 * the vendor HAL is opened. A vendor HAL that fails its first open can stay
 * broken for the life of the process, so it is better not to let it fail.
 */
typedef uint32_t (*mc_dev_fn)(uint32_t device_id);

static void wait_for_tee(void)
{
	void *mc = dlopen("libMcClient.so", RTLD_NOW);
	mc_dev_fn mc_open, mc_close;
	uint32_t r = 0;
	int waited;

	if (!mc) {
		LOGE("dlopen libMcClient.so: %s", dlerror());
		return;
	}
	mc_open = (mc_dev_fn)dlsym(mc, "mcOpenDevice");
	mc_close = (mc_dev_fn)dlsym(mc, "mcCloseDevice");
	if (!mc_open || !mc_close) {
		LOGE("libMcClient: no mcOpenDevice/mcCloseDevice");
		dlclose(mc);
		return;
	}
	for (waited = 0; waited <= TEE_WAIT_MS; waited += OPEN_RETRY_STEP_MS) {
		r = mc_open(0);		/* MC_DEVICE_ID_DEFAULT */
		if (r == 0) {
			mc_close(0);
			LOGI("TEE ready after %d ms", waited);
			dlclose(mc);
			return;
		}
		usleep(OPEN_RETRY_STEP_MS * 1000);
	}
	LOGE("TEE still not answering after %d ms (mcOpenDevice 0x%x)", TEE_WAIT_MS, r);
	dlclose(mc);
}

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static fingerprint_device_t *vendor;
static fingerprint_notify_t client_notify;
static volatile uint8_t *auth_flag;	/* NULL: workaround not used */

/* enrolled fids of the active group, mirrored to <store_path>/nikel_fids */
static uint32_t cur_gid;
static char fid_file[PATH_MAX];
static uint32_t fids[MAX_FIDS];
static int fid_count;
/* remove-all in progress: how many REMOVED callbacks are still to come */
static int remove_all_pending = -1;

static void fids_save(void)
{
	FILE *f;
	int i;

	if (!fid_file[0])
		return;
	f = fopen(fid_file, "w");
	if (!f) {
		LOGE("cannot write %s: %s", fid_file, strerror(errno));
		return;
	}
	for (i = 0; i < fid_count; i++)
		fprintf(f, "%u\n", fids[i]);
	fclose(f);
}

static void fids_load(void)
{
	FILE *f;
	unsigned int v;

	fid_count = 0;
	if (!fid_file[0])
		return;
	f = fopen(fid_file, "r");
	if (!f)
		return;
	while (fid_count < MAX_FIDS && fscanf(f, "%u", &v) == 1)
		if (v)
			fids[fid_count++] = v;
	fclose(f);
}

static void fids_add(uint32_t fid)
{
	int i;

	if (!fid)
		return;
	for (i = 0; i < fid_count; i++)
		if (fids[i] == fid)
			return;
	if (fid_count < MAX_FIDS)
		fids[fid_count++] = fid;
	fids_save();
}

static void fids_del(uint32_t fid)
{
	int i;

	for (i = 0; i < fid_count; i++) {
		if (fids[i] == fid) {
			fids[i] = fids[--fid_count];
			fids_save();
			return;
		}
	}
}

/* called by the vendor HAL, possibly from its own threads */
static void wrap_notify(const fingerprint_msg_t *in)
{
	fingerprint_msg_t msg = *in;
	fingerprint_notify_t cb;

	pthread_mutex_lock(&lock);
	switch (msg.type) {
	case FINGERPRINT_TEMPLATE_ENROLLING:
		if (msg.data.enroll.samples_remaining == 0)
			fids_add(msg.data.enroll.finger.fid);
		break;
	case FINGERPRINT_TEMPLATE_REMOVED:
		fids_del(msg.data.removed.finger.fid);
		if (remove_all_pending > 0) {
			remove_all_pending--;
			msg.data.removed.remaining_templates = remove_all_pending;
			if (remove_all_pending == 0)
				remove_all_pending = -1;
		} else {
			msg.data.removed.remaining_templates = 0;
		}
		break;
	default:
		break;
	}
	cb = client_notify;
	pthread_mutex_unlock(&lock);

	if (cb)
		cb(&msg);
}

static int wrap_set_notify(struct fingerprint_device *dev, fingerprint_notify_t notify)
{
	(void)dev;
	pthread_mutex_lock(&lock);
	client_notify = notify;
	pthread_mutex_unlock(&lock);
	{
		int ret = vendor->set_notify(vendor, wrap_notify);

		if (ret)
			LOGE("set_notify failed: %d", ret);
		return ret;
	}
}

static uint64_t wrap_pre_enroll(struct fingerprint_device *dev)
{
	uint64_t challenge;

	(void)dev;
	challenge = vendor->pre_enroll(vendor);
	if (auth_flag)
		*auth_flag = 0;
	return challenge;
}

static int wrap_enroll(struct fingerprint_device *dev, const hw_auth_token_t *token,
		       uint32_t gid, uint32_t timeout_sec)
{
	(void)dev;
	return vendor->enroll(vendor, token, gid, timeout_sec);
}

static int wrap_post_enroll(struct fingerprint_device *dev)
{
	(void)dev;
	return vendor->post_enroll(vendor);
}

static uint64_t wrap_get_authenticator_id(struct fingerprint_device *dev)
{
	(void)dev;
	return vendor->get_authenticator_id(vendor);
}

static int wrap_cancel(struct fingerprint_device *dev)
{
	(void)dev;
	return vendor->cancel(vendor);
}

static int wrap_enumerate(struct fingerprint_device *dev)
{
	fingerprint_msg_t msg;
	fingerprint_notify_t cb;
	uint32_t list[MAX_FIDS];
	int n, i;

	(void)dev;
	pthread_mutex_lock(&lock);
	n = fid_count;
	memcpy(list, fids, sizeof(list));
	cb = client_notify;
	pthread_mutex_unlock(&lock);

	if (!cb)
		return -EINVAL;

	memset(&msg, 0, sizeof(msg));
	msg.type = FINGERPRINT_TEMPLATE_ENUMERATING;
	if (n == 0) {
		/* "no templates": fid 0, remaining 0 */
		msg.data.enumerated.finger.gid = cur_gid;
		cb(&msg);
		return 0;
	}
	for (i = 0; i < n; i++) {
		msg.data.enumerated.finger.gid = cur_gid;
		msg.data.enumerated.finger.fid = list[i];
		msg.data.enumerated.remaining_templates = n - 1 - i;
		cb(&msg);
	}
	return 0;
}

static int wrap_remove(struct fingerprint_device *dev, uint32_t gid, uint32_t fid)
{
	uint32_t list[MAX_FIDS];
	int n, i, ret = 0;

	(void)dev;
	if (fid != 0)
		return vendor->remove(vendor, gid, fid);

	/* remove all: one vendor call per known finger */
	pthread_mutex_lock(&lock);
	n = fid_count;
	memcpy(list, fids, sizeof(list));
	remove_all_pending = n > 0 ? n : -1;
	pthread_mutex_unlock(&lock);

	if (n == 0) {
		fingerprint_msg_t msg;
		fingerprint_notify_t cb;

		pthread_mutex_lock(&lock);
		cb = client_notify;
		pthread_mutex_unlock(&lock);
		memset(&msg, 0, sizeof(msg));
		msg.type = FINGERPRINT_TEMPLATE_REMOVED;
		msg.data.removed.finger.gid = gid;
		if (cb)
			cb(&msg);
		return 0;
	}
	for (i = 0; i < n; i++) {
		ret = vendor->remove(vendor, gid, list[i]);
		if (ret)
			LOGE("remove fid %u: %d", list[i], ret);
	}
	return ret;
}

static int wrap_set_active_group(struct fingerprint_device *dev, uint32_t gid,
				 const char *store_path)
{
	int ret;

	(void)dev;
	ret = vendor->set_active_group(vendor, gid, store_path);

	pthread_mutex_lock(&lock);
	cur_gid = gid;
	if (store_path && snprintf(fid_file, sizeof(fid_file), "%s/nikel_fids",
				   store_path) < (int)sizeof(fid_file))
		fids_load();
	else
		fid_file[0] = 0;
	LOGI("group %u: %d known finger(s), %s", gid, fid_count,
	      fid_file[0] ? fid_file : "(no store path)");
	pthread_mutex_unlock(&lock);
	return ret;
}

static int wrap_authenticate(struct fingerprint_device *dev, uint64_t operation_id,
			     uint32_t gid)
{
	(void)dev;
	return vendor->authenticate(vendor, operation_id, gid);
}

static int wrap_close(hw_device_t *dev)
{
	int ret = vendor->common.close(&vendor->common);

	vendor = NULL;
	free(dev);
	return ret;
}

static int wrap_open(const hw_module_t *module, const char *id, hw_device_t **device)
{
	char sensor[PROPERTY_VALUE_MAX];
	const struct backend *be = NULL;
	struct hw_module_t *vmod;
	fingerprint_device_t *dev;
	hw_device_t *vdev = NULL;
	void *handle;
	size_t i;
	int ret;

	if (!device)
		return -EINVAL;
	*device = NULL;
	if (vendor) {
		LOGE("already open");
		return -EBUSY;
	}

	property_get("ro.boot.fpsensor", sensor, "fpc");
	for (i = 0; i < sizeof(backends) / sizeof(backends[0]); i++)
		if (!strcmp(sensor, backends[i].fpsensor))
			be = &backends[i];
	if (!be) {
		LOGE("no HAL for fingerprint sensor '%s'", sensor);
		return -ENODEV;
	}

	LOGI("open: sensor '%s', HAL %s", sensor, be->lib);
	wait_for_tee();

	handle = dlopen(be->lib, RTLD_NOW);
	if (!handle) {
		LOGE("dlopen %s: %s", be->lib, dlerror());
		return -ENOENT;
	}
	vmod = dlsym(handle, HAL_MODULE_INFO_SYM_AS_STR);
	if (!vmod || !vmod->methods || !vmod->methods->open) {
		LOGE("%s: no module info", be->lib);
		dlclose(handle);
		return -EINVAL;
	}
	vmod->dso = handle;

	/*
	 * The vendor HAL must succeed on its first open: after a failed open it
	 * stays broken for the life of the process, and a second open in the same
	 * process hangs (seen after a data wipe: first open -1, retry never
	 * returned). So on failure, end the process; init starts the fingerprint
	 * service again with a fresh copy of the vendor HAL, and that open works
	 * (seen: open fails once, a service restart later works).
	 */
	vdev = NULL;
	ret = vmod->methods->open(vmod, id, &vdev);
	if (ret || !vdev) {
		LOGE("%s: open failed: %d, restarting the fingerprint service for a fresh HAL",
		     be->lib, ret);
		usleep(OPEN_RETRY_STEP_MS * 1000);	/* no tight restart loop */
		_exit(1);
	}
	vendor = (fingerprint_device_t *)vdev;
	LOGI("sensor '%s': %s, device version 0x%x", sensor, be->lib,
	      vdev->version);

	auth_flag = NULL;
	if (be->auth_flag_lib_size) {
		struct stat st;

		if (!stat(be->lib, &st) && st.st_size == be->auth_flag_lib_size) {
			auth_flag = (volatile uint8_t *)vmod + be->auth_flag_off;
			property_set("persist.sys.enroll.status", "1");
			LOGI("enrol token check bypass active");
		} else {
			LOGE("%s is not the known MIUI build, enrol token check stays on",
			      be->lib);
		}
	}

	dev = calloc(1, sizeof(*dev));
	if (!dev) {
		vendor->common.close(vdev);
		vendor = NULL;
		return -ENOMEM;
	}
	dev->common.tag = HARDWARE_DEVICE_TAG;
	dev->common.version = FINGERPRINT_MODULE_API_VERSION_2_1;
	dev->common.module = (struct hw_module_t *)module;
	dev->common.close = wrap_close;
	dev->set_notify = wrap_set_notify;
	dev->pre_enroll = wrap_pre_enroll;
	dev->enroll = wrap_enroll;
	dev->post_enroll = wrap_post_enroll;
	dev->get_authenticator_id = wrap_get_authenticator_id;
	dev->cancel = wrap_cancel;
	dev->enumerate = wrap_enumerate;
	dev->remove = wrap_remove;
	dev->set_active_group = wrap_set_active_group;
	dev->authenticate = wrap_authenticate;

	*device = &dev->common;
	return 0;
}

static struct hw_module_methods_t wrap_methods = {
	.open = wrap_open,
};

fingerprint_module_t HAL_MODULE_INFO_SYM = {
	.common = {
		.tag = HARDWARE_MODULE_TAG,
		.module_api_version = FINGERPRINT_MODULE_API_VERSION_2_1,
		.hal_api_version = HARDWARE_HAL_API_VERSION,
		.id = FINGERPRINT_HARDWARE_MODULE_ID,
		.name = "nikel fingerprint wrapper",
		.author = "nikel",
		.methods = &wrap_methods,
	},
};
