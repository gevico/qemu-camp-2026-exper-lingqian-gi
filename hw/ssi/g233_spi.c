/*
 * G233 SPI controller
 *
 * Copyright (c) 2025 Chao Liu <chao.liu@yeah.net>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-properties.h"
#include "hw/ssi/ssi.h"
#include "hw/ssi/g233_spi.h"
#include "qemu/log.h"
#include "migration/vmstate.h"

static void g233_spi_update_irq(G233SpiState *s)
{
    bool pending = false;

    if ((s->sr & G233_SPI_SR_TXE) && (s->cr1 & G233_SPI_CR1_TXEIE)) {
        pending = true;
    }
    if ((s->sr & G233_SPI_SR_RXNE) && (s->cr1 & G233_SPI_CR1_RXNEIE)) {
        pending = true;
    }
    if ((s->sr & G233_SPI_SR_OVERRUN) && (s->cr1 & G233_SPI_CR1_ERRIE)) {
        pending = true;
    }

    qemu_set_irq(s->irq, pending);
}

static void g233_spi_update_cs(G233SpiState *s)
{
    int i;

    for (i = 0; i < s->num_cs; i++) {
        /* CS is active low: assert (drive low) when selected */
        bool cs_asserted = (i == (s->cr2 & 0x03));
        qemu_set_irq(s->cs_lines[i], !cs_asserted);
    }
}

static uint64_t g233_spi_read(void *opaque, hwaddr offset, unsigned int size)
{
    G233SpiState *s = G233_SPI(opaque);
    uint32_t r = 0;

    switch (offset) {
    case G233_SPI_REG_CR1:
        r = s->cr1;
        break;
    case G233_SPI_REG_CR2:
        r = s->cr2;
        break;
    case G233_SPI_REG_SR:
        r = s->sr;
        break;
    case G233_SPI_REG_DR:
        r = s->rx_buf;
        s->sr &= ~G233_SPI_SR_RXNE;
        g233_spi_update_irq(s);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: bad read offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
    }

    return r;
}

static void g233_spi_write(void *opaque, hwaddr offset,
                           uint64_t value, unsigned int size)
{
    G233SpiState *s = G233_SPI(opaque);
    uint8_t tx_byte = value & 0xFF;

    switch (offset) {
    case G233_SPI_REG_CR1:
        s->cr1 = value & (G233_SPI_CR1_SPE | G233_SPI_CR1_MSTR |
                          G233_SPI_CR1_ERRIE | G233_SPI_CR1_RXNEIE |
                          G233_SPI_CR1_TXEIE);
        if (s->cr1 & G233_SPI_CR1_SPE) {
            s->sr |= G233_SPI_SR_TXE;  /* TX buffer empty */
        } else {
            s->sr = 0;
            s->rx_buf = 0;
        }
        g233_spi_update_irq(s);
        break;

    case G233_SPI_REG_CR2:
        s->cr2 = value & 0x03;
        g233_spi_update_cs(s);
        break;

    case G233_SPI_REG_DR:
        if (s->cr1 & G233_SPI_CR1_SPE) {
            if (s->sr & G233_SPI_SR_RXNE) {
                s->sr |= G233_SPI_SR_OVERRUN;
            }
            s->rx_buf = (uint8_t)ssi_transfer(s->spi, tx_byte);
            s->sr |= G233_SPI_SR_RXNE;
        }
        s->sr |= G233_SPI_SR_TXE;
        g233_spi_update_irq(s);
        break;

    case G233_SPI_REG_SR:
        /* write-1-to-clear for OVERRUN */
        s->sr &= ~(value & G233_SPI_SR_OVERRUN);
        g233_spi_update_irq(s);
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: bad write offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
    }
}

static const MemoryRegionOps g233_spi_ops = {
    .read = g233_spi_read,
    .write = g233_spi_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
};

static void g233_spi_reset(DeviceState *dev)
{
    G233SpiState *s = G233_SPI(dev);

    s->cr1 = 0;
    s->cr2 = 0;
    s->sr = 0;
    s->rx_buf = 0;
}

static void g233_spi_realize(DeviceState *dev, Error **errp)
{
    G233SpiState *s = G233_SPI(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    int i;

    s->spi = ssi_create_bus(dev, "spi");
    sysbus_init_irq(sbd, &s->irq);

    s->cs_lines = g_new0(qemu_irq, s->num_cs);
    for (i = 0; i < s->num_cs; i++) {
        sysbus_init_irq(sbd, &s->cs_lines[i]);
    }

    memory_region_init_io(&s->mmio, OBJECT(dev), &g233_spi_ops, s,
                          TYPE_G233_SPI, 0x1000);
    sysbus_init_mmio(sbd, &s->mmio);
}

static const Property g233_spi_properties[] = {
    DEFINE_PROP_UINT32("num-cs", G233SpiState, num_cs, 2),
};

static const VMStateDescription vmstate_g233_spi = {
    .name = TYPE_G233_SPI,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(cr1, G233SpiState),
        VMSTATE_UINT32(cr2, G233SpiState),
        VMSTATE_UINT32(sr, G233SpiState),
        VMSTATE_UINT8(rx_buf, G233SpiState),
        VMSTATE_END_OF_LIST()
    }
};

static void g233_spi_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &vmstate_g233_spi;
    dc->realize = g233_spi_realize;
    device_class_set_legacy_reset(dc, g233_spi_reset);
    device_class_set_props(dc, g233_spi_properties);
    dc->desc = "G233 SPI controller";
}

static const TypeInfo g233_spi_info = {
    .name          = TYPE_G233_SPI,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(G233SpiState),
    .class_init    = g233_spi_class_init,
};

static void g233_spi_register_types(void)
{
    type_register_static(&g233_spi_info);
}
type_init(g233_spi_register_types)
