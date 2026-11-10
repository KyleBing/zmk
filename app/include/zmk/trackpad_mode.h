/*
 * Copyright (c) 2026 KyleBing
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>

enum zmk_trackpad_mode {
    ZMK_TRACKPAD_MODE_MOUSE = 0,
    ZMK_TRACKPAD_MODE_SCROLL = 1,
    ZMK_TRACKPAD_MODE_ARROW = 2,
};

enum zmk_trackpad_mode zmk_trackpad_mode_get(void);
void zmk_trackpad_mode_set(enum zmk_trackpad_mode mode);
void zmk_trackpad_mode_toggle_scroll(void);
void zmk_trackpad_mode_toggle_arrow(void);
