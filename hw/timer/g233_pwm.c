/*
 * G233 PWM controller
 *
 * Copyright (c) 2025 Chao Liu <chao.liu@yeah.net>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/timer.h"
#include "hw/core/irq.h"
#include "hw/timer/g233_pwm.h"
#include "migration/vmstate.h"

/* Register offsets */
#define REG_GLB      0x00

#define CH_REGS_BASE 0x10
#define CH_STRIDE    0x10
#define CH_CTRL      0x00
#define CH_PERIOD    0x04
#define CH_DUTY      0x08
#define CH_CNT       0x0C

static void g233_pwm_update_cnt(G233PwmState *s, int ch)
{
    if (!(s->ctrl[ch] & G233_PWM_CTRL_EN)) {
        return;
    }
    if (!s->period[ch]) {
        return;
    }

    uint64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    uint64_t elapsed = now - s->last_update[ch];
    if (elapsed == 0) {
        return;
    }

    s->last_update[ch] = now;

    /* Advance counter by elapsed virtual time (1 count per ns) */
    s->cnt[ch] += elapsed;

    /* Wrap at period boundary, set DONE for each completed period */
    if (s->cnt[ch] >= s->period[ch]) {
        s->glb |= G233_PWM_GLB_CH_DONE(ch);
        s->cnt[ch] %= s->period[ch];
    }
}

static void g233_pwm_update_all(G233PwmState *s)
{
    for (int i = 0; i < G233_PWM_CHANS; i++) {
        g233_pwm_update_cnt(s, i);
    }
}

static uint64_t g233_pwm_read(void *opaque, hwaddr offset, unsigned int size)
{
    G233PwmState *s = G233_PWM(opaque);
    uint64_t r = 0;

    switch (offset) {
    case REG_GLB:
        g233_pwm_update_all(s);
        r = s->glb;
        return r;
    default:
        break;
    }

    if (offset >= CH_REGS_BASE && offset < G233_PWM_SIZE) {
        uint32_t ch_off = offset - CH_REGS_BASE;
        unsigned int ch = ch_off / CH_STRIDE;
        unsigned int reg = ch_off % CH_STRIDE;

        if (ch < G233_PWM_CHANS) {
            switch (reg) {
            case CH_CTRL:
                r = s->ctrl[ch];
                return r;
            case CH_PERIOD:
                r = s->period[ch];
                return r;
            case CH_DUTY:
                r = s->duty[ch];
                return r;
            case CH_CNT:
                g233_pwm_update_cnt(s, ch);
                r = s->cnt[ch];
                return r;
            default:
                break;
            }
        }
    }

    qemu_log_mask(LOG_GUEST_ERROR,
                  "%s: bad read offset 0x%" HWADDR_PRIx "\n",
                  __func__, offset);
    return 0;
}

static void g233_pwm_write(void *opaque, hwaddr offset,
                           uint64_t value, unsigned int size)
{
    G233PwmState *s = G233_PWM(opaque);
    uint32_t val = (uint32_t)value;

    switch (offset) {
    case REG_GLB:
        g233_pwm_update_all(s);
        /* CHn_DONE bits are write-1-to-clear */
        for (int i = 0; i < G233_PWM_CHANS; i++) {
            if (val & G233_PWM_GLB_CH_DONE(i)) {
                s->glb &= ~G233_PWM_GLB_CH_DONE(i);
            }
        }
        return;
    default:
        break;
    }

    if (offset >= CH_REGS_BASE && offset < G233_PWM_SIZE) {
        uint32_t ch_off = offset - CH_REGS_BASE;
        unsigned int ch = ch_off / CH_STRIDE;
        unsigned int reg = ch_off % CH_STRIDE;

        if (ch < G233_PWM_CHANS) {
            switch (reg) {
            case CH_CTRL:
            {
                uint32_t old_ctrl = s->ctrl[ch];
                s->ctrl[ch] = val & (G233_PWM_CTRL_EN | G233_PWM_CTRL_POL);

                /* Update GLB CHn_EN mirror on EN change */
                if ((old_ctrl ^ s->ctrl[ch]) & G233_PWM_CTRL_EN) {
                    if (s->ctrl[ch] & G233_PWM_CTRL_EN) {
                        s->glb |= G233_PWM_GLB_CH_EN(ch);
                        /* Reset counter when enabling */
                        s->cnt[ch] = 0;
                        s->last_update[ch] =
                            qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
                    } else {
                        s->glb &= ~G233_PWM_GLB_CH_EN(ch);
                    }
                }
                return;
            }
            case CH_PERIOD:
                s->period[ch] = val;
                return;
            case CH_DUTY:
                s->duty[ch] = val;
                return;
            case CH_CNT:
                /* read-only, ignore writes */
                return;
            default:
                break;
            }
        }
    }

    qemu_log_mask(LOG_GUEST_ERROR,
                  "%s: bad write offset 0x%" HWADDR_PRIx "\n",
                  __func__, offset);
}

static const MemoryRegionOps g233_pwm_ops = {
    .read = g233_pwm_read,
    .write = g233_pwm_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
};

static void g233_pwm_reset(DeviceState *dev)
{
    G233PwmState *s = G233_PWM(dev);

    s->glb = 0;
    for (int i = 0; i < G233_PWM_CHANS; i++) {
        s->ctrl[i] = 0;
        s->period[i] = 0;
        s->duty[i] = 0;
        s->cnt[i] = 0;
        s->last_update[i] = 0;
    }
}

static void g233_pwm_realize(DeviceState *dev, Error **errp)
{
    G233PwmState *s = G233_PWM(dev);

    memory_region_init_io(&s->mmio, OBJECT(dev), &g233_pwm_ops, s,
                          TYPE_G233_PWM, G233_PWM_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->mmio);
    sysbus_init_irq(SYS_BUS_DEVICE(dev), &s->irq);
}

static const VMStateDescription vmstate_g233_pwm = {
    .name = TYPE_G233_PWM,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(glb, G233PwmState),
        VMSTATE_UINT32_ARRAY(ctrl, G233PwmState, G233_PWM_CHANS),
        VMSTATE_UINT32_ARRAY(period, G233PwmState, G233_PWM_CHANS),
        VMSTATE_UINT32_ARRAY(duty, G233PwmState, G233_PWM_CHANS),
        VMSTATE_UINT32_ARRAY(cnt, G233PwmState, G233_PWM_CHANS),
        VMSTATE_UINT64_ARRAY(last_update, G233PwmState, G233_PWM_CHANS),
        VMSTATE_END_OF_LIST()
    }
};

static void g233_pwm_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &vmstate_g233_pwm;
    dc->realize = g233_pwm_realize;
    device_class_set_legacy_reset(dc, g233_pwm_reset);
    dc->desc = "G233 PWM";
}

static const TypeInfo g233_pwm_info = {
    .name          = TYPE_G233_PWM,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(G233PwmState),
    .class_init    = g233_pwm_class_init,
};

static void g233_pwm_register_types(void)
{
    type_register_static(&g233_pwm_info);
}

type_init(g233_pwm_register_types)
