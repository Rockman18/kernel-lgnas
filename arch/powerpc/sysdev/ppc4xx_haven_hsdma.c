/*
 * PPC4xx HSDMA (I2O/DMA) support
 *    include/asm-ppc/ppc4xx_hsdma.h
 *    arch/ppc/syslib/ppc4xx_hsdma.c
 *    arch/ppc/syslib/ppc4xx_hsdma_test.c
 *
 * Copyright 2007 AMCC
 * Victor Gallardo<vgallardo@amcc.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/module.h>       /* Needed by all modules            */
#include <linux/init.h>         /* Needed for the module_xxx macros */
#include <linux/dma-mapping.h>  /* Needed to allocate DMA memory    */
#include <linux/dmapool.h>      /* Needed to allocate DMA memory    */
#include <linux/delay.h>
#include <asm/cacheflush.h>     /* Needed to flush cache            */
#include <asm/ppc4xx_haven_hsdma.h>   /* HSDMA API                        */
#include <asm/io.h>
#include <linux/interrupt.h>
#include <linux/of_platform.h>
#include <asm/dcr-native.h>

/* SDR read/write helper macros */
#define DCRN_SDR_CONFIG_ADDR    0x00E
#define DCRN_SDR_CONFIG_DATA    0x00F

#define SDR_READ(offset) ({                     \
	mtdcr(DCRN_SDR_CONFIG_ADDR, offset);    \
	mfdcr(DCRN_SDR_CONFIG_DATA);})
#define SDR_WRITE(offset, data) ({              \
	mtdcr(DCRN_SDR_CONFIG_ADDR, offset);    \
	mtdcr(DCRN_SDR_CONFIG_DATA, data);})


/*
 * Interface Information
 */

#define PPC4xx_HSDMA "ppc4xx_hsdma"
#define PPC4xx_HSDMA_VERSION "0.1"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("AMCC");
MODULE_VERSION(PPC4xx_HSDMA_VERSION);
MODULE_DESCRIPTION("PowerPC 4xx HSDMA Interface");

/*
 * Kernel Logging
 */

#define LOG(string, args...) \
        printk(KERN_INFO PPC4xx_HSDMA ": " string "\n",##args)

#define ALERT(string, args...) \
        printk(KERN_ALERT PPC4xx_HSDMA ": " string "\n",##args)

#define WARNING(string, args...) \
        printk(KERN_WARNING PPC4xx_HSDMA ": WARNING, " string "\n",##args)

#define ERR(string, args...) \
        printk(KERN_ALERT PPC4xx_HSDMA ": ERROR, " string "\n",##args)

#if defined(CONFIG_PPC4xx_HSDMA_DEBUG)
#define DBG(string, args...) \
        printk(KERN_INFO PPC4xx_HSDMA ": " string "\n",##args)
#else
#define DBG(string, args...)   do { } while (0)
#endif

/*
 * Helpful Status Macros
 */

#define HSDMA_SUCCESS 0

#ifdef CONFIG_NOT_COHERENT_CACHE

#define dma_cache_inv(_start,_size) \
        invalidate_dcache_range(_start, (_start + _size))
#define dma_cache_wback(_start,_size) \
        clean_dcache_range(_start, (_start + _size))
#define dma_cache_wback_inv(_start,_size) \
        flush_dcache_range(_start, (_start + _size))

#else

#define dma_cache_inv(_start,_size)             do { } while (0)
#define dma_cache_wback(_start,_size)           do { } while (0)
#define dma_cache_wback_inv(_start,_size)       do { } while (0)

#endif




#define SA_INTERRUPT 0
#define CONFIG_PPC4xx_HSDMA_FIFO_SIZE 256
#define CONFIG_PPC4xx_HSDMA_MAX_CDB_SG_COUNT 16777214
#define DCRN_SDR_SRST0          0x0200


/* Register offset */
#if defined(CONFIG_440SP)
#define DCRN_I2O0_BASE         0x0060
#define MMIO_I2O0_BASE_HI      0x00000001
#define MMIO_I2O0_BASE_LO      0x00100000
#define MMIO_I2O0_BASE_EN      0x00100001
#define MMIO_I2O0_BASE         0x0000000100100000ull
#elif defined(CONFIG_440SPE)
#define DCRN_I2O0_BASE         0x0060
#define MMIO_I2O0_BASE_HI      0x00000004
#define MMIO_I2O0_BASE_LO      0x00100000
#define MMIO_I2O0_BASE_EN      0x00100001
#define MMIO_I2O0_BASE         0x0000000400100000ull
#elif defined(CONFIG_460GT) || defined(CONFIG_460EX) || \
      defined(CONFIG_460EXR) || defined(CONFIG_431EXR) || \
      defined(CONFIG_AM81281)
#define DCRN_I2O0_BASE         0x0060
#define MMIO_I2O0_BASE_HI      0x00000004
#define MMIO_I2O0_BASE_LO      0x00100000
#define MMIO_I2O0_BASE_EN      0x00100001
#define MMIO_I2O0_BASE         0x0000000400100000ull
#else
#error "Unknown SoC, please check chip manual"
#endif

/*
 * Interrupt Lines
 */

#if defined(CONFIG_440SP) || defined(CONFIG_440SPE)
#define HSDMA0_IRQ_FULL     (19)
#define HSDMA0_IRQ_SRVC     (20)
#define HSDMA1_IRQ_FULL     (21)
#define HSDMA1_IRQ_SRVC     (22)
#define HSDMAx_IRQ_DMA_ERR  (32+22)
#define HSDMAx_IRQ_I2O_ERR  (32+23)
#elif defined(CONFIG_460GT) || defined(CONFIG_460EX) || \
      defined(CONFIG_460EXR) || defined(CONFIG_431EXR) || \
      defined(CONFIG_AM81281)
#define HSDMA1_IRQ_FULL     (21)
#define HSDMA1_IRQ_SRVC     (22)
#define HSDMAx_IRQ_DMA_ERR  (32+22)
#define HSDMAx_IRQ_I2O_ERR  (32+23)
#else
#error "Unknown SoC, please define IRQ lines"
#endif

#define HSDMA_IOPIS_DMA0_ERR  0x00000020
#define HSDMA_IOPIS_DMA1_ERR  0x00000100
#define HSDMA_IOPIS_I200_ERR  0x00001000 

#define HSDMA_CLEAR_DMA_HALT  0x00000040

/*
 * DCR Register offsets
 */

#define DCRN_I2O0_LLIRQ0L   (DCRN_I2O0_BASE + 0x00)
#define DCRN_I2O0_LLIRQ0H   (DCRN_I2O0_BASE + 0x01)
#define DCRN_I2O0_LLWR0M    (DCRN_I2O0_BASE + 0x02)
#define DCRN_I2O0_LLIRQ1L   (DCRN_I2O0_BASE + 0x03)
#define DCRN_I2O0_LLIRQ1H   (DCRN_I2O0_BASE + 0x04)
#define DCRN_I2O0_LLWR1M    (DCRN_I2O0_BASE + 0x05)
#define DCRN_I2O0_IBAL      (DCRN_I2O0_BASE + 0x06)
#define DCRN_I2O0_IBAH      (DCRN_I2O0_BASE + 0x07)
#define DCRN_I2O0_DMA0_ST   (DCRN_I2O0_BASE + 0x08)
#define DCRN_I2O0_DMA1_ST   (DCRN_I2O0_BASE + 0x09)
#define DCRN_I2O0_ISTAT     (DCRN_I2O0_BASE + 0x0A)
#define DCRN_I2O0_DMA0_OP   (DCRN_I2O0_BASE + 0x0B)
#define DCRN_I2O0_DMA1_OP   (DCRN_I2O0_BASE + 0x0C)
#define DCRN_I2O0_SEAT      (DCRN_I2O0_BASE + 0x0D)
#define DCRN_I2O0_SEAD      (DCRN_I2O0_BASE + 0x0E)
#define DCRN_I2O0_I2O_OP    (DCRN_I2O0_BASE + 0x0F)
#define DCRN_I2O0_HWR0L     (DCRN_I2O0_BASE + 0x10)
#define DCRN_I2O0_HWR0H     (DCRN_I2O0_BASE + 0x11)
#define DCRN_I2O0_HWROM     (DCRN_I2O0_BASE + 0x12)
#define DCRN_I2O0_HWR1L     (DCRN_I2O0_BASE + 0x13)
#define DCRN_I2O0_HWR1H     (DCRN_I2O0_BASE + 0x14)
#define DCRN_I2O0_HWR1M     (DCRN_I2O0_BASE + 0x15)
#define DCRN_I2O0_MISCC     (DCRN_I2O0_BASE + 0x16)

/*
 * Memory Mapped IO Registers offsets
 */

#define MMIO_I2O0_ISTS   (0x0000)
#define MMIO_I2O0_ISEAT  (0x0004)
#define MMIO_I2O0_ISEAD  (0x0008)
#define MMIO_I2O0_IDBEL  (0x0020)
#define MMIO_I2O0_IHIS   (0x0030)
#define MMIO_I2O0_IHIM   (0x0034)
#define MMIO_I2O0_IHIQ   (0x0040)
#define MMIO_I2O0_IHOQ   (0x0044)
#define MMIO_I2O0_IOPIS  (0x0050)
#define MMIO_I2O0_IOPIM  (0x0054)
#define MMIO_I2O0_IOPIQ  (0x0058)
#define MMIO_I2O0_IOPOQ  (0x005C)
#define MMIO_I2O0_IIFLH  (0x0060)
#define MMIO_I2O0_IIFLT  (0x0062)
#define MMIO_I2O0_IIPLH  (0x0064)
#define MMIO_I2O0_IIPLT  (0x0066)
#define MMIO_I2O0_IOFLH  (0x0068)
#define MMIO_I2O0_IOFLT  (0x006A)
#define MMIO_I2O0_IOPLH  (0x006C)
#define MMIO_I2O0_IOPLT  (0x006E)
#define MMIO_I2O0_IIDC   (0x0070)
#define MMIO_I2O0_ICTL   (0x0074)
#define MMIO_I2O0_IFCPP  (0x0078)
#define MMIO_I2O0_MFAC0  (0x0080)
#define MMIO_I2O0_MFAC1  (0x0082)
#define MMIO_I2O0_MFAC2  (0x0084)
#define MMIO_I2O0_MFAC3  (0x0086)
#define MMIO_I2O0_MFAC4  (0x0088)
#define MMIO_I2O0_MFAC5  (0x008A)
#define MMIO_I2O0_MFAC6  (0x008C)
#define MMIO_I2O0_MFAC7  (0x008E)
#define MMIO_I2O0_IFCFH  (0x0090)
#define MMIO_I2O0_IFCHT  (0x0092)
#define MMIO_I2O0_IIFMC  (0x0098)
#define MMIO_I2O0_IODB   (0x009C)
#define MMIO_I2O0_IODBC  (0x00A0)
#define MMIO_I2O0_IFBAL  (0x00A4)
#define MMIO_I2O0_IFBAH  (0x00A8)
#define MMIO_I2O0_IFSIZ  (0x00AC)
#define MMIO_I2O0_ISPD0  (0x00B0)
#define MMIO_I2O0_ISPD1  (0x00B4)
#define MMIO_I2O0_ISPD2  (0x00B8)
#define MMIO_I2O0_ISPD3  (0x00BC)
#define MMIO_I2O0_IHIPL  (0x00C0)
#define MMIO_I2O0_IHIPH  (0x00C4)
#define MMIO_I2O0_IHOPL  (0x00C8)
#define MMIO_I2O0_IHOPH  (0x00CC)
#define MMIO_I2O0_IIIPL  (0x00D0)
#define MMIO_I2O0_IIIPH  (0x00D4)
#define MMIO_I2O0_IIOPL  (0x00D8)
#define MMIO_I2O0_IIOPH  (0x00DC)
#define MMIO_I2O0_IFCPL  (0x00E0)
#define MMIO_I2O0_IFCPH  (0x00E4)
#define MMIO_I2O0_IOPT   (0x00F0)

#define MMIO_DMA0_CPFPL  (0x0100)
#define MMIO_DMA0_CPFPH  (0x0104)
#define MMIO_DMA0_CSFPL  (0x0108)
#define MMIO_DMA0_CSFPH  (0x010C)
#define MMIO_DMA0_DSTS   (0x0110)
#define MMIO_DMA0_CFG    (0x0114)
#define MMIO_DMA0_CPFHP  (0x0120)
#define MMIO_DMA0_CPFTP  (0x0122)
#define MMIO_DMA0_CSFHP  (0x0124)
#define MMIO_DMA0_CSFTP  (0x0126)
#define MMIO_DMA0_ACPL   (0x0130)
#define MMIO_DMA0_ACPH   (0x0134)
#define MMIO_DMA0_S1BPL  (0x0138)
#define MMIO_DMA0_S1BPH  (0x013C)
#define MMIO_DMA0_S2BPL  (0x0140)
#define MMIO_DMA0_S2BPH  (0x0144)
#define MMIO_DMA0_S3BPL  (0x0148)
#define MMIO_DMA0_S3BPH  (0x014C)
#define MMIO_DMA0_EARL   (0x0160)
#define MMIO_DMA0_EARH   (0x0164)
#define MMIO_DMA0_SEAT   (0x0170)
#define MMIO_DMA0_SEAD   (0x0174)
#define MMIO_DMA0_OP     (0x0178)
#define MMIO_DMA0_FSIZ   (0x017C)

#define MMIO_DMA1_CPFPL  (0x0200)
#define MMIO_DMA1_CPFPH  (0x0204)
#define MMIO_DMA1_CSFPL  (0x0208)
#define MMIO_DMA1_CSFPH  (0x020C)
#define MMIO_DMA1_DSTS   (0x0210)
#define MMIO_DMA1_CFG    (0x0214)
#define MMIO_DMA1_CPFHP  (0x0220)
#define MMIO_DMA1_CPFTP  (0x0222)
#define MMIO_DMA1_CSFHP  (0x0224)
#define MMIO_DMA1_CSFTP  (0x0226)
#define MMIO_DMA1_ACPL   (0x0230)
#define MMIO_DMA1_ACPH   (0x0234)
#define MMIO_DMA1_S1BPL  (0x0238)
#define MMIO_DMA1_S1BPH  (0x023C)
#define MMIO_DMA1_S2BPL  (0x0240)
#define MMIO_DMA1_S2BPH  (0x0244)
#define MMIO_DMA1_S3BPL  (0x0248)
#define MMIO_DMA1_S3BPH  (0x024C)
#define MMIO_DMA1_EARL   (0x0260)
#define MMIO_DMA1_EARH   (0x0264)
#define MMIO_DMA1_SEAT   (0x0270)
#define MMIO_DMA1_SEAD   (0x0274)
#define MMIO_DMA1_OP     (0x0278)
#define MMIO_DMA1_FSIZ   (0x027C)

/*
 * Register Structures
 */

struct i2o_regs {
        u32 * ists;
        u32 * iseat;
        u32 * isead;
        u32 * idbel;
        u32 * ihis;
        u32 * ihim;
        u32 * ihiq;
        u32 * ihoq;
        u32 * iopis;
        u32 * iopim;
        u32 * iopiq;
        u32 * iopoq;
        u32 * iiflht; // iiflh && iiflt
        u32 * iiplht; // iiplh && iiplt
        u32 * ioflht; // ioflh && ioflt
        u32 * ioplht; // ioplh && iiioplt
        u32 * iidc;
        u32 * ictl;
        u32 * ifcpp;
        u32 * mfac01; // mfac0 mfac1
        u32 * mfac23; // mfac2 mfac3
        u32 * mfac45; // mfac4 mfac5
        u32 * mfac67; // mfac6 mfac7
        u32 * ifcfht; // ifcfh && ifcht
        u32 * iifmc;
        u32 * iodb;
        u32 * iodbc;
        u32 * ifbal;
        u32 * ifbah;
        u32 * ifsiz;
        u32 * ispd0;
        u32 * ispd1;
        u32 * ispd2;
        u32 * ispd3;
        u32 * ihipl;
        u32 * ihiph;
        u32 * ihopl;
        u32 * ihoph;
        u32 * iiipl;
        u32 * iiiph;
        u32 * iiopl;
        u32 * iioph;
        u32 * ifcpl;
        u32 * ifcph;
        u32 * iopt;
};

struct hsdma_regs {
        u32 * cpfpl;
        u32 * cpfph;
        u32 * csfpl;
        u32 * csfph;
        u32 * dsts;
        u32 * cfg;
        u32 * cpfhtp; // cpfhp && cpftp
        u32 * csfhtp; // csfhp && csftp
        u32 * acpl;
        u32 * acph;
        u32 * s1bpl;
        u32 * s1bph;
        u32 * s2bpl;
        u32 * s2bph;
        u32 * s3bpl;
        u32 * s3bph;
        u32 * earl;
        u32 * earh;
        u32 * seat;
        u32 * sead;
        u32 * op;
        u32 * fsiz;
};

/*
 * Device Structure
 */

#define PPC4xx_I2O0_FIFO_SIZE 4096 /* (256*8*2) */

struct i2o_data {
	struct i2o_regs reg;
	struct fifo {
		void      *virt;
		dma_addr_t phys;
	} fifo;
};

struct hsdma_desc_cdb {
	struct list_head  node;
	struct hsdma_cdb *cdb_virt;
	dma_addr_t        cdb_phys;
	int               cdb_status; /* = DMAx.dsts | DMAx.csfpl.cstat */
        void            (*cback) (int status, void * dev, u32 id, u64 crc);
	void             *cback_dev;
	u32              cback_id;
};

struct hsdma_data {
	struct hsdma_regs reg;
	struct cdb {
		struct list_head free;
		struct list_head used;
	} cdb;
};

struct hsdma_dev {
        u32                mmio;
	struct i2o_data    i2o;
	struct hsdma_data  dma[2];
};

typedef struct hsdma_dev hsdma_dev_t;

/*
 * CDB Structure
 */

#define HSDMA_CSTAT_MASK         0x00000003
#define HSDMA_ADDR_MASK          0xFFFFFFF0
#define HSDMA_DONT_INTERRUPT     0x00000008

struct hsdma_cdb
{
  u32 opcode;
  u32 sg1_hi;
  u32 sg1_lo;
  u32 sg_cnt;
  u32 sg2_hi;
  u32 sg2_lo;
  u32 sg3_hi;
  u32 sg3_lo;
};

typedef struct hsdma_cdb hsdma_cdb_t;

/*
 * Helper Union to convert 64bit to hi/lo 32bit
 */

union phys_addr {
	struct {
		u32 hi;
		u32 lo;
	} u32;
	phys_addr_t u64;
};

typedef union phys_addr phys_addr_u;

/*
 * Private Functions Declarations
 */

extern int request_dma(u32 dmanr, const char *device_id);

extern void free_dma(u32 dmanr);

static int ppc4xx_hsdma_setup(void);

static void ppc4xx_hsdma_cleanup(void);

static int ppc4xx_hsdma_ioremap(void);

static irqreturn_t ppc4xx_hsdma_irq_full(int irq, void *dev_id);

static irqreturn_t ppc4xx_hsdma_irq_srvc(int irq, void *dev_id);

static irqreturn_t ppc4xx_hsdma_irq_err(int irq, void *dev_id);

static void ppc4xx_hsdma_create_cdb(hsdma_cdb_t *cdb,
                                    u32 opcode,
                                    u32 attr,
                                    u32 sg_cnt,
                                    phys_addr_u sg1, 
                                    phys_addr_u sg2,
                                    phys_addr_u sg3);

/*
 * Private Variables
 */

static hsdma_dev_t hsdma_dev;
static u32         hsdma_initialized = 0;
static u32         hsdma_requested   = 0;

/* Virtual Interrupt Number */
static int irq_full = NO_IRQ;
static int irq_srvc = NO_IRQ;
static int irq_hsdma_err = NO_IRQ;
static int irq_i2o_err = NO_IRQ;


DECLARE_MUTEX(hsdma_lock);

/**
 * ppc4xx_hsdma_setup - Initializes the HSDMA interface
 *
 * Initializes the HSMDA interface. Returns %0 on success or
 * %-ENOMEM on failure.
 */
static int ppc4xx_hsdma_setup(void)
{
	int i;
	struct hsdma_cdb      *cdb;
	struct hsdma_desc_cdb *desc;
#if defined(CONFIG_440SP) || defined(CONFIG_440SPE)
	int num_dma = 2;
#else
	int num_dma = 1;
#endif
	hsdma_dev_t * dev = &hsdma_dev;

	dev->mmio = 0;
	dev->i2o.fifo.virt  = NULL;
	dev->i2o.fifo.phys  = 0;

	INIT_LIST_HEAD(&dev->dma[0].cdb.free);
	INIT_LIST_HEAD(&dev->dma[0].cdb.used);

	INIT_LIST_HEAD(&dev->dma[1].cdb.free);
	INIT_LIST_HEAD(&dev->dma[1].cdb.used);

	/*
	 * Reset HSDMA (I2O/DMA) Core
	 */
#if 0
	SDR_WRITE(DCRN_SDR_SRST0,
		  (SDR_READ(DCRN_SDR_SRST0) | 0x00010000));
	mdelay(100);
	SDR_WRITE(DCRN_SDR_SRST0,
		  (SDR_READ(DCRN_SDR_SRST0) & ~0x00010000));
#endif

	/*
	 * Setup Memory Queue for HB and LL access
	 */
#if 0
	mtdcr(DCRN_MQ0_CF1L, 0x80000c00);
#endif

	/*
	 * Eanble access HSDMA (I2O/DMA) memory mapped registers
	 */

	mtdcr(DCRN_I2O0_IBAH,MMIO_I2O0_BASE_HI);
  	mtdcr(DCRN_I2O0_IBAL,MMIO_I2O0_BASE_LO);
  	mtdcr(DCRN_I2O0_IBAL,MMIO_I2O0_BASE_EN);

	/*
	 * ioremap memory mapped registers
	 */

	if ( ppc4xx_hsdma_ioremap() ) {
		ppc4xx_hsdma_cleanup();
		return -ENOMEM;
	}

	/*
	 * Allocate I2O FIFO memory
	 */

	dev->i2o.fifo.virt = kmalloc((PPC4xx_I2O0_FIFO_SIZE*num_dma),
				     GFP_KERNEL);
	if ( dev->i2o.fifo.virt == 0 ) {
                ERR("could not allocate I2O FIFO memory!");
		ppc4xx_hsdma_cleanup();
		return -ENOMEM;
	}

	dev->i2o.fifo.phys = virt_to_phys(dev->i2o.fifo.virt);

	memset(dev->i2o.fifo.virt,0x0,(PPC4xx_I2O0_FIFO_SIZE*num_dma));
	dma_cache_wback_inv((unsigned long)dev->i2o.fifo.virt,
	                    (PPC4xx_I2O0_FIFO_SIZE*num_dma));

	out_le32(dev->i2o.reg.ifbah,0x00000000);
	out_le32(dev->i2o.reg.ifbal,dev->i2o.fifo.phys);
	out_le32(dev->i2o.reg.ifsiz,0);

	/*
	 * Setup HSDMA0
	 */

#if defined(CONFIG_440SP) || defined(CONFIG_440SPE)
	for ( i = 0; i < CONFIG_PPC4xx_HSDMA_FIFO_SIZE; i++ ) {
		cdb = (struct hsdma_cdb*)kmalloc(sizeof(struct hsdma_cdb),
					GFP_KERNEL);
		if ( cdb == NULL ) {
                	ERR("could not allocate cdb[%d] memory",i);
			ppc4xx_hsdma_cleanup();
			return -ENOMEM;
		}

		desc = kzalloc(sizeof(struct hsdma_desc_cdb*), GFP_KERNEL);
		if ( desc == NULL ) {
                	ERR("could not allocate desc[%d] memory",i);
			ppc4xx_hsdma_cleanup();
			return -ENOMEM;
		}

		desc->cdb_virt = cdb;
		desc->cdb_phys = virt_to_phys( desc->cdb_virt);

		list_add_tail(&desc->node, &dev->dma[0].cdb.free);
	}

	out_le32(dev->dma[0].reg.cfg,0x00600000);
	
	out_le32(dev->dma[0].reg.fsiz,
		(0x00001000 | (CONFIG_PPC4xx_HSDMA_FIFO_SIZE-1)));
#endif

	/*
	 * Setup HSDMA1
	 */

	for ( i = 0; i < CONFIG_PPC4xx_HSDMA_FIFO_SIZE; i++ ) {
		cdb = (struct hsdma_cdb*)kmalloc(sizeof(struct hsdma_cdb),
					GFP_KERNEL);
		if ( cdb == NULL ) {
                	ERR("could not allocate cdb[%d] memory",i);
			ppc4xx_hsdma_cleanup();
			return -ENOMEM;
		}

		desc = kzalloc(sizeof(struct hsdma_desc_cdb*), GFP_KERNEL);
		if ( desc == NULL ) {
                	ERR("could not allocate desc[%d] memory",i);
			ppc4xx_hsdma_cleanup();
			return -ENOMEM;
		}

		desc->cdb_virt = cdb;
		desc->cdb_phys = virt_to_phys( desc->cdb_virt);

		list_add_tail(&desc->node, &dev->dma[1].cdb.free);
	}

#if 1
	out_le32(dev->dma[1].reg.cfg,0x80600000);
#else
	out_le32(dev->dma[1].reg.cfg,0x00600000);
#endif

	out_le32(dev->dma[1].reg.fsiz,
	        (0x00001000|(CONFIG_PPC4xx_HSDMA_FIFO_SIZE-1)));

	/*
	 * request interrupts
	 */

#if 0
	out_le32(dev->i2o.reg.iopim,0x0000EE07);
#else
	out_le32(dev->i2o.reg.iopim,0x00000000);
#endif

#if defined(CONFIG_440SP) || defined(CONFIG_440SPE)
	if ( request_irq(HSDMA0_IRQ_FULL,ppc4xx_hsdma_irq_full,
	                 SA_INTERRUPT,"HSDMA0 FULL",NULL) ||
	     request_irq(HSDMA0_IRQ_SRVC,ppc4xx_hsdma_irq_srvc,
	                 SA_INTERRUPT,"HSDMA0 SRVC",NULL) ||
	     request_irq(HSDMA1_IRQ_FULL,ppc4xx_hsdma_irq_full,
	                 SA_INTERRUPT,"HSDMA1 FULL",NULL) ||
	     request_irq(HSDMA1_IRQ_SRVC,ppc4xx_hsdma_irq_srvc,
	                 SA_INTERRUPT,"HSDMA1 SRVC",NULL) ||
	     request_irq(HSDMAx_IRQ_DMA_ERR,ppc4xx_hsdma_irq_err,
	                 SA_INTERRUPT,"HSDMA ERR",NULL) ||
	     request_irq(HSDMAx_IRQ_I2O_ERR,ppc4xx_hsdma_irq_err,
	                 SA_INTERRUPT,"HSDMA IERR",NULL) ) {
                ERR("could not setup interrupt support!");
		ppc4xx_hsdma_cleanup();
		return -ENOMEM;
	}
#else
	LOG("Registering interrupt...");
	if ( request_irq(irq_full,ppc4xx_hsdma_irq_full,
	                 SA_INTERRUPT,"HSDMA1 FULL",NULL) ||
	     request_irq(irq_srvc,ppc4xx_hsdma_irq_srvc,
	                 SA_INTERRUPT,"HSDMA1 SRVC",NULL) ||
	     request_irq(irq_hsdma_err,ppc4xx_hsdma_irq_err,
	                 SA_INTERRUPT,"HSDMA ERR",NULL) ||
	     request_irq(irq_i2o_err,ppc4xx_hsdma_irq_err,
	                 SA_INTERRUPT,"HSDMA IERR",NULL) ) {
		LOG("failed.");
                ERR("could not setup interrupt support!");
		ppc4xx_hsdma_cleanup();
		return -ENOMEM;
	}
	LOG("succeeded.");
#endif

	hsdma_initialized = 1;
        return HSDMA_SUCCESS;
}

/**
 * ppc4xx_hsdma_reset - reset HSDMA interface
 *
 * Reset the HSMDA interface.
 */
int ppc4xx_hsdma_reset(void)
{
	int n;
	struct hsdma_desc_cdb *desc, *tmp;
#if defined(CONFIG_440SP) || defined(CONFIG_440SPE)
	int num_dma = 2;
#else
	int num_dma = 1;
#endif
	hsdma_dev_t * dev = &hsdma_dev;

	/*
	 * Reset HSDMA (I2O/DMA) Core
	 */
	SDR_WRITE(DCRN_SDR_SRST0,
		  (SDR_READ(DCRN_SDR_SRST0) | 0x00010000));
	mdelay(100);
	SDR_WRITE(DCRN_SDR_SRST0,
		  (SDR_READ(DCRN_SDR_SRST0) & ~0x00010000));

	/*
	 * Eanble access HSDMA (I2O/DMA) memory mapped registers
	 */

	mtdcr(DCRN_I2O0_IBAH,MMIO_I2O0_BASE_HI);
  	mtdcr(DCRN_I2O0_IBAL,MMIO_I2O0_BASE_LO);
  	mtdcr(DCRN_I2O0_IBAL,MMIO_I2O0_BASE_EN);

	memset(dev->i2o.fifo.virt,0x0,(PPC4xx_I2O0_FIFO_SIZE*num_dma));
	dma_cache_wback_inv((unsigned long)dev->i2o.fifo.virt,
	                    (PPC4xx_I2O0_FIFO_SIZE*num_dma));

	out_le32(dev->i2o.reg.ifbah,0x00000000);
	out_le32(dev->i2o.reg.ifbal,dev->i2o.fifo.phys);
	out_le32(dev->i2o.reg.ifsiz,0);

	/*
	 * Setup HSDMA0
	 */

#if defined(CONFIG_440SP) || defined(CONFIG_440SPE)

	out_le32(dev->dma[0].reg.cfg,0x00600000);
	out_le32(dev->dma[0].reg.fsiz,
		(0x00001000 | (CONFIG_PPC4xx_HSDMA_FIFO_SIZE-1)));
#endif

	/*
	 * Setup HSDMA1
	 */

#if 1
	out_le32(dev->dma[1].reg.cfg,0x80600000);
#else
	out_le32(dev->dma[1].reg.cfg,0x00600000);
#endif
	out_le32(dev->dma[1].reg.fsiz,
	        (0x00001000 | (CONFIG_PPC4xx_HSDMA_FIFO_SIZE-1)));

	/*
	 * request interrupts
	 */

	out_le32(dev->i2o.reg.iopim,0x00000000);

	/*
	 * Clear used cdb list
	 */

	for ( n = 0; n < num_dma; n++ ) {
		list_for_each_entry_safe(desc,tmp,
					 &hsdma_dev.dma[n].cdb.used,node) {
			list_del(&desc->node);
			list_add_tail(&desc->node,&hsdma_dev.dma[n].cdb.free);
		}
	}

	hsdma_initialized = 1;
        return HSDMA_SUCCESS;
}


/**
 * ppc4xx_hsdma_cleanup - Releases the HSDMA interface
 *
 * Releases the HSMDA interface.
 */
static void ppc4xx_hsdma_cleanup(void)
{
	int n;
	struct hsdma_desc_cdb * desc, *tmp;
	hsdma_dev_t * dev = &hsdma_dev;

        LOG("cleaning up");

	for ( n = 0; n < 2; n++ ) {
		list_for_each_entry_safe(desc,tmp,&dev->dma[n].cdb.free,node) {
			list_del(&desc->node);
			kfree(desc->cdb_virt);
			kfree(desc);
		}

		list_for_each_entry_safe(desc,tmp,&dev->dma[n].cdb.used,node) {
			list_del(&desc->node);
			kfree(desc->cdb_virt);
			kfree(desc);
		}
	}

	if ( dev->i2o.fifo.virt) {
		kfree(dev->i2o.fifo.virt);
	}

	if ( dev->mmio ) {
		iounmap((unsigned*)dev->mmio);
	}
}

/**
 * ppc4xx_hsdma_request_dma - Request HSDMA interface
 * @dmanr: DMA channel number
 * @dev: pointer to private data area
 * @cback: Callback to execute when transfer complete
 *
 * Request HSDMA interface. Returns %0 on success else 
 * error.
 */
int ppc4xx_hsdma_request_dma(u32 dmanr)
{
	int n = dmanr - PPC4xx_HSDMA_OFFSET;
	int err = HSDMA_SUCCESS;

	down(&hsdma_lock);


	if ( hsdma_initialized == 0 ) {
		if ( (err = ppc4xx_hsdma_setup()) ) {
			goto out;
		}
	}

	if ( hsdma_requested == 0 ) {
        	if ( (err = request_dma(dmanr,"HSDMA")) ) {
                	ERR("could not get HSDMA%d handle",n);
			goto out;
        	}
	}

	hsdma_requested++;
out:
	up(&hsdma_lock);

        return err;
}

/**
 * ppc4xx_hsdma_free_dma - Free HSDMA interface
 * @dmanr: DMA channel number
 *
 * Free HSDMA interface.
 */
void ppc4xx_hsdma_free_dma(u32 dmanr)
{
	down(&hsdma_lock);

	if ( hsdma_requested > 0 ) {
		hsdma_requested--;
	}

	if ( hsdma_requested == 0 ) {
        	free_dma(dmanr);
	}

	up(&hsdma_lock);
}

/**
 * ppc4xx_hsdma_ioremap - IO remap device
 *
 * IO remap device.
 */
static int ppc4xx_hsdma_ioremap(void)
{
	hsdma_dev_t * dev = &hsdma_dev;

	dev->mmio = (u32)ioremap(MMIO_I2O0_BASE,4096);
	if ( dev->mmio == 0 ) {
		ERR("Could not ioremap registers");
		return -ENOMEM;
	}

	dev->i2o.reg.ists  = (u32*)(dev->mmio + MMIO_I2O0_ISTS);
	dev->i2o.reg.iseat = (u32*)(dev->mmio + MMIO_I2O0_ISEAT);
	dev->i2o.reg.isead = (u32*)(dev->mmio + MMIO_I2O0_ISEAD);
	dev->i2o.reg.idbel = (u32*)(dev->mmio + MMIO_I2O0_IDBEL);
	dev->i2o.reg.ihis  = (u32*)(dev->mmio + MMIO_I2O0_IHIS);
	dev->i2o.reg.ihim  = (u32*)(dev->mmio + MMIO_I2O0_IHIM);
	dev->i2o.reg.ihiq  = (u32*)(dev->mmio + MMIO_I2O0_IHIQ);
	dev->i2o.reg.ihoq  = (u32*)(dev->mmio + MMIO_I2O0_IHOQ);
	dev->i2o.reg.iopis = (u32*)(dev->mmio + MMIO_I2O0_IOPIS);
	dev->i2o.reg.iopim = (u32*)(dev->mmio + MMIO_I2O0_IOPIM);
	dev->i2o.reg.iopiq = (u32*)(dev->mmio + MMIO_I2O0_IOPIQ);
	dev->i2o.reg.iopoq = (u32*)(dev->mmio + MMIO_I2O0_IOPOQ);
	dev->i2o.reg.iiflht= (u32*)(dev->mmio + MMIO_I2O0_IIFLH);
	dev->i2o.reg.iiplht= (u32*)(dev->mmio + MMIO_I2O0_IIPLH);
	dev->i2o.reg.ioflht= (u32*)(dev->mmio + MMIO_I2O0_IOFLH);
	dev->i2o.reg.ioplht= (u32*)(dev->mmio + MMIO_I2O0_IOPLH);
	dev->i2o.reg.iidc  = (u32*)(dev->mmio + MMIO_I2O0_IIDC);
	dev->i2o.reg.ictl  = (u32*)(dev->mmio + MMIO_I2O0_ICTL);
	dev->i2o.reg.ifcpp = (u32*)(dev->mmio + MMIO_I2O0_IFCPP);
	dev->i2o.reg.mfac01= (u32*)(dev->mmio + MMIO_I2O0_MFAC0);
	dev->i2o.reg.mfac23= (u32*)(dev->mmio + MMIO_I2O0_MFAC2);
	dev->i2o.reg.mfac45= (u32*)(dev->mmio + MMIO_I2O0_MFAC4);
	dev->i2o.reg.mfac67= (u32*)(dev->mmio + MMIO_I2O0_MFAC6);
	dev->i2o.reg.ifcfht= (u32*)(dev->mmio + MMIO_I2O0_IFCFH);
	dev->i2o.reg.iifmc = (u32*)(dev->mmio + MMIO_I2O0_IIFMC);
	dev->i2o.reg.iodb  = (u32*)(dev->mmio + MMIO_I2O0_IODB);
	dev->i2o.reg.iodbc = (u32*)(dev->mmio + MMIO_I2O0_IODBC);
	dev->i2o.reg.ifbal = (u32*)(dev->mmio + MMIO_I2O0_IFBAL);
	dev->i2o.reg.ifbah = (u32*)(dev->mmio + MMIO_I2O0_IFBAH);
	dev->i2o.reg.ifsiz = (u32*)(dev->mmio + MMIO_I2O0_IFSIZ);
	dev->i2o.reg.ispd0 = (u32*)(dev->mmio + MMIO_I2O0_ISPD0);
	dev->i2o.reg.ispd1 = (u32*)(dev->mmio + MMIO_I2O0_ISPD1);
	dev->i2o.reg.ispd2 = (u32*)(dev->mmio + MMIO_I2O0_ISPD2);
	dev->i2o.reg.ispd3 = (u32*)(dev->mmio + MMIO_I2O0_ISPD3);
	dev->i2o.reg.ihipl = (u32*)(dev->mmio + MMIO_I2O0_IHIPL);
	dev->i2o.reg.ihiph = (u32*)(dev->mmio + MMIO_I2O0_IHIPH);
	dev->i2o.reg.ihopl = (u32*)(dev->mmio + MMIO_I2O0_IHOPL);
	dev->i2o.reg.ihoph = (u32*)(dev->mmio + MMIO_I2O0_IHOPH);
	dev->i2o.reg.iiipl = (u32*)(dev->mmio + MMIO_I2O0_IIIPL);
	dev->i2o.reg.iiiph = (u32*)(dev->mmio + MMIO_I2O0_IIIPH);
	dev->i2o.reg.iiopl = (u32*)(dev->mmio + MMIO_I2O0_IIOPL);
	dev->i2o.reg.iioph = (u32*)(dev->mmio + MMIO_I2O0_IIOPH);
	dev->i2o.reg.ifcpl = (u32*)(dev->mmio + MMIO_I2O0_IFCPL);
	dev->i2o.reg.ifcph = (u32*)(dev->mmio + MMIO_I2O0_IFCPH);
	dev->i2o.reg.iopt  = (u32*)(dev->mmio + MMIO_I2O0_IOPT);

#if defined(CONFIG_440SP) || defined(CONFIG_440SPE)
	dev->dma[0].reg.cpfpl = (u32*)(dev->mmio + MMIO_DMA0_CPFPL);
	dev->dma[0].reg.cpfph = (u32*)(dev->mmio + MMIO_DMA0_CPFPH);
	dev->dma[0].reg.csfpl = (u32*)(dev->mmio + MMIO_DMA0_CSFPL);
	dev->dma[0].reg.csfph = (u32*)(dev->mmio + MMIO_DMA0_CSFPH);
	dev->dma[0].reg.dsts  = (u32*)(dev->mmio + MMIO_DMA0_DSTS);
	dev->dma[0].reg.cfg   = (u32*)(dev->mmio + MMIO_DMA0_CFG);
	dev->dma[0].reg.cpfhtp= (u32*)(dev->mmio + MMIO_DMA0_CPFHP);
	dev->dma[0].reg.csfhtp= (u32*)(dev->mmio + MMIO_DMA0_CSFHP);
	dev->dma[0].reg.acpl  = (u32*)(dev->mmio + MMIO_DMA0_ACPL);
	dev->dma[0].reg.acph  = (u32*)(dev->mmio + MMIO_DMA0_ACPH);
	dev->dma[0].reg.s1bpl = (u32*)(dev->mmio + MMIO_DMA0_S1BPL);
	dev->dma[0].reg.s1bph = (u32*)(dev->mmio + MMIO_DMA0_S1BPH);
	dev->dma[0].reg.s2bpl = (u32*)(dev->mmio + MMIO_DMA0_S2BPL);
	dev->dma[0].reg.s2bph = (u32*)(dev->mmio + MMIO_DMA0_S2BPH);
	dev->dma[0].reg.s3bpl = (u32*)(dev->mmio + MMIO_DMA0_S3BPL);
	dev->dma[0].reg.s3bph = (u32*)(dev->mmio + MMIO_DMA0_S3BPH);
	dev->dma[0].reg.earl  = (u32*)(dev->mmio + MMIO_DMA0_EARL);
	dev->dma[0].reg.earh  = (u32*)(dev->mmio + MMIO_DMA0_EARH);
	dev->dma[0].reg.seat  = (u32*)(dev->mmio + MMIO_DMA0_SEAT);
	dev->dma[0].reg.sead  = (u32*)(dev->mmio + MMIO_DMA0_SEAD);
	dev->dma[0].reg.op    = (u32*)(dev->mmio + MMIO_DMA0_OP);
	dev->dma[0].reg.fsiz  = (u32*)(dev->mmio + MMIO_DMA0_FSIZ);
#endif

	dev->dma[1].reg.cpfpl = (u32*)(dev->mmio + MMIO_DMA1_CPFPL);
	dev->dma[1].reg.cpfph = (u32*)(dev->mmio + MMIO_DMA1_CPFPH);
	dev->dma[1].reg.csfpl = (u32*)(dev->mmio + MMIO_DMA1_CSFPL);
	dev->dma[1].reg.csfph = (u32*)(dev->mmio + MMIO_DMA1_CSFPH);
	dev->dma[1].reg.dsts  = (u32*)(dev->mmio + MMIO_DMA1_DSTS);
	dev->dma[1].reg.cfg   = (u32*)(dev->mmio + MMIO_DMA1_CFG);
	dev->dma[1].reg.cpfhtp= (u32*)(dev->mmio + MMIO_DMA1_CPFHP);
	dev->dma[1].reg.csfhtp= (u32*)(dev->mmio + MMIO_DMA1_CSFHP);
	dev->dma[1].reg.acpl  = (u32*)(dev->mmio + MMIO_DMA1_ACPL);
	dev->dma[1].reg.acph  = (u32*)(dev->mmio + MMIO_DMA1_ACPH);
	dev->dma[1].reg.s1bpl = (u32*)(dev->mmio + MMIO_DMA1_S1BPL);
	dev->dma[1].reg.s1bph = (u32*)(dev->mmio + MMIO_DMA1_S1BPH);
	dev->dma[1].reg.s2bpl = (u32*)(dev->mmio + MMIO_DMA1_S2BPL);
	dev->dma[1].reg.s2bph = (u32*)(dev->mmio + MMIO_DMA1_S2BPH);
	dev->dma[1].reg.s3bpl = (u32*)(dev->mmio + MMIO_DMA1_S3BPL);
	dev->dma[1].reg.s3bph = (u32*)(dev->mmio + MMIO_DMA1_S3BPH);
	dev->dma[1].reg.earl  = (u32*)(dev->mmio + MMIO_DMA1_EARL);
	dev->dma[1].reg.earh  = (u32*)(dev->mmio + MMIO_DMA1_EARH);
	dev->dma[1].reg.seat  = (u32*)(dev->mmio + MMIO_DMA1_SEAT);
	dev->dma[1].reg.sead  = (u32*)(dev->mmio + MMIO_DMA1_SEAD);
	dev->dma[1].reg.op    = (u32*)(dev->mmio + MMIO_DMA1_OP);
	dev->dma[1].reg.fsiz  = (u32*)(dev->mmio + MMIO_DMA1_FSIZ);

	return HSDMA_SUCCESS;
}

/**
 * ppc4xx_hsdma_irq_full - FIFO full interrupt handler
 * @irq: Linux interrupt number
 * @dev_instance: Pointer to interrupt-specific data
 *
 * Handles FIFO full interrupts.
 */
static irqreturn_t ppc4xx_hsdma_irq_full(int irq, void *dev_id)
{
	struct hsdma_desc_cdb * desc, *tmp;
	u32 phys;
	int n = (irq == HSDMA1_IRQ_FULL)? 1 : 0;

	int cdb_status = -1;

	ERR("%s: DMA%d",__FUNCTION__,n);

	phys=in_le32(hsdma_dev.dma[n].reg.csfpl)&HSDMA_ADDR_MASK;

	list_for_each_entry_safe(desc,tmp,&hsdma_dev.dma[n].cdb.used,node) {
		if ( desc->cdb_phys == phys ) {
			list_del(&desc->node);
			if ( desc->cback ) {
				desc->cback(cdb_status,
			                    desc->cback_dev,
			                    desc->cback_id,
			                    0);
			}
			list_add_tail(&desc->node,
			              &hsdma_dev.dma[n].cdb.free);
			break;
		}	
	}

        return IRQ_HANDLED;
}

/**
 * ppc4xx_hsdma_getCRC - return CRC for CRC request commands.
 *
 * Rreturn CRC for CRC request commands.
 */
static u64 ppc4xx_hsdma_getCRC(struct hsdma_desc_cdb * desc)
{
	int cmd = le32_to_cpu(desc->cdb_virt->opcode) & 0xFF000000;

	if ( cmd == HSDMA_MOVE_SG1_SG2_CRC || cmd == HSDMA_GENERATE_CRC ) {
		phys_addr_u c;
		c.u32.hi = le32_to_cpu(desc->cdb_virt->sg3_hi);
		c.u32.lo = le32_to_cpu(desc->cdb_virt->sg3_lo);
		return c.u64;
	}

	return 0;
}

/**
 * ppc4xx_hsdma_irq_srvc - Service interrupt handler
 * @irq: Linux interrupt number
 * @dev_instance: Pointer to interrupt-specific data
 *
 * Handles service interrupts. Calls callback event Handler
 */
static irqreturn_t ppc4xx_hsdma_irq_srvc(int irq, void *dev_id)
{
	struct hsdma_desc_cdb * desc, *tmp;
	u32 stat;
	u32 phys;
	int n = (irq == HSDMA1_IRQ_SRVC)? 1 : 0;

	int cdb_status; /* = DMAx.dsts | DMAx.csfpl.cstat */

	cdb_status = in_le32(hsdma_dev.dma[n].reg.dsts);

	while ((stat=in_le32(hsdma_dev.dma[n].reg.csfpl))) {
		phys = stat & HSDMA_ADDR_MASK;
		stat = stat & HSDMA_CSTAT_MASK;
		cdb_status |= stat;
		list_for_each_entry_safe(desc,tmp,
		                         &hsdma_dev.dma[n].cdb.used,node) {
			if ( desc->cdb_phys == phys ) {
				list_del(&desc->node);

				if ( desc->cback ) {
					desc->cback(cdb_status,
				                    desc->cback_dev,
				                    desc->cback_id,
				                    ppc4xx_hsdma_getCRC(desc));
				}

				list_add_tail(&desc->node,
				              &hsdma_dev.dma[n].cdb.free);
				break;
			}
		}
	}

        return IRQ_HANDLED;
}

/**
 * ppc4xx_hsdma_irq_err - Error event interrupt handler
 * @irq: Linux interrupt number
 * @dev_instance: Pointer to interrupt-specific data
 *
 * Handles error event interrupts. Calls callback event Handler
 */
static irqreturn_t ppc4xx_hsdma_irq_err(int irq, void *dev_id)
{
	struct hsdma_desc_cdb * desc, *tmp;
	u32 stat;
	int n = 0;
	u32 ists;
	u32 iopis;
	u32 dsts;

	int cdb_status; /* = DMAx.dsts | DMAx.csfpl.cstat */

	iopis = in_le32(hsdma_dev.i2o.reg.iopis);

	if (iopis & HSDMA_IOPIS_I200_ERR) {
		/* 
		 * TBD - What should be done here?
		 *       Or this should not happened?
		 *       For now lets clear the ists bits.
		 */
		ists = in_le32(hsdma_dev.i2o.reg.ists);
		ERR("%s: iopis = 0x%08x ists  = 0x%08x",
			__FUNCTION__,iopis,ists);
		out_le32(hsdma_dev.i2o.reg.ists,ists);
        	return IRQ_HANDLED;
	} else if (iopis & HSDMA_IOPIS_DMA0_ERR) {
		n = 0;
	} else if (iopis & HSDMA_IOPIS_DMA1_ERR) {
		n = 1;
	}

	dsts = in_le32(hsdma_dev.dma[n].reg.dsts);
	dsts = in_le32(hsdma_dev.dma[n].reg.dsts);
	ERR("%s: iopis = 0x%08x dsts = 0x%08x",__FUNCTION__,iopis,dsts);

	cdb_status = dsts;

	out_le32(hsdma_dev.dma[n].reg.dsts,HSDMA_CLEAR_DMA_HALT);

	/*
	 * Clear CS FIFO
	 */

	while ((stat=in_le32(hsdma_dev.dma[n].reg.csfpl)));

	/*
	 * Clear used cdb list
	 */

	list_for_each_entry_safe(desc,tmp,&hsdma_dev.dma[n].cdb.used,node) {
		list_del(&desc->node);
		if ( desc->cback ) {
			desc->cback(cdb_status,
			            desc->cback_dev,
			            desc->cback_id,
			            0);
		}
		list_add_tail(&desc->node,&hsdma_dev.dma[n].cdb.free);
	}

        return IRQ_HANDLED;
}

/**
 * ppc4xx_hsdma_create_cdb - Create HSDMA CDB
 * @opcode: Opcode
 * @attr: Attribute
 * @sg_count: SG count
 * @sg1: value based on CDB opcode
 * @sg2: value based on CDB opcode
 * @sg3: value based on CDB opcode
 *
 * Creates HSDMA CDB
 */
static void ppc4xx_hsdma_create_cdb(hsdma_cdb_t *cdb,
                                    u32 opcode,
                                    u32 attr,
                                    u32 sg_cnt,
                                    phys_addr_u sg1, 
                                    phys_addr_u sg2,
                                    phys_addr_u sg3)
{

	cdb->opcode = cpu_to_le32(opcode|attr);
	cdb->sg1_hi = cpu_to_le32(sg1.u32.hi);
	cdb->sg1_lo = cpu_to_le32(sg1.u32.lo);
	cdb->sg_cnt = cpu_to_le32(sg_cnt);
	cdb->sg2_hi = cpu_to_le32(sg2.u32.hi);
	cdb->sg2_lo = cpu_to_le32(sg2.u32.lo);
	cdb->sg3_hi = cpu_to_le32(sg3.u32.hi);
	cdb->sg3_lo = cpu_to_le32(sg3.u32.lo);

	DBG("cdb.sg3_lo = 0x%08X",le32_to_cpu(cdb->sg3_lo));
	DBG("cdb.sg3_hi = 0x%08X",le32_to_cpu(cdb->sg3_hi));
	DBG("cdb.sg2_lo = 0x%08X",le32_to_cpu(cdb->sg2_lo));
	DBG("cdb.sg2_hi = 0x%08X",le32_to_cpu(cdb->sg2_hi));
	DBG("cdb.sg_cnt = 0x%08X",le32_to_cpu(cdb->sg_cnt));
	DBG("cdb.sg1_lo = 0x%08X",le32_to_cpu(cdb->sg1_lo));
	DBG("cdb.sg1_hi = 0x%08X",le32_to_cpu(cdb->sg1_hi));
	DBG("cdb.opcode = 0x%08X",le32_to_cpu(cdb->opcode));
}

/**
 * ppc4xx_hsdma_noop - Does HSDMA NOOP
 * @dmanr: DMA channel number
 *
 * Adds the HSDMA command to HSDMA queue.
 */
void ppc4xx_hsdma_noop(u32 dmanr,
                      void (*cback)(int status, void *dev, u32 id, u64 crc),
                      void *dev, u32 id)
{
	ppc4xx_hsdma(dmanr,0,HSDMA_NO_OP,0,0,0,cback,dev,id);
}

/**
 * ppc4xx_hsdma_memcpy - Does HSDMA memory copy
 * @dmanr: DMA channel number
 * @size: Size of Transfer
 * @src: physical source address
 * @dst: physical destination address
 *
 * Adds the HSDMA command to HSDMA queue.
 */
void ppc4xx_hsdma_memcpy(u32 dmanr, size_t size,
                      phys_addr_t src, phys_addr_t dst,
                      void (*cback)(int status, void *dev, u32 id, u64 crc),
                      void *dev, u32 id)
{
	ppc4xx_hsdma(dmanr,size,HSDMA_MOVE_SG1_SG2,src,dst,0,cback,dev,id);
}

/**
 * ppc4xx_hsdma_memcpy2 - Does HSDMA memory copy to two locations
 * @dmanr: DMA channel number
 * @size: Size of Transfer
 * @src: physical source address
 * @dst1: physical destination address 1
 * @dst1: physical destination address 2
 *
 * Adds the HSDMA command to HSDMA queue.
 */
void ppc4xx_hsdma_memcpy2(u32 dmanr, size_t size,
                      phys_addr_t src, phys_addr_t dst1, phys_addr_t dst2,
                      void (*cback)(int status, void *dev, u32 id, u64 crc),
                      void *dev, u32 id)
{
	ppc4xx_hsdma(dmanr,size,HSDMA_MULTICAST,src,dst1,dst2,cback,dev,id);
}

/**
 * ppc4xx_hsdma_data_fill - Does HSDMA data fill
 * @dmanr: DMA channel number
 * @size: Size of Transfer
 * @src: physical source address
 * @pLo: Low 64bit data pattern
 * @pHi: High 64bit data pattern
 *
 * Adds the HSDMA command to HSDMA queue.
 */
void ppc4xx_hsdma_data_fill(u32 dmanr, size_t size,
                      phys_addr_t src, phys_addr_t pLo, phys_addr_t pHi,
                      void (*cback)(int status, void *dev, u32 id, u64 crc),
                      void *dev, u32 id)
{
	pLo = cpu_to_le64(pLo);
	pHi = cpu_to_le64(pHi);

	ppc4xx_hsdma(dmanr,size,HSDMA_DATA_FILL_128,pHi,src,pLo,cback,dev,id);
}

/**
 * ppc4xx_hsdma_data_check - Does HSDMA data check
 * @dmanr: DMA channel number
 * @size: Size of Transfer
 * @src: physical source address
 * @pLo: Low 64bit data pattern
 * @pHi: High 64bit data pattern
 *
 * Adds the HSDMA command to HSDMA queue.
 */
void ppc4xx_hsdma_data_check(u32 dmanr, size_t size,
                      phys_addr_t src, phys_addr_t pLo, phys_addr_t pHi,
                      void (*cback)(int status, void *dev, u32 id, u64 crc),
                      void *dev, u32 id)
{
	pLo = cpu_to_le64(pLo);
	pHi = cpu_to_le64(pHi);

	ppc4xx_hsdma(dmanr,size,HSDMA_DATA_CHECK_128,src,pHi,pLo,cback,dev,id);
}

/**
 * ppc4xx_hsdma_lsfr_reset - Changes the LFSR seed value
 * @dmanr: DMA channel number
 * @sLo: Low 64bit data pattern
 * @sHi: High 64bit data pattern
 *
 * Adds the HSDMA command to HSDMA queue.
 */
void ppc4xx_hsdma_lfsr_reset(u32 dmanr, phys_addr_t sLo, phys_addr_t sHi,
                      void (*cback)(int status, void *dev, u32 id, u64 crc),
                      void *dev, u32 id)
{
	sLo = cpu_to_le64(sLo);
	sHi = cpu_to_le64(sHi);

	ppc4xx_hsdma(dmanr,0,HSDMA_LFSR_RESET,sHi,sLo,0,cback,dev,id);
}

/**
 * ppc4xx_hsdma_lfsr_fill - Does HSDMA LFSR fill
 * @dmanr: DMA channel number
 * @size: Size of Transfer
 * @buff: physical source address
 *
 * Adds the HSDMA command to HSDMA queue.
 */
void ppc4xx_hsdma_lfsr_fill(u32 dmanr, size_t size, phys_addr_t buff,
                      void (*cback)(int status, void *dev, u32 id, u64 crc),
                      void *dev, u32 id)
{
	ppc4xx_hsdma(dmanr,size,HSDMA_LFSR_FILL,0,buff,0,cback,dev,id);
}

/**
 * ppc4xx_hsdma_lfsr_check - Does HSDMA LFSR check
 * @dmanr: DMA channel number
 * @size: Size of Transfer
 * @buff: physical source address
 *
 * Adds the HSDMA command to HSDMA queue.
 */
void ppc4xx_hsdma_lfsr_check(u32 dmanr, size_t size, phys_addr_t buff,
                      void (*cback)(int status, void *dev, u32 id, u64 crc),
                      void *dev, u32 id)
{
	ppc4xx_hsdma(dmanr,size,HSDMA_LFSR_CHECK,buff,0,0,cback,dev,id);
}

/**
 * ppc4xx_hsdma_lfsr_ifill - Does HSDMA LFSR invert fill
 * @dmanr: DMA channel number
 * @size: Size of Transfer
 * @buff: physical source address
 *
 * Adds the HSDMA command to HSDMA queue.
 */
void ppc4xx_hsdma_lfsr_ifill(u32 dmanr, size_t size, phys_addr_t buff,
                      void (*cback)(int status, void *dev, u32 id, u64 crc),
                      void *dev, u32 id)
{
	ppc4xx_hsdma(dmanr,size,HSDMA_LFSR_FILL_INVERT,0,buff,0,cback,dev,id);
}

/**
 * ppc4xx_hsdma_lfsr_icheck - Does HSDMA LFSR invert check
 * @dmanr: DMA channel number
 * @size: Size of Transfer
 * @buff: physical source address
 *
 * Adds the HSDMA command to HSDMA queue.
 */
void ppc4xx_hsdma_lfsr_icheck(u32 dmanr, size_t size, phys_addr_t buff,
                      void (*cback)(int status, void *dev, u32 id, u64 crc),
                      void *dev, u32 id)
{
	ppc4xx_hsdma(dmanr,size,HSDMA_LFSR_CHECK_INVERT,buff,0,0,cback,dev,id);
}

#if defined(CONFIG_460GT) || defined(CONFIG_460EX) || \
    defined(CONFIG_460EXR) || defined(CONFIG_431EXR) || \
    defined(CONFIG_AM81281)
/**
 * ppc4xx_hsdma_memcpy_crc - Does HSDMA memory copy with CRC
 * @dmanr: DMA channel number
 * @size: Size of Transfer
 * @src: physical source address
 * @dst: physical destination address
 *
 * Adds the HSDMA command to HSDMA queue.
 */
void ppc4xx_hsdma_memcpy_crc(u32 dmanr, size_t size,
                      phys_addr_t src, phys_addr_t dst,
                      void (*cback)(int status, void *dev, u32 id, u64 crc),
                      void *dev, u32 id)
{
	ppc4xx_hsdma(dmanr,size,HSDMA_MOVE_SG1_SG2_CRC,src,dst,
			0xFFFFFFFFFFFFFFFFull,cback,dev,id);
}
#endif

#if defined(CONFIG_460GT) || defined(CONFIG_460EX) || \
    defined(CONFIG_460EXR) || defined(CONFIG_431EXR) || \
    defined(CONFIG_AM81281)
/**
 * ppc4xx_hsdma_generate_crc - Generates CRC
 * @dmanr: DMA channel number
 * @size: Size of Transfer
 * @src: physical source address
 * @dst: physical destination address
 *
 * Adds the HSDMA command to HSDMA queue.
 */
void ppc4xx_hsdma_generate_crc(u32 dmanr, size_t size,
                      phys_addr_t src, phys_addr_t dst,
                      void (*cback)(int status, void *dev, u32 id, u64 crc),
                      void *dev, u32 id)
{
	ppc4xx_hsdma(dmanr,size,HSDMA_GENERATE_CRC,src,dst,
			0xFFFFFFFFFFFFFFFFull,cback,dev,id);
}
#endif

/**
 * ppc4xx_hsdma - Creates CDB and adds CDB to HSDMA queue
 * @dmanr: DMA channel number
 * @size: Size of Transfer
 * @opcode: opcode
 * @sg1: value based on CDB opcode
 * @sg2: value based on CDB opcode
 * @sg3: value based on CDB opcode
 *
 * Create CDB and adds CDB to HSDMA queue.
 */
void ppc4xx_hsdma(u32 dmanr, size_t size, u32 opcode,
                 phys_addr_t sg1, phys_addr_t sg2, phys_addr_t sg3,
                 void (*cback)(int status, void *dev, u32 id, u64 crc),
                 void *dev, u32 id)
{
	struct hsdma_desc_cdb *desc;
	int n = dmanr - PPC4xx_HSDMA_OFFSET;
	u32 cpfpl_data;
	u32 attr;
	phys_addr_t offset;
	phys_addr_t sg1_data;
	phys_addr_t sg2_data;
	phys_addr_t sg3_data;
	size_t i;
	size_t nCDBs = size/CONFIG_PPC4xx_HSDMA_MAX_CDB_SG_COUNT;
	size_t last_cdb_sg_cnt = size%CONFIG_PPC4xx_HSDMA_MAX_CDB_SG_COUNT;
	size_t sg_count = CONFIG_PPC4xx_HSDMA_MAX_CDB_SG_COUNT;

	if ( size == 0 ) {
		if ( opcode != HSDMA_LFSR_RESET ) {
			opcode = HSDMA_NO_OP;
		}
		nCDBs  = 0;
		last_cdb_sg_cnt = 0;
	} else if ( last_cdb_sg_cnt == 0 ) {
		nCDBs--;
		last_cdb_sg_cnt = CONFIG_PPC4xx_HSDMA_MAX_CDB_SG_COUNT;
	}

	DBG("opcode     = 0x%08X",opcode);
	DBG("nCDBs      = %d",(nCDBs+1));
	DBG("sg_cnt     = %d",CONFIG_PPC4xx_HSDMA_MAX_CDB_SG_COUNT);
	DBG("lst_sg_cnt = %d",last_cdb_sg_cnt);

	/* 
	 * create CDBs and send them to DMA Engine
	 */

	attr = 0;

	for ( i = 0; i <= nCDBs; i++ ) {
	        offset = i * CONFIG_PPC4xx_HSDMA_MAX_CDB_SG_COUNT;
		switch ( opcode ) {
		case HSDMA_NO_OP:
			sg1_data = sg1;
			sg2_data = sg2;
			sg3_data = sg3;
			break;
		case HSDMA_MOVE_SG1_SG2:
			sg1_data = sg1+offset;
			sg2_data = sg2+offset;
			sg3_data = sg3;
			break;
		case HSDMA_MULTICAST:
			sg1_data = sg1+offset;
			sg2_data = sg2+offset;
			sg3_data = sg3+offset;
			break;
		case HSDMA_DATA_CHECK_128:
			sg1_data = sg1+offset;
			sg2_data = sg2;
			sg3_data = sg3;
			break;
		case HSDMA_DATA_FILL_128:
			sg1_data = sg1;
			sg2_data = sg2+offset;
			sg3_data = sg3;
			break;
		case HSDMA_LFSR_RESET:
			sg1_data = sg1;
			sg2_data = sg2;
			sg3_data = sg3;
			break;
		case HSDMA_LFSR_CHECK:
			sg1_data = sg1+offset;
			sg2_data = sg2;
			sg3_data = sg3;
			break;
		case HSDMA_LFSR_FILL:
			sg1_data = sg1;
			sg2_data = sg2+offset;
			sg3_data = sg3;
			break;
		case HSDMA_LFSR_CHECK_INVERT:
			sg1_data = sg1+offset;
			sg2_data = sg2;
			sg3_data = sg3;
			break;
		case HSDMA_LFSR_FILL_INVERT:
			sg1_data = sg1;
			sg2_data = sg2+offset;
			sg3_data = sg3;
			break;
#if defined(CONFIG_460GT) || defined(CONFIG_460EX) || \
    defined(CONFIG_460EXR) || defined(CONFIG_431EXR) || \
    defined(CONFIG_AM81281)
		case HSDMA_MOVE_SG1_SG2_CRC:
			sg1_data = sg1+offset;
			sg2_data = sg2+offset;
			sg3_data = sg3;
			break;
		case HSDMA_GENERATE_CRC:
			sg1_data = sg1+offset;
			sg2_data = sg2;
			sg3_data = sg3;
			break;
#endif
		default:
			sg1_data = sg1;
			sg2_data = sg2;
			sg3_data = sg3;
		}

		if ( list_empty(&hsdma_dev.dma[n].cdb.free) ) {
			u32 phys;
			struct hsdma_desc_cdb *d,*t;
			WARNING("CDB FIFO to small");
			while((phys=in_le32(hsdma_dev.dma[n].reg.csfpl)) != 0){
				phys &=HSDMA_ADDR_MASK;
				list_for_each_entry_safe(d,t,
			                      &hsdma_dev.dma[n].cdb.used,node) {
					if ( d->cdb_phys == phys ) {
						list_del(&d->node);
						list_add_tail(&d->node,
					            &hsdma_dev.dma[n].cdb.free);
						break;
					}	
				}
			}
		}

		desc = container_of(hsdma_dev.dma[n].cdb.free.next,
		                    struct hsdma_desc_cdb,node);
		list_del(&desc->node);

	        if ( i==nCDBs) {
			sg_count = last_cdb_sg_cnt;
			desc->cback     = cback;
			desc->cback_dev = dev;
			desc->cback_id  = id;
		}

		ppc4xx_hsdma_create_cdb(desc->cdb_virt, opcode, attr, sg_count,
	                               (phys_addr_u)sg1_data,
	                               (phys_addr_u)sg2_data,
	                               (phys_addr_u)sg3_data);

		list_add_tail(&desc->node, &hsdma_dev.dma[n].cdb.used);

		dma_cache_wback_inv((unsigned long)desc->cdb_virt,
		                    sizeof(hsdma_cdb_t));

		cpfpl_data = (u32)desc->cdb_phys;

		if ( i < nCDBs ) {
			cpfpl_data |= HSDMA_DONT_INTERRUPT;
		}
		out_le32(hsdma_dev.dma[n].reg.cpfpl,cpfpl_data);

#if defined(CONFIG_460GT) || defined(CONFIG_460EX) || \
    defined(CONFIG_460EXR) || defined(CONFIG_431EXR) || \
    defined(CONFIG_AM81281)
		if ( opcode == HSDMA_MOVE_SG1_SG2_CRC ||
		     opcode == HSDMA_GENERATE_CRC ) {
			attr = HSDMA_CHAIN_CRC;
		}
#endif
	}
}
/**
 * ppc4xx_hsdma - Creates CDB and adds CDB to HSDMA queue
 * @dmanr: DMA channel number
 * @size: Size of Transfer
 * @opcode: opcode
 * @sg1: value based on CDB opcode
 * @sg2: value based on CDB opcode
 * @sg3: value based on CDB opcode
 *
 * Create CDB and adds CDB to HSDMA queue.
 */
int ppc4xx_hsdma_poll(u32 dmanr, size_t size, u32 opcode,
                 phys_addr_t sg1, phys_addr_t sg2, phys_addr_t sg3,
		 int timeout)
{
	struct hsdma_desc_cdb *desc, *tmp;
	int n = dmanr - PPC4xx_HSDMA_OFFSET;
	u32 cpfpl_data;
	u32 attr;
	phys_addr_t offset;
	phys_addr_t sg1_data;
	phys_addr_t sg2_data;
	phys_addr_t sg3_data;
	size_t i;
	size_t nCDBs = size/CONFIG_PPC4xx_HSDMA_MAX_CDB_SG_COUNT;
	size_t last_cdb_sg_cnt = size%CONFIG_PPC4xx_HSDMA_MAX_CDB_SG_COUNT;
	size_t sg_count = CONFIG_PPC4xx_HSDMA_MAX_CDB_SG_COUNT;
	u32 stat;
	int ret = 0;

	if ( size == 0 ) {
		if ( opcode != HSDMA_LFSR_RESET ) {
			opcode = HSDMA_NO_OP;
		}
		nCDBs  = 0;
		last_cdb_sg_cnt = 0;
	} else if ( last_cdb_sg_cnt == 0 ) {
		nCDBs--;
		last_cdb_sg_cnt = CONFIG_PPC4xx_HSDMA_MAX_CDB_SG_COUNT;
	}

	DBG("opcode     = 0x%08X",opcode);
	DBG("nCDBs      = %d",(nCDBs+1));
	DBG("sg_cnt     = %d",CONFIG_PPC4xx_HSDMA_MAX_CDB_SG_COUNT);
	DBG("lst_sg_cnt = %d",last_cdb_sg_cnt);

	/* 
	 * create CDBs and send them to DMA Engine
	 */

	attr = 0;

	for ( i = 0; i <= nCDBs; i++ ) {
	        offset = i * CONFIG_PPC4xx_HSDMA_MAX_CDB_SG_COUNT;
		switch ( opcode ) {
		case HSDMA_NO_OP:
			sg1_data = sg1;
			sg2_data = sg2;
			sg3_data = sg3;
			break;
		case HSDMA_MOVE_SG1_SG2:
			sg1_data = sg1+offset;
			sg2_data = sg2+offset;
			sg3_data = sg3;
			break;
		case HSDMA_MULTICAST:
			sg1_data = sg1+offset;
			sg2_data = sg2+offset;
			sg3_data = sg3+offset;
			break;
		case HSDMA_DATA_CHECK_128:
			sg1_data = sg1+offset;
			sg2_data = sg2;
			sg3_data = sg3;
			break;
		case HSDMA_DATA_FILL_128:
			sg1_data = sg1;
			sg2_data = sg2+offset;
			sg3_data = sg3;
			break;
		case HSDMA_LFSR_RESET:
			sg1_data = sg1;
			sg2_data = sg2;
			sg3_data = sg3;
			break;
		case HSDMA_LFSR_CHECK:
			sg1_data = sg1+offset;
			sg2_data = sg2;
			sg3_data = sg3;
			break;
		case HSDMA_LFSR_FILL:
			sg1_data = sg1;
			sg2_data = sg2+offset;
			sg3_data = sg3;
			break;
		case HSDMA_LFSR_CHECK_INVERT:
			sg1_data = sg1+offset;
			sg2_data = sg2;
			sg3_data = sg3;
			break;
		case HSDMA_LFSR_FILL_INVERT:
			sg1_data = sg1;
			sg2_data = sg2+offset;
			sg3_data = sg3;
			break;
#if defined(CONFIG_460GT) || defined(CONFIG_460EX) || \
    defined(CONFIG_460EXR) || defined(CONFIG_431EXR) || \
    defined(CONFIG_AM81281)
		case HSDMA_MOVE_SG1_SG2_CRC:
			sg1_data = sg1+offset;
			sg2_data = sg2+offset;
			sg3_data = sg3;
			break;
		case HSDMA_GENERATE_CRC:
			sg1_data = sg1+offset;
			sg2_data = sg2;
			sg3_data = sg3;
			break;
#endif
		default:
			sg1_data = sg1;
			sg2_data = sg2;
			sg3_data = sg3;
		}

		if ( list_empty(&hsdma_dev.dma[n].cdb.free) ) {
			u32 phys;
			struct hsdma_desc_cdb *d,*t;
			WARNING("CDB FIFO to small");
			while((phys=in_le32(hsdma_dev.dma[n].reg.csfpl)) != 0){
				phys &=HSDMA_ADDR_MASK;
				list_for_each_entry_safe(d,t,
			                      &hsdma_dev.dma[n].cdb.used,node) {
					if ( d->cdb_phys == phys ) {
						list_del(&d->node);
						list_add_tail(&d->node,
					            &hsdma_dev.dma[n].cdb.free);
						break;
					}	
				}
			}
		}

		desc = container_of(hsdma_dev.dma[n].cdb.free.next,
		                    struct hsdma_desc_cdb,node);
		list_del(&desc->node);

	        if (i==nCDBs) {
			sg_count = last_cdb_sg_cnt;
		}

		ppc4xx_hsdma_create_cdb(desc->cdb_virt, opcode, attr, sg_count,
	                               (phys_addr_u)sg1_data,
	                               (phys_addr_u)sg2_data,
	                               (phys_addr_u)sg3_data);

		list_add_tail(&desc->node, &hsdma_dev.dma[n].cdb.used);

		dma_cache_wback_inv((unsigned long)desc->cdb_virt,
		                    sizeof(hsdma_cdb_t));

		cpfpl_data = (u32)desc->cdb_phys;
		cpfpl_data |= HSDMA_DONT_INTERRUPT;

		out_le32(hsdma_dev.dma[n].reg.cpfpl,cpfpl_data);

#if defined(CONFIG_460GT) || defined(CONFIG_460EX) || \
    defined(CONFIG_460EXR) || defined(CONFIG_431EXR) || \
    defined(CONFIG_AM81281)
		if ( opcode == HSDMA_MOVE_SG1_SG2_CRC ||
		     opcode == HSDMA_GENERATE_CRC ) {
			attr = HSDMA_CHAIN_CRC;
		}
#endif
	}

	for ( ; timeout > 0; timeout--) {
		if ((stat=in_le32(hsdma_dev.dma[n].reg.csfpl))) {
			break;
		}
		mdelay(1);
	}

	if (timeout == 0) {
		ret = -ETIME;
	}

	/*
	 * Clear CS FIFO
	 */

	while ((stat=in_le32(hsdma_dev.dma[n].reg.csfpl)));

	/*
	 * Clear used cdb list
	 */

	list_for_each_entry_safe(desc,tmp,&hsdma_dev.dma[n].cdb.used,node) {
		list_del(&desc->node);
		list_add_tail(&desc->node,&hsdma_dev.dma[n].cdb.free);
	}

	return ret;
}

#if defined(CONFIG_PPC4xx_HSDMA_MODULE)

#if defined(CONFIG_PPC4xx_HSDMA_SELF_TEST)
extern void ppc4xx_hsdma_self_test(u32 dmanr);
#endif

/**
 * ppc4xx_hsdma_init - Initializes the HSDMA interface
 *
 * Initializes the HSMDA interface. Returns %0 on success or
 * %-ENOMEM on failure.
 */
static int __init ppc4xx_hsdma_init(void)
{
        LOG("initializing");
	LOG("version %s",PPC4xx_HSDMA_VERSION);

#if defined(CONFIG_PPC4xx_HSDMA_SELF_TEST)
	LOG("starting self_test");
	ppc4xx_hsdma_self_test(PPC4xx_HSDMA1);
#endif
	return 0;
}

module_init(ppc4xx_hsdma_init);

/**
 * ppc4xx_hsdma_exit - Releases the HSDMA interface
 *
 * Releases the HSMDA interface.
 */
static void __exit ppc4xx_hsdma_exit(void)
{
        LOG("cleaning up");

	ppc4xx_hsdma_cleanup();
}

module_exit(ppc4xx_hsdma_exit);

#endif

static int __init ppc4xx_hsdma_prepare(struct device_node *np)
{
        
        LOG("Parsing interrupt maps...");

        irq_full = irq_of_parse_and_map(np, 0);
        if ( irq_full == NO_IRQ ){
                dev_err(NULL, "irq_of_parse_and_map failed\n");
		return -1;
        }
        
        irq_srvc = irq_of_parse_and_map(np, 1);
        if ( irq_srvc == NO_IRQ ){
                dev_err(NULL, "irq_of_parse_and_map failed\n");
		return -1;
        }

        irq_hsdma_err = irq_of_parse_and_map(np, 2);
        if ( irq_hsdma_err == NO_IRQ ){
                dev_err(NULL, "irq_of_parse_and_map failed\n");
		return -1;
        }

        irq_i2o_err = irq_of_parse_and_map(np, 3);
        if ( irq_i2o_err == NO_IRQ ){
                dev_err(NULL, "irq_of_parse_and_map failed\n");
		return -1;
        }

        LOG("succeeded.");

        return 0;
}


static int __init ppc4xx_hsdma_initialize(void)
{
        struct device_node *np;

        for_each_compatible_node(np, NULL, "amcc,hsdma-460gt")
                ppc4xx_hsdma_prepare(np);
        return 0;
}

arch_initcall(ppc4xx_hsdma_initialize);


/*
 * EXPORT FUNCTIONS:
 */

EXPORT_SYMBOL(ppc4xx_hsdma_request_dma);
EXPORT_SYMBOL(ppc4xx_hsdma_free_dma);

EXPORT_SYMBOL(ppc4xx_hsdma_noop);
EXPORT_SYMBOL(ppc4xx_hsdma_memcpy);
EXPORT_SYMBOL(ppc4xx_hsdma_memcpy2);
EXPORT_SYMBOL(ppc4xx_hsdma_data_check);
EXPORT_SYMBOL(ppc4xx_hsdma_data_fill);
EXPORT_SYMBOL(ppc4xx_hsdma_lfsr_reset);
EXPORT_SYMBOL(ppc4xx_hsdma_lfsr_fill);
EXPORT_SYMBOL(ppc4xx_hsdma_lfsr_check);
EXPORT_SYMBOL(ppc4xx_hsdma_lfsr_ifill);
EXPORT_SYMBOL(ppc4xx_hsdma_lfsr_icheck);
#if defined(CONFIG_460GT) || defined(CONFIG_460EX) || \
    defined(CONFIG_460EXR) || defined(CONFIG_431EXR) || \
    defined(CONFIG_AM81281)
EXPORT_SYMBOL(ppc4xx_hsdma_memcpy_crc);
EXPORT_SYMBOL(ppc4xx_hsdma_generate_crc);
#endif

EXPORT_SYMBOL(ppc4xx_hsdma);


