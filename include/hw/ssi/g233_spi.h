/*
 * G233 SPI controller
 *
 * Copyright (c) 2025 Chao Liu <chao.liu@yeah.net>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Register map (base 0x10018000):
 *   0x00  SPI_CR1  — bit0: SPE, bit2: MSTR, bit5: ERRIE,
 *                     bit6: RXNEIE, bit7: TXEIE
 *   0x04  SPI_CR2  — bits[1:0]: CS select
 *   0x08  SPI_SR   — bit0: RXNE, bit1: TXE, bit4: OVERRUN (w1c)
 *   0x0C  SPI_DR   — data register
 */

#ifndef HW_G233_SPI_H
#define HW_G233_SPI_H

#include "hw/core/sysbus.h"
#include "hw/ssi/ssi.h"
#include "qom/object.h"

#define TYPE_G233_SPI "g233-spi"
OBJECT_DECLARE_SIMPLE_TYPE(G233SpiState, G233_SPI)

/* Register offsets */
#define G233_SPI_REG_CR1    0x00
#define G233_SPI_REG_CR2    0x04
#define G233_SPI_REG_SR     0x08
#define G233_SPI_REG_DR     0x0C

/* SPI_CR1 bit fields */
#define G233_SPI_CR1_SPE     (1u << 0)
#define G233_SPI_CR1_MSTR    (1u << 2)
#define G233_SPI_CR1_ERRIE   (1u << 5)
#define G233_SPI_CR1_RXNEIE  (1u << 6)
#define G233_SPI_CR1_TXEIE   (1u << 7)

/* SPI_SR bit fields */
#define G233_SPI_SR_RXNE     (1u << 0)
#define G233_SPI_SR_TXE      (1u << 1)
#define G233_SPI_SR_OVERRUN  (1u << 4)

struct G233SpiState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    qemu_irq irq;

    uint32_t num_cs;
    qemu_irq *cs_lines;

    SSIBus *spi;

    uint32_t cr1;
    uint32_t cr2;
    uint32_t sr;
    uint8_t rx_buf;
};

#endif /* HW_G233_SPI_H */
