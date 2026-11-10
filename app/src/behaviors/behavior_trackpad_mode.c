/*
 * Copyright (c) 2026 KyleBing
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_trackpad_mode

#include <zephyr/device.h>
#include <drivers/behavior.h>
#include <zephyr/logging/log.h>

#include <dt-bindings/zmk/trackpad_mode.h>
#include <zmk/trackpad_mode.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

static int on_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
    switch (binding->param1) {
    case TP_MOUSE:
        zmk_trackpad_mode_set(ZMK_TRACKPAD_MODE_MOUSE);
        return ZMK_BEHAVIOR_OPAQUE;
    case TP_SCROLL:
        zmk_trackpad_mode_set(ZMK_TRACKPAD_MODE_SCROLL);
        return ZMK_BEHAVIOR_OPAQUE;
    case TP_ARROW:
        zmk_trackpad_mode_set(ZMK_TRACKPAD_MODE_ARROW);
        return ZMK_BEHAVIOR_OPAQUE;
    case TP_TOG_SCROLL:
        zmk_trackpad_mode_toggle_scroll();
        return ZMK_BEHAVIOR_OPAQUE;
    case TP_TOG_ARROW:
        zmk_trackpad_mode_toggle_arrow();
        return ZMK_BEHAVIOR_OPAQUE;
    default:
        LOG_ERR("Unknown trackpad mode command: %d", binding->param1);
        return -ENOTSUP;
    }
}

static int on_keymap_binding_released(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static int behavior_trackpad_mode_init(const struct device *dev) { return 0; }

static const struct behavior_driver_api behavior_trackpad_mode_driver_api = {
    .binding_pressed = on_keymap_binding_pressed,
    .binding_released = on_keymap_binding_released,
    .locality = BEHAVIOR_LOCALITY_GLOBAL,
};

BEHAVIOR_DT_INST_DEFINE(0, behavior_trackpad_mode_init, NULL, NULL, NULL, POST_KERNEL,
                        CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &behavior_trackpad_mode_driver_api);

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
