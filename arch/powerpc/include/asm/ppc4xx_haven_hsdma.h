/*
 * PPC4xx HAVEN HSDMA (I2O/DMA) support
 *
 * Copyright 2007 AMCC
 * Victor Gallardo<vgallardo@amcc.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef __ASMPPC_PPC4xx_HAVEN_HSDMA_H
#define __ASMPPC_PPC4xx_HAVEN_HSDMA_H

#include <linux/types.h>

#if defined(CONFIG_NPSIM_MODULE) || defined(CONFIG_NPSIM)
#include <asm/npsim_interrupt.h>
#else
#include <asm/io.h>
#endif

/*
 * HSDMA Port Numbers
 */

#if defined(CONFIG_460SP) || defined(CONFIG_460SPE)
#define PPC4xx_HSDMA_OFFSET 0 /* request_dma offset from other DMA engines */
#define PPC4xx_HSDMA0 (PPC4xx_HSDMA_OFFSET + 0)
#define PPC4xx_HSDMA1 (PPC4xx_HSDMA_OFFSET + 1)
#elif defined(CONFIG_460GT) || defined(CONFIG_460EX) || \
      defined(CONFIG_NPSIM_MODULE) || defined(CONFIG_NPSIM) || \
      defined(CONFIG_460EXR) || defined (CONFIG_431EXR) || \
      defined(CONFIG_AM81281) 
#define PPC4xx_HSDMA_OFFSET 4 /* request_dma offset from other DMA engines */
#define PPC4xx_HSDMA1 (PPC4xx_HSDMA_OFFSET + 1)
#else
#error "HSDMA ports have not been define for specifid SoC"
#endif

/*
 * HSDMA opcodes
 */

#define HSDMA_NO_OP             0x00000000
#define HSDMA_MOVE_SG1_SG2      0x01000000
#define HSDMA_MULTICAST         0x05000000
#define HSDMA_DATA_CHECK_128    0x23000000
#define HSDMA_DATA_FILL_128     0x24000000
#define HSDMA_LFSR_RESET        0x80000000
#define HSDMA_LFSR_CHECK        0x83000000
#define HSDMA_LFSR_FILL         0x84000000
#define HSDMA_LFSR_CHECK_INVERT 0xC3000000
#define HSDMA_LFSR_FILL_INVERT  0xC4000000
#if defined(CONFIG_460GT) || defined(CONFIG_460EX) || \
    defined(CONFIG_NPSIM_MODULE) || defined(CONFIG_NPSIM) || \
    defined(CONFIG_460EXR) || defined(CONFIG_431EXR) || \
    defined(CONFIG_AM81281)
#define HSDMA_MOVE_SG1_SG2_CRC  0xD0000000
#define HSDMA_GENERATE_CRC      0xD1000000
#endif

/*
 * CDB ATTRIBUTES
 */

#define HSDMA_RLS_ORDER         0x00800000
#define HSDMA_NO_SNOOP          0x00400000
#define HSDMA_NO_MBUSYWAIT      0x00200000
#define HSDMA_CHAIN_CRC         0x00100000

/*
 * Public Functions Declarations
 */

#if defined(CONFIG_PPC4xx_HSDMA_TEST)
void ppc4xx_hsdma_self_test(u32 dmanr);
#endif

int ppc4xx_hsdma_request_dma(u32 dmanr);

void ppc4xx_hsdma_free_dma(u32 dmanr);

void ppc4xx_hsdma_noop(u32 dmanr,
                       void (*cback)(int status, void *dev, u32 id, u64 crc),
                       void *dev, u32 id);

void ppc4xx_hsdma_memcpy(u32 dmanr, size_t size,
                       phys_addr_t src, phys_addr_t dst,
                       void (*cback)(int status, void *dev, u32 id, u64 crc),
                       void *dev, u32 id);

void ppc4xx_hsdma_memcpy2(u32 dmanr, size_t size,
                       phys_addr_t src, phys_addr_t dst1, phys_addr_t dst2,
                       void (*cback)(int status, void *dev, u32 id, u64 crc),
                       void *dev, u32 id);

void ppc4xx_hsdma_data_fill(u32 dmanr, size_t size,
                       phys_addr_t src, phys_addr_t pLo, phys_addr_t pHi,
                       void (*cback)(int status, void *dev, u32 id, u64 crc),
                       void *dev, u32 id);

void ppc4xx_hsdma_data_check(u32 dmanr, size_t size,
                       phys_addr_t src, phys_addr_t pLo, phys_addr_t pHi,
                       void (*cback)(int status, void *dev, u32 id, u64 crc),
                       void *dev, u32 id);

void ppc4xx_hsdma_lfsr_reset(u32 dmanr, phys_addr_t sLo, phys_addr_t sHi,
                       void (*cback)(int status, void *dev, u32 id, u64 crc),
                       void *dev, u32 id);

void ppc4xx_hsdma_lfsr_fill(u32 dmanr, size_t size, phys_addr_t buff,
                       void (*cback)(int status, void *dev, u32 id, u64 crc),
                       void *dev, u32 id);

void ppc4xx_hsdma_lfsr_check(u32 dmanr, size_t size, phys_addr_t buff,
                       void (*cback)(int status, void *dev, u32 id, u64 crc),
                       void *dev, u32 id);

void ppc4xx_hsdma_lfsr_ifill(u32 dmanr, size_t size, phys_addr_t buff,
                       void (*cback)(int status, void *dev, u32 id, u64 crc),
                       void *dev, u32 id);

void ppc4xx_hsdma_lfsr_icheck(u32 dmanr, size_t size, phys_addr_t buff,
                       void (*cback)(int status, void *dev, u32 id, u64 crc),
                       void *dev, u32 id);

int ppc4xx_hsdma_reset(void);

#if defined(CONFIG_460GT) || defined(CONFIG_460EX) || \
    defined(CONFIG_NPSIM_MODULE) || defined(CONFIG_NPSIM) || \
    defined(CONFIG_460EXR) || defined(CONFIG_431EXR) || \
    defined(CONFIG_AM81281)
void ppc4xx_hsdma_memcpy_crc(u32 dmanr, size_t size,
                        phys_addr_t src, phys_addr_t dst,
                       void (*cback)(int status, void *dev, u32 id, u64 crc),
                       void *dev, u32 id);

void ppc4xx_hsdma_generate_crc(u32 dmanr, size_t size,
                        phys_addr_t src, phys_addr_t dst,
                       void (*cback)(int status, void *dev, u32 id, u64 crc),
                       void *dev, u32 id);
#endif

void ppc4xx_hsdma(u32 dmanr, size_t size, u32 opcode,
                 phys_addr_t sg1, phys_addr_t sg2, phys_addr_t sg3,
                 void (*cback)(int status, void *dev, u32 id, u64 crc),
                 void *dev, u32 id);

#endif /*__ASMPPC_PPC4xx_HAVEN_HSDMA_H*/

