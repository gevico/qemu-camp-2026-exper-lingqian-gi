/*
 * G233 GPIO controller
 *
 * Copyright (c) 2025 Chao Liu <chao.liu@yeah.net>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/core/irq.h"
#include "hw/gpio/g233_gpio.h"
#include "migration/vmstate.h"
#include "trace.h"

static void g233_gpio_update_irq(G233GPIOState *s)
{
    uint32_t pending = 0;

    /* Simplified interrupt logic: IS set based on trigger/polarity */
    for (int i = 0; i < G233_GPIO_PINS; i++) {
        if (!(s->ie & (1u << i))) {
            continue;
        }
        if (s->is & (1u << i)) {
            pending |= (1u << i);
        }
    }

    qemu_set_irq(s->irq, pending != 0);
}

static uint64_t g233_gpio_read(void *opaque, hwaddr offset, unsigned int size)
{
    G233GPIOState *s = G233_GPIO(opaque);
    uint64_t r = 0;

    switch (offset) {
    case G233_GPIO_REG_DIR:
        r = s->dir;
        break;
    case G233_GPIO_REG_OUT:
        r = s->out;
        break;
    case G233_GPIO_REG_IN:
        /* When pin is output, reflect OUT value; when input, return shadow */
        r = (s->out & s->dir) | (s->in_shadow & ~s->dir);
        break;
    case G233_GPIO_REG_IE:
        r = s->ie;
        break;
    case G233_GPIO_REG_IS:
        r = s->is;
        break;
    case G233_GPIO_REG_TRIG:
        r = s->trig;
        break;
    case G233_GPIO_REG_POL:
        r = s->pol;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: bad read offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
    }

    return r;
}

static void g233_gpio_write(void *opaque, hwaddr offset,
                            uint64_t value, unsigned int size)
{
    G233GPIOState *s = G233_GPIO(opaque);

    switch (offset) {
    case G233_GPIO_REG_DIR:
        s->dir = value;
        break;
    case G233_GPIO_REG_OUT:
    {
        uint32_t old_out = s->out;
        s->out = value;

        /* Simplified edge/level detection */
        for (int i = 0; i < G233_GPIO_PINS; i++) {
            if (!(s->ie & (1u << i))) {
                continue;
            }

            bool old_bit = (old_out >> i) & 1;
            bool new_bit = (s->out >> i) & 1;
            bool trig_edge = !(s->trig & (1u << i));  /* edge-triggered */
            bool pol_high = (s->pol >> i) & 1;         /* polarity */

            if (trig_edge) {
                /* Edge-triggered */
                bool edge = pol_high ? (new_bit && !old_bit) : (!new_bit && old_bit);
                if (edge) {
                    s->is |= (1u << i);
                }
            } else {
                /* Level-triggered */
                bool active = pol_high ? new_bit : !new_bit;
                if (active) {
                    s->is |= (1u << i);
                } else {
                    s->is &= ~(1u << i);
                }
            }
        }
        g233_gpio_update_irq(s);
        break;
    }
    case G233_GPIO_REG_IN:
        /* read-only, ignore writes */
        break;
    case G233_GPIO_REG_IE:
        s->ie = value;
        g233_gpio_update_irq(s);
        break;
    case G233_GPIO_REG_IS:
        /* Write 1 to clear */
        s->is &= ~value;
        g233_gpio_update_irq(s);
        break;
    case G233_GPIO_REG_TRIG:
        s->trig = value;
        break;
    case G233_GPIO_REG_POL:
        s->pol = value;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: bad write offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
    }
}

static const MemoryRegionOps g233_gpio_ops = {
    .read = g233_gpio_read,
    .write = g233_gpio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
};

static void g233_gpio_reset(DeviceState *dev)
{
    G233GPIOState *s = G233_GPIO(dev);

    s->dir = 0;
    s->out = 0;
    s->in_shadow = 0;
    s->ie = 0;
    s->is = 0;
    s->trig = 0;
    s->pol = 0;
}

static void g233_gpio_realize(DeviceState *dev, Error **errp)
{
    G233GPIOState *s = G233_GPIO(dev);

    memory_region_init_io(&s->mmio, OBJECT(dev), &g233_gpio_ops, s,
                          TYPE_G233_GPIO, G233_GPIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->mmio);
    sysbus_init_irq(SYS_BUS_DEVICE(dev), &s->irq);
}

static const VMStateDescription vmstate_g233_gpio = {
    .name = TYPE_G233_GPIO,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(dir, G233GPIOState),
        VMSTATE_UINT32(out, G233GPIOState),
        VMSTATE_UINT32(in_shadow, G233GPIOState),
        VMSTATE_UINT32(ie, G233GPIOState),
        VMSTATE_UINT32(is, G233GPIOState),
        VMSTATE_UINT32(trig, G233GPIOState),
        VMSTATE_UINT32(pol, G233GPIOState),
        VMSTATE_END_OF_LIST()
    }
};

static void g233_gpio_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &vmstate_g233_gpio;
    dc->realize = g233_gpio_realize;
    device_class_set_legacy_reset(dc, g233_gpio_reset);
    dc->desc = "G233 GPIO";
}

static const TypeInfo g233_gpio_info = {
    .name = TYPE_G233_GPIO,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(G233GPIOState),
    .class_init = g233_gpio_class_init,
};

static void g233_gpio_register_types(void)
{
    type_register_static(&g233_gpio_info);
}

type_init(g233_gpio_register_types)
