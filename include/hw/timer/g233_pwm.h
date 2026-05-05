/*
 * G233 PWM controller
 *
 * Copyright (c) 2025 Chao Liu <chao.liu@yeah.net>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Register map (base 0x10015000), 4 channels:
 *   0x00  PWM_GLB      — bits[3:0] CHn_EN mirror, bits[7:4] CHn_DONE (w1c)
 *   Per channel (CHn at 0x10 + n*0x10):
 *     +0x00 CHn_CTRL   — bit0: EN, bit1: POL
 *     +0x04 CHn_PERIOD — period value
 *     +0x08 CHn_DUTY   — duty cycle
 *     +0x0C CHn_CNT    — counter (read-only)
 */

#ifndef HW_G233_PWM_H
#define HW_G233_PWM_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_G233_PWM "g233-pwm"
OBJECT_DECLARE_SIMPLE_TYPE(G233PwmState, G233_PWM)

#define G233_PWM_CHANS 4
#define G233_PWM_SIZE  0x100

/* PWM_GLB bit fields */
#define G233_PWM_GLB_CH_EN(n)   (1u << (n))
#define G233_PWM_GLB_CH_DONE(n) (1u << (4 + (n)))

/* CHn_CTRL bit fields */
#define G233_PWM_CTRL_EN   (1u << 0)
#define G233_PWM_CTRL_POL  (1u << 1)

struct G233PwmState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    qemu_irq irq;

    uint32_t glb;
    uint32_t ctrl[G233_PWM_CHANS];
    uint32_t period[G233_PWM_CHANS];
    uint32_t duty[G233_PWM_CHANS];
    uint32_t cnt[G233_PWM_CHANS];
    uint64_t last_update[G233_PWM_CHANS];
};

#endif /* HW_G233_PWM_H */
