/*
 * G233 WDT (Watchdog Timer) controller
 *
 * Copyright (c) 2025 Chao Liu <chao.liu@yeah.net>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Register map (base 0x10010000):
 *   0x00  WDT_CTRL  — bit0: EN, bit1: INTEN
 *   0x04  WDT_LOAD  — reload value
 *   0x08  WDT_VAL   — current counter (read-only)
 *   0x0C  WDT_KEY   — write 0x5A5A5A5A=feed, 0x1ACCE551=lock
 *   0x10  WDT_SR    — bit0: TIMEOUT (write-1-to-clear)
 */

#ifndef HW_G233_WDT_H
#define HW_G233_WDT_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_G233_WDT "g233-wdt"
OBJECT_DECLARE_SIMPLE_TYPE(G233WdtState, G233_WDT)

#define G233_WDT_SIZE  0x1000

/* Register offsets */
#define G233_WDT_REG_CTRL    0x00
#define G233_WDT_REG_LOAD    0x04
#define G233_WDT_REG_VAL     0x08
#define G233_WDT_REG_KEY     0x0C
#define G233_WDT_REG_SR      0x10

/* WDT_CTRL bit fields */
#define G233_WDT_CTRL_EN     (1u << 0)
#define G233_WDT_CTRL_INTEN  (1u << 1)

/* WDT_KEY values */
#define G233_WDT_KEY_FEED    0x5A5A5A5A
#define G233_WDT_KEY_LOCK    0x1ACCE551

/* WDT_SR bit fields */
#define G233_WDT_SR_TIMEOUT  (1u << 0)

/* Counter scaling: 1 count per microsecond */
#define G233_WDT_NS_PER_TICK 1000

struct G233WdtState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    qemu_irq irq;

    uint32_t ctrl;
    uint32_t load;
    uint32_t val;
    uint32_t sr;
    bool locked;
    uint64_t last_update;
};

#endif /* HW_G233_WDT_H */
