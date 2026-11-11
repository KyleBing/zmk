/*
 * Copyright (c) 2021 The ZMK Contributors
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
#include <zmk/indicator_led.h>
#include <zmk/usb.h>
#include <zmk/event_manager.h>
#include <zmk/events/activity_state_changed.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/keymap.h>
#include <zmk/led_fx.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

BUILD_ASSERT(DT_HAS_CHOSEN(zmk_led_indicator),
             "CONFIG_ZMK_INDICATOR_LED is enabled but no zmk,led_indicator chosen node found");

static const struct device *const indiled_dev = DEVICE_DT_GET(DT_CHOSEN(zmk_led_indicator));

#define CHILD_COUNT(...) +1
#define DT_NUM_CHILD(node_id) (DT_FOREACH_CHILD(node_id, CHILD_COUNT))

#define INDICATOR_LED_NUM_LEDS (DT_NUM_CHILD(DT_CHOSEN(zmk_led_indicator)))

#define BRT_MAX 100

struct indicator_led_state {
    uint8_t brightness;
    bool on;
    bool cycle_state;
    bool blinky_state;
};
struct indicator_led_cycle {
    uint8_t brightness;
    bool cycle_direction;
};
struct indicator_led_blinky {
    uint8_t brightness;
    bool blinky_direction;
};
static struct indicator_led_cycle led_cycle = {
    .brightness = CONFIG_ZMK_IDICATOR_LAYER_CYCLE_MINBRT,
    .cycle_direction = true,
};
static struct indicator_led_blinky led_blinky = {
    .brightness = CONFIG_ZMK_IDICATOR_LAYER_BRT,
    .blinky_direction = true,
};
static struct k_work_delayable polling_work;
static struct k_work_delayable cycle_work;
static struct k_work_delayable blinky_work;
static struct indicator_led_state state = {
    .brightness = CONFIG_ZMK_IDICATOR_LAYER_BRT,
    .on = IS_ENABLED(CONFIG_ZMK_IDICATOR_ON_START),
    .cycle_state = false,
    .blinky_state = false,
};
static uint8_t last_layer = 0xFF;
static int last_pattern = -1;
static struct zmk_led_fx_state fx;
static void polling_work_work_handler(struct k_work *work);
int zmk_indicator_led_on();
int zmk_indicator_led_off();

static int pattern_for_layer(uint8_t layer) {
    switch (layer) {
    case 1:
        return CONFIG_ZMK_LED_LAYER1_PATTERN;
    case 2:
        return CONFIG_ZMK_LED_LAYER2_PATTERN;
    default:
        return CONFIG_ZMK_LED_LAYER0_PATTERN;
    }
}

static void apply_pattern(int pattern) {
    uint8_t brt = zmk_led_fx_tick(pattern, &fx, CONFIG_ZMK_IDICATOR_LAYER_BRT,
                                  CONFIG_ZMK_IDICATOR_LAYER_CYCLE_MINBRT,
                                  CONFIG_ZMK_IDICATOR_BRT_STEP, CONFIG_ZMK_LED_BURST_PAUSE_TICKS);
    if (brt == 0) {
        zmk_indicator_led_off();
        return;
    }
    zmk_indicator_led_set_brt(brt);
}

static int zmk_indicator_led_update() {
    uint8_t brt = zmk_indicator_led_get_brt();
    // LOG_DBG("Update indicator_led brightness: %d%%", brt);

    for (int i = 0; i < INDICATOR_LED_NUM_LEDS; i++) {
        int rc = led_set_brightness(indiled_dev, i, brt);
        if (rc != 0) {
            // LOG_ERR("Failed to update indicator_led LED %d: %d", i, rc);
            return rc;
        }
    }
    return 0;
}
void indicator_led_brightness_cal(void) {

    if (led_cycle.brightness >= CONFIG_ZMK_IDICATOR_LAYER_CYCLE_MAXBRT) {
        led_cycle.cycle_direction = false;
    }
    if (led_cycle.brightness <= CONFIG_ZMK_IDICATOR_LAYER_CYCLE_MINBRT) {
        led_cycle.cycle_direction = true;
    }
    led_cycle.brightness =
        (led_cycle.cycle_direction ? (led_cycle.brightness + CONFIG_ZMK_IDICATOR_BRT_STEP)
                                   : (led_cycle.brightness - CONFIG_ZMK_IDICATOR_BRT_STEP));
}

void indicator_led_brightness_blinky(void) {
    if (led_blinky.brightness == CONFIG_ZMK_IDICATOR_LAYER_CYCLE_MAXBRT) {
        led_blinky.blinky_direction = false;
    }
    if (led_blinky.brightness == CONFIG_ZMK_IDICATOR_LAYER_CYCLE_MINBRT) {
        led_blinky.blinky_direction = true;
    }
    led_blinky.brightness =
        (led_blinky.blinky_direction ? (CONFIG_ZMK_IDICATOR_LAYER_CYCLE_MAXBRT)
                                     : (CONFIG_ZMK_IDICATOR_LAYER_CYCLE_MINBRT));
}

static void cycle_work_work_handler(struct k_work *work) {
    indicator_led_brightness_cal();

    zmk_indicator_led_set_brt(led_cycle.brightness);
    k_work_reschedule(&cycle_work, K_MSEC(CONFIG_ZMK_IDICATOR_LAYER_CYCLE_INTERVAL));
}
static void blinky_work_work_handler(struct k_work *work) {
    indicator_led_brightness_blinky();

    zmk_indicator_led_set_brt(led_blinky.brightness);
    int BLINK_INTERVAL = CONFIG_ZMK_IDICATOR_BLINK_INTERVAL;
    k_work_reschedule(&blinky_work, K_MSEC(BLINK_INTERVAL));
}
static int zmk_indicator_led_init(void) {
    if (!device_is_ready(indiled_dev)) {
        LOG_ERR("indicator_led device \"%s\" is not ready", indiled_dev->name);
        return -ENODEV;
    }
    k_work_init_delayable(&polling_work, polling_work_work_handler);
    k_work_init_delayable(&cycle_work, cycle_work_work_handler);
    k_work_init_delayable(&blinky_work, blinky_work_work_handler);
    k_work_reschedule(&polling_work, K_MSEC(100));

    return zmk_indicator_led_update();
}

static int zmk_indicator_led_update_and_save() {
    int rc = zmk_indicator_led_update();
    if (rc != 0) {
        return rc;
    }

    return 0;
}

int zmk_indicator_led_on() {
    state.brightness = MAX(state.brightness, CONFIG_ZMK_IDICATOR_BRT_STEP);
    state.on = true;
    return zmk_indicator_led_update_and_save();
}

int zmk_indicator_led_off() {
    state.on = false;
    return zmk_indicator_led_update_and_save();
}

int zmk_indicator_led_toggle() {
    return state.on ? zmk_indicator_led_off() : zmk_indicator_led_on();
}

bool zmk_indicator_led_is_on() { return state.on; }

int zmk_indicator_led_set_brt(uint8_t brightness) {
    state.brightness = MIN(brightness, BRT_MAX);
    state.on = (state.brightness > 0);
    return zmk_indicator_led_update_and_save();
}

uint8_t zmk_indicator_led_get_brt() { return state.on ? state.brightness : 0; }

uint8_t zmk_indicator_led_calc_brt(int direction) {
    int brt = state.brightness + (direction * CONFIG_ZMK_IDICATOR_BRT_STEP);
    return CLAMP(brt, CONFIG_ZMK_IDICATOR_LAYER_CYCLE_MINBRT,
                 CONFIG_ZMK_IDICATOR_LAYER_CYCLE_MAXBRT);
}

uint8_t zmk_indicator_led_calc_brt_cycle() {
    if (state.brightness == BRT_MAX) {
        return 0;
    } else {
        return zmk_indicator_led_calc_brt(1);
    }
}
static inline void cycle_schedule(void) {
    if (state.cycle_state) {
        k_work_reschedule(&cycle_work, K_MSEC(10));
        return;
    }
    k_work_cancel_delayable(&cycle_work);
}
static inline void blinky_schedule(void) {
    if (state.blinky_state) {
        k_work_reschedule(&blinky_work, K_MSEC(10));
        return;
    }
    k_work_cancel_delayable(&blinky_work);
}
static inline void cycle_onoff(bool onoff) {
    if (onoff) {
        if (!state.cycle_state) {
            state.cycle_state = true;
            cycle_schedule();
        }
        return;
    }
    if (state.cycle_state) {
        state.cycle_state = false;
        cycle_schedule();
    }
}
static inline void blinky_onoff(bool onoff) {
    if (onoff) {
        if (!state.blinky_state) {
            state.blinky_state = true;
            blinky_schedule();
        }
        return;
    }
    if (state.blinky_state) {
        state.blinky_state = false;
        blinky_schedule();
    }
}

static void polling_work_work_handler(struct k_work *work) {
    uint8_t layer = zmk_keymap_highest_layer_active();
    int pattern = pattern_for_layer(layer);

    cycle_onoff(false);
    blinky_onoff(false);

    if (layer != last_layer || pattern != last_pattern) {
        fx.step = 0;
        fx.brt = CONFIG_ZMK_IDICATOR_LAYER_CYCLE_MINBRT;
        fx.dir = 1;
        last_layer = layer;
        last_pattern = pattern;
    }

    apply_pattern(pattern);
    k_work_reschedule(&polling_work, K_MSEC(120));
}
/*static void polling_work_work_handler(struct k_work *work) {
    for (int i = 0; i < 4; i++) {
        if (zmk_keymap_layer_active(i)) {
            switch (i) {
            case 0:
                cycle_onoff(false);
                zmk_indicator_led_off();
                blinky_onoff(false);
                break;
            case 1:
                cycle_onoff(false);
                zmk_indicator_led_set_brt(CONFIG_ZMK_IDICATOR_LAYER_BRT);
                zmk_indicator_led_on();
                blinky_onoff(false);

                break;
            case 2:
                if (!zmk_indicator_led_is_on())
                    zmk_indicator_led_on();
                cycle_onoff(true);
                blinky_onoff(false);
                break;
            case 3:
                if (!zmk_indicator_led_is_on())
                    zmk_indicator_led_on();
                cycle_onoff(false);
                blinky_onoff(true);
                break;

            default:
                break;
            }
        }
    }
    k_work_reschedule(&polling_work, K_MSEC(100));
}*/

SYS_INIT(zmk_indicator_led_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
