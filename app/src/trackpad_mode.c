/*
 * Copyright (c) 2026 KyleBing
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zmk/trackpad_mode.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

static enum zmk_trackpad_mode current_mode = ZMK_TRACKPAD_MODE_MOUSE;

enum zmk_trackpad_mode zmk_trackpad_mode_get(void) { return current_mode; }

void zmk_trackpad_mode_set(enum zmk_trackpad_mode mode) {
    current_mode = mode;
    LOG_INF("trackpad mode -> %d", (int)mode);
}

void zmk_trackpad_mode_toggle_scroll(void) {
    if (current_mode == ZMK_TRACKPAD_MODE_SCROLL) {
        zmk_trackpad_mode_set(ZMK_TRACKPAD_MODE_MOUSE);
    } else {
        zmk_trackpad_mode_set(ZMK_TRACKPAD_MODE_SCROLL);
    }
}

void zmk_trackpad_mode_toggle_arrow(void) {
    if (current_mode == ZMK_TRACKPAD_MODE_ARROW) {
        zmk_trackpad_mode_set(ZMK_TRACKPAD_MODE_MOUSE);
    } else {
        zmk_trackpad_mode_set(ZMK_TRACKPAD_MODE_ARROW);
    }
}
