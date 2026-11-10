/*
 * Copyright (c) 2020 The ZMK Contributors
 * Copyright (c) 2026 KyleBing
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>
#include <stdlib.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/settings/settings.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/matrix.h>
#include <zmk/kscan.h>
#include <zmk/display.h>
#include <drivers/ext_power.h>

#include <zmk/hid.h>
#include <zmk/keys.h>
#include <dt-bindings/zmk/mouse.h>
#include <dt-bindings/zmk/hid_usage.h>
#include <dt-bindings/zmk/hid_usage_pages.h>
#include <zmk/hid_indicators.h>
#include <zmk/indicator_capslock.h>
#include <zmk/endpoints.h>
#include <zmk/trackpad_mode.h>
#ifdef CONFIG_ZMK_MOUSE
#include <zmk/mouse.h>
#endif /* CONFIG_ZMK_MOUSE */

static const struct device *get_a320_device(void) {
    const struct device *dev = DEVICE_DT_GET_ANY(avago_a320);

    if (dev == NULL) {
        printk("\nError: no device found.\n");
        return NULL;
    }
    if (!device_is_ready(dev)) {
        printk("\nError: Device \"%s\" is not ready; "
               "check the driver initialization logs for errors.\n",
               dev->name);
        return NULL;
    }
    printk("Found device \"%s\", getting sensor data\n", dev->name);
    return dev;
}

static bool hid_caps_scroll_active(void) {
    uint8_t profile = zmk_hid_indicators_get_current_profile();
    return profile == 2 || profile == 3 || profile == 7 || profile == 4;
}

static void compute_scroll_from_delta(int8_t x, int8_t y, int8_t *scroll_x, int8_t *scroll_y) {
    if (abs(y) >= 128) {
        *scroll_x = -x / 24;
        *scroll_y = -y / 24;
    } else if (abs(y) >= 64) {
        *scroll_x = -x / 16;
        *scroll_y = -y / 16;
    } else if (abs(y) >= 32) {
        *scroll_x = -x / 12;
        *scroll_y = -y / 12;
    } else if (abs(y) >= 21) {
        *scroll_x = -x / 8;
        *scroll_y = -y / 8;
    } else if (abs(y) >= 3) {
        *scroll_x = -(x > 0) ? 1 : (x < 0) ? -1 : 0;
        *scroll_y = -((y > 0) ? 1 : (y < 0) ? -1 : 0);
    } else {
        *scroll_x = -(x > 0) ? 1 : (x < 0) ? -1 : 0;
        *scroll_y = 0;
    }
}

static void scale_mouse_delta(int8_t *x, int8_t *y) {
    *x = ((*x < 127) ? *x : (*x - 256)) * 1.5 * CONFIG_TRACKPAD_SPEEDMULTIPLIER_HORIZONTAL / 100;
    *y = ((*y < 127) ? *y : (*y - 256)) * 1.5 * CONFIG_TRACKPAD_SPEEDMULTIPLIER_VERTICAL / 100;
}

static void send_mouse_report(int8_t x, int8_t y, int8_t scroll_x, int8_t scroll_y) {
    zmk_hid_mouse_movement_set(0, 0);
    zmk_hid_mouse_movement_update(x, y);
    zmk_hid_mouse_scroll_set(0, 0);
    zmk_hid_mouse_scroll_update(scroll_x, scroll_y);
    zmk_endpoints_send_mouse_report();
}

static void tap_arrow_key(zmk_key_t key) {
    zmk_hid_keyboard_press(key);
    zmk_endpoints_send_report(HID_USAGE_KEY);
    zmk_hid_keyboard_release(key);
    zmk_endpoints_send_report(HID_USAGE_KEY);
    k_sleep(K_MSEC(CONFIG_TRACKPAD_ARROW_INTERVAL));
}

static void handle_arrow_mode(int8_t x, int8_t y) {
    static int16_t acc_x;
    static int16_t acc_y;
    const int16_t threshold = CONFIG_TRACKPAD_ARROW_THRESHOLD;

    /* Same signed conversion as mouse path before speed scaling. */
    int16_t dx = (x < 127) ? x : (x - 256);
    int16_t dy = (y < 127) ? y : (y - 256);

    acc_x += dx;
    acc_y += dy;

    while (abs(acc_x) >= threshold || abs(acc_y) >= threshold) {
        if (abs(acc_x) >= abs(acc_y)) {
            if (acc_x > 0) {
                tap_arrow_key(HID_USAGE_KEY_KEYBOARD_RIGHTARROW);
                acc_x -= threshold;
            } else {
                tap_arrow_key(HID_USAGE_KEY_KEYBOARD_LEFTARROW);
                acc_x += threshold;
            }
        } else {
            if (acc_y > 0) {
                tap_arrow_key(HID_USAGE_KEY_KEYBOARD_DOWNARROW);
                acc_y -= threshold;
            } else {
                tap_arrow_key(HID_USAGE_KEY_KEYBOARD_UPARROW);
                acc_y += threshold;
            }
        }
    }

    /* No mouse movement while in arrow mode. */
    send_mouse_report(0, 0, 0, 0);
}

static void handle_legacy_caps_or_mouse(int8_t x, int8_t y) {
    int8_t scroll_x = 0;
    int8_t scroll_y = 0;

    if (hid_caps_scroll_active()) {
        compute_scroll_from_delta(x, y, &scroll_x, &scroll_y);
        k_sleep(K_MSEC(CONFIG_TRACKPAD_SCROLL_INTERVAL));
        x = 0;
        y = 0;
    } else {
        scale_mouse_delta(&x, &y);
    }

    send_mouse_report(x, y, scroll_x, scroll_y);
}

/*
 * Pixel-ish wheel: accumulate movement, emit small HID notches every poll,
 * then coast with decaying velocity after lift (Magic Mouse-like inertia).
 * CONFIG_TRACKPAD_SCROLL_INTERVAL is distance-per-notch, not a sleep.
 */
static void handle_inertial_scroll(int8_t raw_x, int8_t raw_y, bool active) {
    static int32_t acc_x, acc_y;
    static int32_t vel_x, vel_y;
    static uint8_t idle;
    static uint8_t axis; /* 0 none, 1 horiz, 2 vert */

    if (!active) {
        acc_x = acc_y = vel_x = vel_y = 0;
        idle = 0;
        axis = 0;
        return;
    }

    const int32_t unit = MAX((int32_t)CONFIG_TRACKPAD_SCROLL_INTERVAL, 8);
    const int16_t dx = raw_x;
    const int16_t dy = raw_y;
    const int16_t dead = 1;

    if (abs(dx) > dead || abs(dy) > dead) {
        idle = 0;
        acc_x += dx;
        acc_y += dy;
        vel_x = CLAMP((vel_x + dx * 3) / 2, -40, 40);
        vel_y = CLAMP((vel_y + dy * 3) / 2, -40, 40);
    } else {
        if (idle < 200) {
            idle++;
        }
        if (idle >= 2) {
            vel_x = (vel_x * 92) / 100;
            vel_y = (vel_y * 92) / 100;
            acc_x += vel_x;
            acc_y += vel_y;
            if (abs(vel_x) <= 1 && abs(vel_y) <= 1) {
                acc_x = acc_y = vel_x = vel_y = 0;
                axis = 0;
            }
        }
    }

    if (axis == 0) {
        if (abs(acc_x) >= unit || abs(acc_y) >= unit) {
            axis = (abs(acc_y) >= abs(acc_x)) ? 2 : 1;
        }
    }

    int8_t sx = 0;
    int8_t sy = 0;
    if (axis == 2) {
        int32_t q = acc_y / unit;
        sy = (int8_t)CLAMP(-q, -4, 4);
        acc_y -= (-(int32_t)sy) * unit;
        acc_x = 0;
        vel_x = 0;
    } else if (axis == 1) {
        int32_t q = acc_x / unit;
        sx = (int8_t)CLAMP(-q, -4, 4);
        acc_x -= (-(int32_t)sx) * unit;
        acc_y = 0;
        vel_y = 0;
    }

    if (sx != 0 || sy != 0) {
        send_mouse_report(0, 0, sx, sy);
    }
}

static void handle_bbq10_trackpad(int8_t x, int8_t y) {
    enum zmk_trackpad_mode mode = zmk_trackpad_mode_get();

    handle_inertial_scroll(x, y, mode == ZMK_TRACKPAD_MODE_SCROLL);

    switch (mode) {
    case ZMK_TRACKPAD_MODE_SCROLL:
        break;
    case ZMK_TRACKPAD_MODE_ARROW:
        handle_arrow_mode(x, y);
        break;
    case ZMK_TRACKPAD_MODE_MOUSE:
    default:
        scale_mouse_delta(&x, &y);
        send_mouse_report(x, y, 0, 0);
        break;
    }
}

int main(void) {
    LOG_INF("Welcome to ZMK!\n");

    if (zmk_kscan_init(DEVICE_DT_GET(ZMK_MATRIX_NODE_ID)) != 0) {
        return -ENOTSUP;
    }
    const struct device *dev = get_a320_device();
    if (dev == NULL) {
        return -ENODEV;
    }
#ifdef CONFIG_ZMK_DISPLAY
    zmk_display_init();
#endif /* CONFIG_ZMK_DISPLAY */
    struct sensor_value xy_pos;
    while (1) {
        sensor_channel_get(dev, SENSOR_CHAN_AMBIENT_TEMP, &xy_pos);
        char target_9900[] = "bb9900";
        char target_q10[] = "bbq10";
        char target_q20[] = "bbq20";
        char target_q30[] = "bbq30";
        char target_9981[] = "bbp9981";
        char target_9983[] = "bbp9983";
        char target_bbcase[] = "bbcase";

        if (strcmp(CONFIG_ZMK_KEYBOARD_NAME, target_9900) == 0 ||
            strcmp(CONFIG_ZMK_KEYBOARD_NAME, target_bbcase) == 0) {
            int8_t x = xy_pos.val2;
            int8_t y = xy_pos.val1;
            handle_legacy_caps_or_mouse(x, y);
        } else if (strcmp(CONFIG_ZMK_KEYBOARD_NAME, target_q10) == 0) {
            int8_t x = xy_pos.val1;
            int8_t y = -1 * xy_pos.val2;
            handle_bbq10_trackpad(x, y);
        } else if (strcmp(CONFIG_ZMK_KEYBOARD_NAME, target_q30) == 0 ||
                   strcmp(CONFIG_ZMK_KEYBOARD_NAME, target_9981) == 0 ||
                   strcmp(CONFIG_ZMK_KEYBOARD_NAME, target_9983) == 0) {
            int8_t x = xy_pos.val1;
            int8_t y = -1 * xy_pos.val2;
            handle_legacy_caps_or_mouse(x, y);
        } else if (strcmp(CONFIG_ZMK_KEYBOARD_NAME, target_q20) == 0) {
            int8_t x = -1 * xy_pos.val1;
            int8_t y = xy_pos.val2;
            handle_legacy_caps_or_mouse(x, y);
        }

        int polling_ms = (1.0 / (float)CONFIG_INPUT_A320_POLLINGRATE) * 1000.0;
        k_sleep(K_MSEC(polling_ms));
    }
}
