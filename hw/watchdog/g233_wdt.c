/*
 * G233 WDT (Watchdog Timer) controller
 *
 * Copyright (c) 2025 Chao Liu <chao.liu@yeah.net>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/timer.h"
#include "hw/core/irq.h"
#include "hw/watchdog/g233_wdt.h"
#include "migration/vmstate.h"

static void g233_wdt_update_irq(G233WdtState *s)
{
    bool pending = (s->sr & G233_WDT_SR_TIMEOUT) &&
                   (s->ctrl & G233_WDT_CTRL_INTEN);
    qemu_set_irq(s->irq, pending);
}

static void g233_wdt_update_counter(G233WdtState *s)
{
    if (!(s->ctrl & G233_WDT_CTRL_EN)) {
        return;
    }

    uint64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    uint64_t elapsed = now - s->last_update;
    if (elapsed == 0 || s->val == 0) {
        return;
    }

    s->last_update = now;

    uint64_t elapsed_ticks = elapsed / G233_WDT_NS_PER_TICK;
    if (elapsed_ticks == 0) {
        return;
    }

    if (elapsed_ticks >= s->val) {
        s->sr |= G233_WDT_SR_TIMEOUT;
        s->val = 0;
        g233_wdt_update_irq(s);
    } else {
        s->val -= elapsed_ticks;
    }
}

static uint64_t g233_wdt_read(void *opaque, hwaddr offset, unsigned int size)
{
    G233WdtState *s = G233_WDT(opaque);
    uint64_t r = 0;

    switch (offset) {
    case G233_WDT_REG_CTRL:
        r = s->ctrl;
        break;
    case G233_WDT_REG_LOAD:
        r = s->load;
        break;
    case G233_WDT_REG_VAL:
        g233_wdt_update_counter(s);
        r = s->val;
        break;
    case G233_WDT_REG_KEY:
        r = 0;
        break;
    case G233_WDT_REG_SR:
        g233_wdt_update_counter(s);
        r = s->sr;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: bad read offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
    }

    return r;
}

static void g233_wdt_write(void *opaque, hwaddr offset,
                           uint64_t value, unsigned int size)
{
    G233WdtState *s = G233_WDT(opaque);
    uint32_t val = (uint32_t)value;

    g233_wdt_update_counter(s);

    switch (offset) {
    case G233_WDT_REG_CTRL:
        if (s->locked) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: write to locked WDT ignored\n", __func__);
            break;
        }
        s->ctrl = val & (G233_WDT_CTRL_EN | G233_WDT_CTRL_INTEN);
        if (s->ctrl & G233_WDT_CTRL_EN) {
            /* Reload counter when enabling */
            s->val = s->load;
            s->last_update = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        }
        g233_wdt_update_irq(s);
        break;
    case G233_WDT_REG_LOAD:
        if (s->locked) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: write to locked WDT ignored\n", __func__);
            break;
        }
        s->load = val;
        break;
    case G233_WDT_REG_VAL:
        /* read-only, ignore writes */
        break;
    case G233_WDT_REG_KEY:
        if (val == G233_WDT_KEY_FEED) {
            /* Feed the watchdog */
            s->val = s->load;
            s->last_update = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
            s->sr &= ~G233_WDT_SR_TIMEOUT;
            g233_wdt_update_irq(s);
        } else if (val == G233_WDT_KEY_LOCK) {
            s->locked = true;
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: bad WDT_KEY value 0x%x\n", __func__, val);
        }
        break;
    case G233_WDT_REG_SR:
        /* write-1-to-clear */
        s->sr &= ~val;
        g233_wdt_update_irq(s);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: bad write offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
    }
}

static const MemoryRegionOps g233_wdt_ops = {
    .read = g233_wdt_read,
    .write = g233_wdt_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
};

static void g233_wdt_reset(DeviceState *dev)
{
    G233WdtState *s = G233_WDT(dev);

    s->ctrl = 0;
    s->load = 0;
    s->val = 0;
    s->sr = 0;
    s->locked = false;
    s->last_update = 0;
}

static void g233_wdt_realize(DeviceState *dev, Error **errp)
{
    G233WdtState *s = G233_WDT(dev);

    memory_region_init_io(&s->mmio, OBJECT(dev), &g233_wdt_ops, s,
                          TYPE_G233_WDT, G233_WDT_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->mmio);
    sysbus_init_irq(SYS_BUS_DEVICE(dev), &s->irq);
}

static const VMStateDescription vmstate_g233_wdt = {
    .name = TYPE_G233_WDT,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(ctrl, G233WdtState),
        VMSTATE_UINT32(load, G233WdtState),
        VMSTATE_UINT32(val, G233WdtState),
        VMSTATE_UINT32(sr, G233WdtState),
        VMSTATE_BOOL(locked, G233WdtState),
        VMSTATE_UINT64(last_update, G233WdtState),
        VMSTATE_END_OF_LIST()
    }
};

static void g233_wdt_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &vmstate_g233_wdt;
    dc->realize = g233_wdt_realize;
    device_class_set_legacy_reset(dc, g233_wdt_reset);
    dc->desc = "G233 WDT";
}

static const TypeInfo g233_wdt_info = {
    .name          = TYPE_G233_WDT,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(G233WdtState),
    .class_init    = g233_wdt_class_init,
};

static void g233_wdt_register_types(void)
{
    type_register_static(&g233_wdt_info);
}

type_init(g233_wdt_register_types)
