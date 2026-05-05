/*
 * G233 GPIO controller
 *
 * Copyright (c) 2025 Chao Liu <chao.liu@yeah.net>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_G233_GPIO_H
#define HW_G233_GPIO_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_G233_GPIO "g233-gpio"
OBJECT_DECLARE_SIMPLE_TYPE(G233GPIOState, G233_GPIO)

#define G233_GPIO_PINS 32
#define G233_GPIO_SIZE 0x100

/* Register offsets */
#define G233_GPIO_REG_DIR    0x00   /* direction (0=input, 1=output) */
#define G233_GPIO_REG_OUT    0x04   /* output data */
#define G233_GPIO_REG_IN     0x08   /* input data (read-only) */
#define G233_GPIO_REG_IE     0x0C   /* interrupt enable */
#define G233_GPIO_REG_IS     0x10   /* interrupt status (w1c) */
#define G233_GPIO_REG_TRIG   0x14   /* trigger type (0=edge, 1=level) */
#define G233_GPIO_REG_POL    0x18   /* polarity (0=low/falling, 1=high/rising) */

struct G233GPIOState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    qemu_irq irq;

    uint32_t dir;
    uint32_t out;
    uint32_t in_shadow;
    uint32_t ie;
    uint32_t is;
    uint32_t trig;
    uint32_t pol;
};

#endif /* HW_G233_GPIO_H */
