/*
 * Copyright (C) 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 *
 * Lights HAL for the Redmi Note 4 MTK (nikel), MTK leds class:
 *   lcd-backlight      display backlight (0-255)
 *   button-backlight   capacitive key backlight
 *   red, green, blue   notification LED (blink via "timer" trigger)
 * SHIFT's light HAL drives a KTD2037 chip that nikel does not have.
 */
#define LOG_TAG "lights.nikel"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <log/log.h>
#include <hardware/lights.h>

#define LEDS "/sys/class/leds/"

static pthread_once_t g_init = PTHREAD_ONCE_INIT;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static struct light_state_t g_notification;
static struct light_state_t g_battery;
static struct light_state_t g_attention;

static void init_globals(void)
{
    pthread_mutex_init(&g_lock, NULL);
    memset(&g_notification, 0, sizeof(g_notification));
    memset(&g_battery, 0, sizeof(g_battery));
    memset(&g_attention, 0, sizeof(g_attention));
}

static int write_str(const char *path, const char *s)
{
    int fd = open(path, O_WRONLY);
    if (fd < 0) {
        ALOGW("open %s: %s", path, strerror(errno));
        return -errno;
    }
    ssize_t n = write(fd, s, strlen(s));
    int err = n < 0 ? -errno : 0;
    close(fd);
    return err;
}

static int write_int(const char *path, int v)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%d\n", v);
    return write_str(path, buf);
}

static int is_lit(const struct light_state_t *s)
{
    return s->color & 0x00ffffff;
}

static int rgb_to_brightness(const struct light_state_t *s)
{
    int c = s->color & 0x00ffffff;
    return ((77 * ((c >> 16) & 0xff)) + (150 * ((c >> 8) & 0xff)) +
            (29 * (c & 0xff))) >> 8;
}

static int set_light_backlight(struct light_device_t *dev,
                               const struct light_state_t *state)
{
    (void)dev;
    pthread_mutex_lock(&g_lock);
    int err = write_int(LEDS "lcd-backlight/brightness", rgb_to_brightness(state));
    pthread_mutex_unlock(&g_lock);
    return err;
}

static int set_light_buttons(struct light_device_t *dev,
                             const struct light_state_t *state)
{
    (void)dev;
    /* the kernel has no button-backlight LED yet: stay quiet until it does */
    if (access(LEDS "button-backlight/brightness", W_OK) != 0)
        return 0;
    pthread_mutex_lock(&g_lock);
    int err = write_int(LEDS "button-backlight/brightness", is_lit(state) ? 255 : 0);
    pthread_mutex_unlock(&g_lock);
    return err;
}

static void set_one_led(const char *name, int value, int on_ms, int off_ms)
{
    char path[64];
    snprintf(path, sizeof(path), LEDS "%s/trigger", name);
    if (value && on_ms > 0 && off_ms > 0) {
        write_str(path, "timer");
        snprintf(path, sizeof(path), LEDS "%s/delay_on", name);
        write_int(path, on_ms);
        snprintf(path, sizeof(path), LEDS "%s/delay_off", name);
        write_int(path, off_ms);
    } else {
        write_str(path, "none");
    }
    snprintf(path, sizeof(path), LEDS "%s/brightness", name);
    write_int(path, value);
}

/* caller holds g_lock */
static void update_rgb_led(void)
{
    const struct light_state_t *s;

    /* priority: attention > notification > battery */
    if (is_lit(&g_attention))
        s = &g_attention;
    else if (is_lit(&g_notification))
        s = &g_notification;
    else
        s = &g_battery;

    int on = 0, off = 0;
    if (s->flashMode == LIGHT_FLASH_TIMED || s->flashMode == LIGHT_FLASH_HARDWARE) {
        on = s->flashOnMS;
        off = s->flashOffMS;
    }
    unsigned c = s->color;
    set_one_led("red", (c >> 16) & 0xff, on, off);
    set_one_led("green", (c >> 8) & 0xff, on, off);
    set_one_led("blue", c & 0xff, on, off);
}

static int set_light_notifications(struct light_device_t *dev,
                                   const struct light_state_t *state)
{
    (void)dev;
    pthread_mutex_lock(&g_lock);
    g_notification = *state;
    update_rgb_led();
    pthread_mutex_unlock(&g_lock);
    return 0;
}

static int set_light_battery(struct light_device_t *dev,
                             const struct light_state_t *state)
{
    (void)dev;
    pthread_mutex_lock(&g_lock);
    g_battery = *state;
    update_rgb_led();
    pthread_mutex_unlock(&g_lock);
    return 0;
}

static int set_light_attention(struct light_device_t *dev,
                               const struct light_state_t *state)
{
    (void)dev;
    pthread_mutex_lock(&g_lock);
    g_attention = *state;
    update_rgb_led();
    pthread_mutex_unlock(&g_lock);
    return 0;
}

/* keyboard / bluetooth / wifi: nikel has no such lights */
static int set_light_noop(struct light_device_t *dev,
                          const struct light_state_t *state)
{
    (void)dev;
    (void)state;
    return 0;
}

static int close_lights(struct light_device_t *dev)
{
    free(dev);
    return 0;
}

static int open_lights(const struct hw_module_t *module, const char *name,
                       struct hw_device_t **device)
{
    int (*set_light)(struct light_device_t *, const struct light_state_t *);

    if (!strcmp(LIGHT_ID_BACKLIGHT, name))
        set_light = set_light_backlight;
    else if (!strcmp(LIGHT_ID_BUTTONS, name))
        set_light = set_light_buttons;
    else if (!strcmp(LIGHT_ID_NOTIFICATIONS, name))
        set_light = set_light_notifications;
    else if (!strcmp(LIGHT_ID_BATTERY, name))
        set_light = set_light_battery;
    else if (!strcmp(LIGHT_ID_ATTENTION, name))
        set_light = set_light_attention;
    else
        /* Android 8.1's light HIDL wrapper gives up on the whole HAL if any
         * light type fails to open, so accept the ones nikel lacks. */
        set_light = set_light_noop;

    pthread_once(&g_init, init_globals);

    struct light_device_t *dev = calloc(1, sizeof(*dev));
    if (!dev)
        return -ENOMEM;
    dev->common.tag = HARDWARE_DEVICE_TAG;
    dev->common.version = 0;
    dev->common.module = (struct hw_module_t *)module;
    dev->common.close = (int (*)(struct hw_device_t *))close_lights;
    dev->set_light = set_light;
    *device = (struct hw_device_t *)dev;
    return 0;
}

static struct hw_module_methods_t lights_module_methods = {
    .open = open_lights,
};

struct hw_module_t HAL_MODULE_INFO_SYM = {
    .tag = HARDWARE_MODULE_TAG,
    .version_major = 1,
    .version_minor = 0,
    .id = LIGHTS_HARDWARE_MODULE_ID,
    .name = "nikel lights module",
    .author = "nikel port",
    .methods = &lights_module_methods,
};
