/*
 * Copyright (c) 2021 The ZMK Contributors
 * Copyright (c) 2026 KyleBing
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/led.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

#include <zmk/activity.h>
#include <zmk/hid_indicators.h>
#include <zmk/indicator_capslock.h>
#include <zmk/usb.h>
#include <zmk/event_manager.h>
#include <zmk/keymap.h>
#include <zmk/trackpad_mode.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

BUILD_ASSERT(
    DT_HAS_CHOSEN(zmk_indicator_capslock),
    "CONFIG_ZMK_INDICATOR_CAPSLOCK is enabled but no zmk,indicator_capslock chosen node found");

static const struct device *const indicaps_dev = DEVICE_DT_GET(DT_CHOSEN(zmk_indicator_capslock));

#define CHILD_COUNT(...) +1
#define DT_NUM_CHILD(node_id) (DT_FOREACH_CHILD(node_id, CHILD_COUNT))

#define INDICATOR_LED_NUM_LEDS (DT_NUM_CHILD(DT_CHOSEN(zmk_indicator_capslock)))

#define BRT_MAX 100
#define BURST_PAUSE_TICKS 4
#define LED_TICK_MS 120

struct indicator_capslock_state {
    uint8_t brightness;
    bool on;
};

static struct k_work_delayable polling_work;
static struct indicator_capslock_state state = {
    .brightness = CONFIG_ZMK_INDICATOR_CAPSLOCK_BRT,
    .on = IS_ENABLED(CONFIG_ZMK_IDICATOR_ON_START),
};
static uint8_t burst_step;
static int last_pattern = -1;

static void polling_work_work_handler(struct k_work *work);

static int zmk_indicator_capslock_update() {
    uint8_t brt = zmk_indicator_capslock_get_brt();

    for (int i = 0; i < INDICATOR_LED_NUM_LEDS; i++) {
        int rc = led_set_brightness(indicaps_dev, i, brt);
        if (rc != 0) {
            return rc;
        }
    }
    return 0;
}

static int zmk_indicator_capslock_init(void) {
    if (!device_is_ready(indicaps_dev)) {
        LOG_ERR("indicator_led device \"%s\" is not ready", indicaps_dev->name);
        return -ENODEV;
    }
    k_work_init_delayable(&polling_work, polling_work_work_handler);
    k_work_reschedule(&polling_work, K_MSEC(100));

    return zmk_indicator_capslock_update();
}

static int zmk_indicator_capslock_update_and_save() {
    int rc = zmk_indicator_capslock_update();
    if (rc != 0) {
        return rc;
    }

    return 0;
}

int zmk_indicator_capslock_on() {
    state.brightness = CONFIG_ZMK_INDICATOR_CAPSLOCK_BRT;
    state.on = true;
    return zmk_indicator_capslock_update_and_save();
}

int zmk_indicator_capslock_off() {
    state.on = false;
    return zmk_indicator_capslock_update_and_save();
}

bool zmk_indicator_capslock_is_on() { return state.on; }

int zmk_indicator_capslock_set_brt(uint8_t brightness) {
    state.brightness = MIN(brightness, BRT_MAX);
    state.on = (state.brightness > 0);
    return zmk_indicator_capslock_update_and_save();
}

uint8_t zmk_indicator_capslock_get_brt() { return state.on ? state.brightness : 0; }

static bool hid_capslock_on(void) {
    uint8_t profile = zmk_hid_indicators_get_current_profile();
    return profile == 2 || profile == 3 || profile == 7;
}

/* n flashes then a pause: on, off, on, off, ... then idle dark. */
static void apply_burst(uint8_t flashes) {
    uint8_t onoff = flashes * 2;
    uint8_t period = onoff + BURST_PAUSE_TICKS;
    uint8_t phase = burst_step % period;
    bool lit = (phase < onoff) && ((phase % 2) == 0);

    if (lit) {
        zmk_indicator_capslock_on();
    } else {
        zmk_indicator_capslock_off();
    }
    burst_step++;
}

static void polling_work_work_handler(struct k_work *work) {
    enum zmk_trackpad_mode mode = zmk_trackpad_mode_get();
    int pattern;

    if (mode == ZMK_TRACKPAD_MODE_SCROLL) {
        pattern = 2;
    } else if (mode == ZMK_TRACKPAD_MODE_ARROW) {
        pattern = 4;
    } else if (hid_capslock_on()) {
        pattern = 0; /* solid */
    } else {
        pattern = -1; /* off */
    }

    if (pattern != last_pattern) {
        burst_step = 0;
        last_pattern = pattern;
    }

    if (pattern > 0) {
        apply_burst((uint8_t)pattern);
    } else if (pattern == 0) {
        zmk_indicator_capslock_on();
    } else {
        zmk_indicator_capslock_off();
    }

    k_work_reschedule(&polling_work, K_MSEC(LED_TICK_MS));
}

SYS_INIT(zmk_indicator_capslock_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
