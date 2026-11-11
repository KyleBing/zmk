/*
 * SPDX-License-Identifier: MIT
 *
 * 0 off | 1 solid | 2-6 burst 1-5 | 7 blink | 8 breathe | 9 strobe | 10 heart | 11 pulse
 */

#pragma once

#include <stdint.h>

#define ZMK_LED_FX_OFF 0
#define ZMK_LED_FX_SOLID 1
#define ZMK_LED_FX_BURST1 2
#define ZMK_LED_FX_BURST2 3
#define ZMK_LED_FX_BURST3 4
#define ZMK_LED_FX_BURST4 5
#define ZMK_LED_FX_BURST5 6
#define ZMK_LED_FX_BLINK 7
#define ZMK_LED_FX_BREATHE 8
#define ZMK_LED_FX_STROBE 9
#define ZMK_LED_FX_HEART 10
#define ZMK_LED_FX_PULSE 11

struct zmk_led_fx_state {
    uint16_t step;
    uint8_t brt;
    int8_t dir;
};

static inline uint8_t zmk_led_fx_burst(uint8_t flashes, uint8_t pause, uint16_t step, uint8_t max_brt) {
    uint8_t onoff = flashes * 2;
    uint8_t period = onoff + pause;
    if (period == 0) {
        return 0;
    }
    uint8_t phase = step % period;
    return (phase < onoff && (phase % 2) == 0) ? max_brt : 0;
}

static inline uint8_t zmk_led_fx_tick(int pattern, struct zmk_led_fx_state *s, uint8_t max_brt,
                                      uint8_t min_brt, uint8_t step_sz, uint8_t pause) {
    s->step++;
    if (max_brt == 0) {
        max_brt = 100;
    }
    if (min_brt > max_brt) {
        min_brt = max_brt > 10 ? 10 : 0;
    }
    if (step_sz == 0) {
        step_sz = 5;
    }
    if (pause == 0) {
        pause = 4;
    }

    switch (pattern) {
    case ZMK_LED_FX_SOLID:
        return max_brt;
    case ZMK_LED_FX_BURST1:
        return zmk_led_fx_burst(1, pause, s->step, max_brt);
    case ZMK_LED_FX_BURST2:
        return zmk_led_fx_burst(2, pause, s->step, max_brt);
    case ZMK_LED_FX_BURST3:
        return zmk_led_fx_burst(3, pause, s->step, max_brt);
    case ZMK_LED_FX_BURST4:
        return zmk_led_fx_burst(4, pause, s->step, max_brt);
    case ZMK_LED_FX_BURST5:
        return zmk_led_fx_burst(5, pause, s->step, max_brt);
    case ZMK_LED_FX_BLINK:
        return ((s->step % 4) < 2) ? max_brt : 0;
    case ZMK_LED_FX_STROBE:
        return (s->step % 2) ? max_brt : 0;
    case ZMK_LED_FX_HEART: {
        uint8_t ph = s->step % 10;
        return (ph == 0 || ph == 2) ? max_brt : 0;
    }
    case ZMK_LED_FX_PULSE:
        return ((s->step % 8) < 5) ? max_brt : 0;
    case ZMK_LED_FX_BREATHE: {
        if (s->dir == 0) {
            s->dir = 1;
            s->brt = min_brt;
        }
        int b = (int)s->brt + (int)s->dir * (int)step_sz;
        if (b >= (int)max_brt) {
            b = max_brt;
            s->dir = -1;
        }
        if (b <= (int)min_brt) {
            b = min_brt;
            s->dir = 1;
        }
        s->brt = (uint8_t)b;
        return s->brt;
    }
    case ZMK_LED_FX_OFF:
    default:
        return 0;
    }
}
