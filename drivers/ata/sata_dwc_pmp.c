/*
 * drivers/ata/sata_dwc.c
 *
 * Synopsys DesignWare Cores (DWC) SATA host driver
 *
 * Author: Mark Miesfeld <mmiesfeld@amcc.com>
 *
 * Ported from 2.6.19.2 to 2.6.25/26 by Stefan Roese <sr@denx.de>
 * Copyright 2008 DENX Software Engineering
 *
 * Based on versions provided by AMCC and Synopsys which are:
 *          Copyright 2006 Applied Micro Circuits Corporation
 *          COPYRIGHT (C) 2005  SYNOPSYS, INC.  ALL RIGHTS RESERVED
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */


#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/libata.h>
#include <linux/rtc.h>
#include <linux/slab.h>

#include <scsi/scsi_host.h>
#include <scsi/scsi_cmnd.h>

#include "libata.h" //omw

/**
 *  Definition for printing debug information.
 *  CONFIG_SATA_DWC_DEBUG - print out step by step calling
 *  functions so that we can keep track the processing flow.
 * 
 *  CONFIG_SATA_DWC_VDEBUG - print out verbose information
 *  in addition to debug information. This will include detail
 *  values for DMA, register contents,...
 */
#ifdef CONFIG_SATA_DWC_DEBUG
#define dwc_dev_dbg(dev, format, arg...) \
	({ if (0) dev_printk(KERN_INFO, dev, format, ##arg); 0; })
#define dwc_port_dbg(ap, format, arg...) \
	ata_port_printk(ap, KERN_INFO, format, ##arg)
#define dwc_link_dbg(link, format, arg...) \
	ata_link_printk(link, KERN_INFO, format, ##arg)
#else
#define dwc_dev_dbg(dev, format, arg...) \
	({ 0; })
#define dwc_port_dbg(ap, format, arg...) \
	({ 0; })
#define dwc_link_dbg(link, format, arg...) \
	({ 0; })
#endif

#define DEBUG_NCQ
#ifdef CONFIG_SATA_DWC_VDEBUG
#define DEBUG_NCQ
#define dwc_dev_vdbg(dev, format, arg...) \
	({ if (0) dev_printk(KERN_INFO, dev, format, ##arg); 0; })
#define dwc_port_vdbg(ap, format, arg...) \
	ata_port_printk(ap, KERN_INFO, format, ##arg)
#define dwc_link_vdbg(link, format, arg...) \
	ata_link_printk(link, KERN_INFO, format, ##arg)
#else
#define dwc_dev_vdbg(dev, format, arg...) \
	({ 0; })
#define dwc_port_vdbg(ap, format, arg...) \
	({ 0; })
#define dwc_link_vdbg(link, format, arg...) \
	({ 0; })
#endif

#define dwc_dev_info(dev, format, arg...) \
	({ if (0) dev_printk(KERN_INFO, dev, format, ##arg); 0; })
#define dwc_port_info(ap, format, arg...) \
	ata_port_printk(ap, KERN_INFO, format, ##arg)
#define dwc_link_info(link, format, arg...) \
	ata_link_printk(link, KERN_INFO, format, ##arg)



#define DRV_NAME        "sata-dwc"
#define DRV_VERSION     "2.0"

/* Port Multiplier discovery Signature */
#define PSCR_SCONTROL_DET_ENABLE	0x00000001
#define PSCR_SSTATUS_DET_PRESENT	0x00000001
#define PSCR_SERROR_DIAG_X		0x04000000

/* Port multiplier port entry in SCONTROL register */
#define SCONTROL_PMP_MASK		0x000f0000
#define PMP_TO_SCONTROL(p)		((p << 16) & 0x000f0000)
#define SCONTROL_TO_PMP(p)		(((p) & 0x000f0000) >> 16)


/* SATA DMA driver Globals */
#if defined(CONFIG_APM82181)
#define DMA_NUM_CHANS			2
#else
#define DMA_NUM_CHANS			1
#endif

#define DMA_NUM_CHAN_REGS		8

/* SATA DMA Register definitions */
#if defined(CONFIG_APM82181)
#define AHB_DMA_BRST_DFLT       64  /* 16 data items burst length */
#else
#define AHB_DMA_BRST_DFLT		64	/* 16 data items burst length */
#endif

struct dmareg {
	u32 low;		/* Low bits 0-31 */
	u32 high;		/* High bits 32-63 */
};

/* DMA Per Channel registers */

struct dma_chan_regs {
	struct dmareg sar;	/* Source Address */
	struct dmareg dar;	/* Destination address */
	struct dmareg llp;	/* Linked List Pointer */
	struct dmareg ctl;	/* Control */
	struct dmareg sstat;	/* Source Status not implemented in core */
	struct dmareg dstat;	/* Destination Status not implemented in core */
	struct dmareg sstatar;	/* Source Status Address not impl in core */
	struct dmareg dstatar;	/* Destination Status Address not implemented */
	struct dmareg cfg;	/* Config */
	struct dmareg sgr;	/* Source Gather */
	struct dmareg dsr;	/* Destination Scatter */
};

/* Generic Interrupt Registers */
struct dma_interrupt_regs {
	struct dmareg tfr;	/* Transfer Interrupt */
	struct dmareg block;	/* Block Interrupt */
	struct dmareg srctran;	/* Source Transfer Interrupt */
	struct dmareg dsttran;	/* Dest Transfer Interrupt */
	struct dmareg error;	/* Error */
};

struct ahb_dma_regs {
	struct dma_chan_regs	chan_regs[DMA_NUM_CHAN_REGS];
	struct dma_interrupt_regs interrupt_raw;	/* Raw Interrupt */
	struct dma_interrupt_regs interrupt_status;	/* Interrupt Status */
	struct dma_interrupt_regs interrupt_mask;	/* Interrupt Mask */
	struct dma_interrupt_regs interrupt_clear;	/* Interrupt Clear */
	struct dmareg		statusInt;		/* Interrupt combined */
	struct dmareg		rq_srcreg;		/* Src Trans Req */
	struct dmareg		rq_dstreg;		/* Dst Trans Req */
	struct dmareg		rq_sgl_srcreg;		/* Sngl Src Trans Req */
	struct dmareg		rq_sgl_dstreg;		/* Sngl Dst Trans Req */
	struct dmareg		rq_lst_srcreg;		/* Last Src Trans Req */
	struct dmareg		rq_lst_dstreg;		/* Last Dst Trans Req */
	struct dmareg		dma_cfg;		/* DMA Config */
	struct dmareg		dma_chan_en;		/* DMA Channel Enable */
	struct dmareg		dma_id;			/* DMA ID */
	struct dmareg		dma_test;		/* DMA Test */
	struct dmareg		res1;			/* reserved */
	struct dmareg		res2;			/* reserved */

	/* DMA Comp Params
	 * Param 6 = dma_param[0], Param 5 = dma_param[1],
	 * Param 4 = dma_param[2] ...
	 */
	struct dmareg		dma_params[6];
};

/* Data structure for linked list item */
struct lli {
	u32		sar;		/* Source Address */
	u32		dar;		/* Destination address */
	u32		llp;		/* Linked List Pointer */
	struct dmareg	ctl;		/* Control */
#if defined(CONFIG_APM82181)
	u32             dstat;          /* Source status is not supported */
#else
	struct dmareg	dstat;		/* Destination Status */
#endif
};

#define DWC_DMAC_LLI_SZ		(sizeof(struct lli))
#define DWC_DMAC_LLI_NUM		256
#define DWC_DMAC_TWIDTH_BYTES	4
#define DWC_DMAC_LLI_TBL_SZ	\
	(DWC_DMAC_LLI_SZ * DWC_DMAC_LLI_NUM)
#if defined(CONFIG_APM82181)
#define DWC_DMAC_CTRL_TSIZE_MAX    \
        (0x00000800 * DWC_DMAC_TWIDTH_BYTES)
#else
#define DWC_DMAC_CTRL_TSIZE_MAX	\
	(0x00000800 * DWC_DMAC_TWIDTH_BYTES)
#endif
/* DMA Register Operation Bits */
#define DMA_EN              0x00000001		/* Enable AHB DMA */
#define DMA_WRITE_EN(ch)    ((0x000000001 << (ch)) << 8)
#define DMA_CHANNEL(ch)		(0x00000001 << (ch))	/* Select channel */
#define DMA_ENABLE_CHAN(ch)	(DMA_WRITE_EN(ch) | DMA_CHANNEL(ch))
#define DMA_DISABLE_CHAN(ch)	(DMA_WRITE_EN(ch) | 0x00000000)

/* Channel Control Register */
#define DMA_CTL_BLK_TS(size)	((size) & 0x000000FFF)	/* Blk Transfer size */
#define DMA_CTL_LLP_SRCEN	0x10000000	/* Blk chain enable Src */
#define DMA_CTL_LLP_DSTEN	0x08000000	/* Blk chain enable Dst */
/*
 * This define is used to set block chaining disabled in the control low
 * register.  It is already in little endian format so it can be &'d dirctly.
 * It is essentially: cpu_to_le32(~(DMA_CTL_LLP_SRCEN | DMA_CTL_LLP_DSTEN))
 */
#define DMA_CTL_LLP_DISABLE_LE32 0xffffffe7
#define DMA_CTL_SMS(num)	((num & 0x3) << 25)	/*Src Master Select*/
#define DMA_CTL_DMS(num)	((num & 0x3) << 23)	/*Dst Master Select*/
#define DMA_CTL_TTFC(type)	((type & 0x7) << 20)	/*Type&Flow cntr*/
#define DMA_CTL_TTFC_P2M_DMAC	0x00000002		/*Per mem,DMAC cntr*/
#define DMA_CTL_TTFC_M2P_PER	0x00000003		/*Mem per,peri cntr*/
#define DMA_CTL_SRC_MSIZE(size)	((size & 0x7) << 14)	/*Src Burst Len*/
#define DMA_CTL_DST_MSIZE(size)	((size & 0x7) << 11)	/*Dst Burst Len*/
#define DMA_CTL_SINC_INC	0x00000000		/*Src addr incr*/
#define DMA_CTL_SINC_DEC	0x00000200
#define DMA_CTL_SINC_NOCHANGE	0x00000400
#define DMA_CTL_DINC_INC	0x00000000		/*Dst addr incr*/
#define DMA_CTL_DINC_DEC	0x00000080
#define DMA_CTL_DINC_NOCHANGE	0x00000100
#define DMA_CTL_SRC_TRWID(size)	((size & 0x7) << 4)	/*Src Trnsfr Width*/
#define DMA_CTL_DST_TRWID(size)	((size & 0x7) << 1)	/*Dst Trnsfr Width*/
#define DMA_CTL_INT_EN		0x00000001		/*Interrupt Enable*/

/* Channel Configuration Register high bits */
#define DMA_CFG_FCMOD_REQ	0x00000001		/*Flow cntrl req*/
#define DMA_CFG_PROTCTL		(0x00000003 << 2)	/*Protection cntrl*/

/* Channel Configuration Register low bits */
#define DMA_CFG_RELD_DST	0x80000000		/*Reload Dst/Src Addr*/
#define DMA_CFG_RELD_SRC	0x40000000
#define DMA_CFG_HS_SELSRC	0x00000800		/*SW hndshk Src/Dst*/
#define DMA_CFG_HS_SELDST	0x00000400
#define DMA_CFG_FIFOEMPTY       (0x00000001 << 9)	/*FIFO Empty bit*/

/* Assign hardware handshaking interface (x) to dst / sre peripheral */
#define DMA_CFG_HW_HS_DEST(int_num)	((int_num & 0xF) << 11)
#define DMA_CFG_HW_HS_SRC(int_num)	((int_num & 0xF) << 7)

/* Channel Linked List Pointer Register */
#define DMA_LLP_LMS(addr, master)	(((addr) & 0xfffffffc) | (master))
#define DMA_LLP_AHBMASTER1		0	/* List Master Select */
#define DMA_LLP_AHBMASTER2		1

#define SATA_DWC_MAX_PORTS	1

#define DWC_SCR_OFFSET	0x24
#define DWC_REG_OFFSET	0x64

/* DWC SATA Registers */
struct sata_dwc_regs {
	u32 fptagr;		/* 1st party DMA tag */
	u32 fpbor;		/* 1st party DMA buffer offset */
	u32 fptcr;		/* 1st party DMA Xfr count */
	u32 dmacr;		/* DMA Control */
	u32 dbtsr;		/* DMA Burst Transac size */
	u32 intpr;		/* Interrupt Pending */
	u32 intmr;		/* Interrupt Mask */
	u32 errmr;		/* Error Mask */
	u32 llcr;		/* Link Layer Control */
	u32 phycr;		/* PHY Control */
	u32 physr;		/* PHY Status */
	u32 rxbistpd;		/* Recvd BIST pattern def register */
	u32 rxbistpd1;		/* Recvd BIST data dword1 */
	u32 rxbistpd2;		/* Recvd BIST pattern data dword2 */
	u32 txbistpd;		/* Trans BIST pattern def register */
	u32 txbistpd1;		/* Trans BIST data dword1 */
	u32 txbistpd2;		/* Trans BIST data dword2 */
	u32 bistcr;		/* BIST Control Register */
	u32 bistfctr;		/* BIST FIS Count Register */
	u32 bistsr;		/* BIST Status Register */
	u32 bistdecr;		/* BIST Dword Error count register */
	u32 res[15];		/* Reserved locations */
	u32 testr;		/* Test Register */
	u32 versionr;		/* Version Register */
	u32 idr;		/* ID Register */
	u32 unimpl[192];	/* Unimplemented */
	u32 dmadr[256];	/* FIFO Locations in DMA Mode */
};

#define SCR_SCONTROL_DET_ENABLE		0x00000001
#define SCR_SSTATUS_DET_PRESENT		0x00000001
#define SCR_SERROR_DIAG_X		0x04000000

/* DWC SATA Register Operations */
#define	DWC_TXFIFO_DEPTH		0x01FF
#define	DWC_RXFIFO_DEPTH		0x01FF

#define DWC_DMACR_TMOD_TXCHEN	0x00000004
#define	DWC_DMACR_TXCHEN		(0x00000001 | \
						DWC_DMACR_TMOD_TXCHEN)
#define	DWC_DMACR_RXCHEN		(0x00000002 | \
						DWC_DMACR_TMOD_TXCHEN)
#define DWC_DMACR_TX_CLEAR(v)	(((v) & ~DWC_DMACR_TXCHEN) | \
						DWC_DMACR_TMOD_TXCHEN)
#define DWC_DMACR_RX_CLEAR(v)	(((v) & ~DWC_DMACR_RXCHEN) | \
						DWC_DMACR_TMOD_TXCHEN)
#define DWC_DMACR_TXRXCH_CLEAR	DWC_DMACR_TMOD_TXCHEN

#define DWC_DMA_DBTSR_MWR(size)	((size/4) & \
						DWC_TXFIFO_DEPTH)
#define DWC_DMA_DBTSR_MRD(size)	(((size/4) & \
						DWC_RXFIFO_DEPTH) << 16)

// SATA DWC Interrupts
#define	DWC_INTPR_DMAT			0x00000001
#define DWC_INTPR_NEWFP			0x00000002
#define DWC_INTPR_PMABRT		0x00000004
#define DWC_INTPR_ERR			0x00000008
#define DWC_INTPR_NEWBIST		0x00000010
#define DWC_INTPR_PRIMERR		0x00000020
#define DWC_INTPR_CMDABORT		0x00000040
#define DWC_INTPR_CMDGOOD		0x00000080
#define DWC_INTPR_IPF			0x80000000

// 
#define DWC_LLCR_SCRAMEN		0x00000001
#define DWC_LLCR_DESCRAMEN		0x00000002
#define DWC_LLCR_RPDEN			0x00000004

// Defines for SError register
#define DWC_SERR_ERRI		0x00000001 // Recovered data integrity error
#define DWC_SERR_ERRM		0x00000002 // Recovered communication error
#define DWC_SERR_ERRT		0x00000100 // Non-recovered transient data integrity error
#define DWC_SERR_ERRC		0x00000200 // Non-recovered persistent communication or data integrity error
#define DWC_SERR_ERRP		0x00000400 // Protocol error
#define DWC_SERR_ERRE		0x00000800 // Internal host adapter error
#define DWC_SERR_DIAGN		0x00010000 // PHYRdy change
#define DWC_SERR_DIAGI		0x00020000 // PHY internal error
#define DWC_SERR_DIAGW		0x00040000 // Phy COMWAKE signal is detected
#define DWC_SERR_DIAGB		0x00080000 // 10b to 8b decoder err
#define DWC_SERR_DIAGT		0x00100000 // Disparity error
#define DWC_SERR_DIAGC		0x00200000 // CRC error
#define DWC_SERR_DIAGH		0x00400000 // Handshake error
#define DWC_SERR_DIAGL		0x00800000 // Link sequence (illegal transition) error
#define DWC_SERR_DIAGS		0x01000000 // Transport state transition error
#define DWC_SERR_DIAGF		0x02000000 // Unrecognized FIS type
#define DWC_SERR_DIAGX		0x04000000 // Exchanged error - Set when PHY COMINIT signal is detected.
#define DWC_SERR_DIAGA		0x08000000 // Port Selector Presence detected

/* This is all error bits, zero's are reserved fields. */
#define DWC_SERR_ERR_BITS	0x0FFF0F03

#define DWC_SCR0_SPD_GET(v)	((v >> 4) & 0x0000000F)

struct sata_dwc_device {
	struct resource  reg;            /* Resource for register */
	struct device   *dev;		/* generic device struct */
	struct ata_probe_ent *pe;		/* ptr to probe-ent */
	struct ata_host *host;
	u8              *reg_base;
	struct sata_dwc_regs *sata_dwc_regs;	/* DW Synopsys SATA specific */
	u8              *scr_base;
	int             dma_channel;	/* DWC SATA DMA channel  */
	int             irq_dma;
	struct timer_list an_timer;
};

#define DWC_QCMD_MAX	32

/*
 * This structure manages operations on the SATA port.
 * Especially for NCQ transfers.
 * For each DMA transfer, there are 2 interrupts signaled after
 * enabling a transfer: DMA complete interrupt and SATA interrupt.
 * One can occur before another. The QC completes should be called
 * at the later one.
 */
struct sata_dwc_device_port {
	struct sata_dwc_device	*hsdev;
	struct lli	*llit[DWC_QCMD_MAX];
	dma_addr_t	llit_dma[DWC_QCMD_MAX];
	int			dma_chan[DWC_QCMD_MAX];
	int			dma_pending[DWC_QCMD_MAX];
	u32 		sactive_issued;	/* issued queued ops */
	u32 		sactive_queued;	/* queued ops */
};

static struct dwc_error_info {
	u32 err_reg;
	const char *desc;
} dwc_error_db[18] = {
	{DWC_SERR_ERRI , "ERRI"},
	{DWC_SERR_ERRM , "ERRM"},
	{DWC_SERR_ERRT , "ERRT"},
	{DWC_SERR_ERRC , "ERRC"},
	{DWC_SERR_ERRP , "ERRP"},
	{DWC_SERR_ERRE , "ERRE"},
	{DWC_SERR_DIAGN, "DIAGN"},
	{DWC_SERR_DIAGI, "DIAGI"},
	{DWC_SERR_DIAGW, "DIAGW"},
	{DWC_SERR_DIAGB, "DIAGB"},
	{DWC_SERR_DIAGT, "DIAGT"},
	{DWC_SERR_DIAGC, "DIAGC"},
	{DWC_SERR_DIAGH, "DIAGH"},
	{DWC_SERR_DIAGL, "DIAGL"},
	{DWC_SERR_DIAGS, "DIAGS"},
	{DWC_SERR_DIAGF, "DIAGF"},
	{DWC_SERR_DIAGX, "DIAGX"},
	{DWC_SERR_DIAGA, "DIAGA"},
};

static struct sata_dwc_device* dwc_dev_list[2];
static int dma_intr_registered = 0;
/*
 * Commonly used DWC SATA driver Macros
 */
#define HSDEV_FROM_HOST(host)	((struct sata_dwc_device *) \
					(host)->private_data)
#define HSDEV_FROM_AP(ap)	((struct sata_dwc_device *) \
					(ap)->host->private_data)
#define HSDEVP_FROM_AP(ap)	((struct sata_dwc_device_port *) \
					(ap)->private_data)
#define HSDEV_FROM_QC(qc)	((struct sata_dwc_device *) \
					(qc)->ap->host->private_data)
#define HSDEV_FROM_HSDEVP(p)	((struct sata_dwc_device *) \
					(hsdevp)->hsdev)

enum {
	DWC_DMA_NONE			= 0,
	DWC_DMA_PENDING_TX		= 1,
	DWC_DMA_PENDING_RX		= 2,
	DWC_DMA_ITPR			= 3,
	DWC_DMA_DONE			= 4,
};

/*
 * Globals
 */
static struct ahb_dma_regs *sata_dma_regs = 0;

/*
 * Prototypes
 */
static void dwc_bmdma_start_by_tag(struct ata_queued_cmd *qc, u8 tag);
static int dwc_qc_complete(struct ata_port *ap, struct ata_queued_cmd *qc,
				u32 check_status);
static void dwc_current_qc_complete(struct ata_port *ap, u32 check_status);
static void dwc_port_stop(struct ata_port *ap);
static void dwc_dma_clear_ctrlreg(struct sata_dwc_device_port *hsdevp, u8 tag);

static int dwc_dma_init(struct sata_dwc_device *hsdev);
static void dwc_dma_exit(struct sata_dwc_device *hsdev);
static void dwc_dma_terminate(struct ata_port *ap, int dma_ch);
static void dwc_enable_interrupts(struct sata_dwc_device *hsdev);
static void sata_dwc_init_port ( struct ata_port *ap );
u8 dwc_check_status(struct ata_port *ap);

/**
 * Convert content of SERROR register to text for displayed
 */
static void dwc_err_2_txt (u32 error) {
	int i;
	struct dwc_error_info *err_info;

	printk("\ndwc_error_intr detect errors: ");
	for( i=0; i<ARRAY_SIZE(dwc_error_db); i++) {
		err_info = &dwc_error_db[i];
		if ( err_info->err_reg & error )
			printk(" %s", err_info->desc);
	}
	printk("\n");
}

#if defined(CONFIG_SATA_DWC_VDEBUG)
/*
 * Display contents of DMA control registers.
 */
static void dwc_dma_chan_display( int dma_chan) {
	u32 reg_high, reg_low;

	printk("%s DMA channel %d content:\n", __func__, dma_chan);

	reg_high = in_le32(&(sata_dma_regs->chan_regs[dma_chan].cfg.high));
	reg_low  = in_le32(&(sata_dma_regs->chan_regs[dma_chan].cfg.low));
	printk(" - CFG: high=0x%08x, low=0x%08x\n", reg_high, reg_low);
	
	reg_low  = in_le32(&(sata_dma_regs->chan_regs[dma_chan].llp.low));
	printk(" - LLP: low=0x%08x\n", reg_low);

	printk(" - CTL: low=0x%08x\n", in_le32(&(sata_dma_regs->chan_regs[dma_chan].ctl.low)));
	
	reg_high = in_le32(&(sata_dma_regs->chan_regs[dma_chan].sar.high));
	reg_low  = in_le32(&(sata_dma_regs->chan_regs[dma_chan].sar.low));
	printk(" - SAR: high=0x%08x, low=0x%08x\n", reg_high, reg_low);
	
	reg_high = in_le32(&(sata_dma_regs->chan_regs[dma_chan].dar.high));
	reg_low  = in_le32(&(sata_dma_regs->chan_regs[dma_chan].dar.low));
	printk(" - DAR: high=0x%08x, low=0x%08x\n", reg_high, reg_low);
}

/*
 * Function to convert DMA direction to text to be used for debug purpose.
 */
static const char *dir_2_txt(enum dma_data_direction dir)
{
	switch (dir) {
	case DMA_BIDIRECTIONAL:
		return "bi";
	case DMA_FROM_DEVICE:
		return "from";
	case DMA_TO_DEVICE:
		return "to";
	case DMA_NONE:
		return "none";
	default:
		return "err";
	}
}
#endif


/*
 * Function to convert SATA protocol into text so that debug information
 * can be easy understandable.
 */
static const char *prot_2_txt(enum ata_tf_protocols protocol)
{
	switch (protocol) {
	case ATA_PROT_UNKNOWN:
		return "ata unknown";
	case ATA_PROT_NODATA:
		return "ata nodata";
	case ATA_PROT_PIO:
		return "ata pio";
	case ATA_PROT_DMA:
		return "dma";
	case ATA_PROT_NCQ:
		return "ata ncq";
	case ATAPI_PROT_PIO:
		return "atapi pio";
	case ATAPI_PROT_NODATA:
		return "atapi nodata";
	case ATAPI_PROT_DMA:
		return "atapi dma";
	default:
		return "err";
	}
}

/*
 * Function to convert ATA command to text
 */
inline const char *ata_cmd_2_txt(const struct ata_taskfile *tf)
{
	switch (tf->command) {
	case ATA_CMD_CHK_POWER:
		return "ATA_CMD_CHK_POWER";
	case ATA_CMD_EDD:
		return "ATA_CMD_EDD";
	case ATA_CMD_FLUSH:
		return "ATA_CMD_FLUSH";
	case ATA_CMD_FLUSH_EXT:
		return "ATA_CMD_FLUSH_EXT";
	case ATA_CMD_ID_ATA:
		return "ATA_CMD_ID_ATA";
	case ATA_CMD_ID_ATAPI:
		return "ATA_CMD_ID_ATAPI";
	case ATA_CMD_FPDMA_READ:
		return "ATA_CMD_FPDMA_READ";
	case ATA_CMD_FPDMA_WRITE:
		return "ATA_CMD_FPDMA_WRITE";
	case ATA_CMD_READ:
		return "ATA_CMD_READ";
	case ATA_CMD_READ_EXT:
		return "ATA_CMD_READ_EXT";
	case ATA_CMD_READ_NATIVE_MAX_EXT :
		return "ATA_CMD_READ_NATIVE_MAX_EXT";
	case ATA_CMD_VERIFY_EXT :
		return "ATA_CMD_VERIFY_EXT";
	case ATA_CMD_WRITE:
		return "ATA_CMD_WRITE";
	case ATA_CMD_WRITE_EXT:
		return "ATA_CMD_WRITE_EXT";
	case ATA_CMD_PIO_READ:
		return "ATA_CMD_PIO_READ";
	case ATA_CMD_PIO_READ_EXT:
		return "ATA_CMD_PIO_READ_EXT";
	case ATA_CMD_PIO_WRITE:
		return "ATA_CMD_PIO_WRITE";
	case ATA_CMD_PIO_WRITE_EXT:
		return "ATA_CMD_PIO_WRITE_EXT";
	case ATA_CMD_SET_FEATURES:
		return "ATA_CMD_SET_FEATURES";
	case ATA_CMD_PACKET:
		return "ATA_CMD_PACKET";
	case ATA_CMD_PMP_READ:
		return "ATA_CMD_PMP_READ";
	case ATA_CMD_PMP_WRITE:
		return "ATA_CMD_PMP_WRITE";
	default:
		return "ATA_CMD_???";
	}
}

/* 
 * Dump content of the taskfile
 */
static void sata_dwc_tf_dump(struct device *dwc_dev, struct ata_taskfile *tf)
{
	dwc_dev_vdbg(dwc_dev, "taskfile cmd: 0x%02x protocol: %s flags: 0x%lx"
			"device: %x\n", tf->command, prot_2_txt(tf->protocol),
			tf->flags, tf->device);
	dwc_dev_vdbg(dwc_dev, "feature: 0x%02x nsect: 0x%x lbal: 0x%x lbam:"
			"0x%x lbah: 0x%x\n", tf->feature, tf->nsect, tf->lbal,
			tf->lbam, tf->lbah);
	dwc_dev_vdbg(dwc_dev, "hob_feature: 0x%02x hob_nsect: 0x%x hob_lbal: 0x%x "
			"hob_lbam: 0x%x hob_lbah: 0x%x\n", tf->hob_feature,
			tf->hob_nsect, tf->hob_lbal, tf->hob_lbam,
			tf->hob_lbah);
}

/*
 * Function: get_burst_length_encode
 * arguments: datalength: length in bytes of data
 * returns value to be programmed in register corrresponding to data length
 * This value is effectively the log(base 2) of the length
 */
static inline int get_burst_length_encode(int datalength)
{
	int items = datalength >> 2;	/* div by 4 to get lword count */

	if (items >= 64)
		return 5;

	if (items >= 32)
		return 4;

	if (items >= 16)
		return 3;

	if (items >= 8)
		return 2;

	if (items >= 4)
		return 1;

	return 0;
}

static inline u32 dwc_qctag_to_mask(u8 tag)
{
	return 0x00000001 << (tag & 0x1f);
}

/*
 * Clear Interrupts on a DMA channel
 */
static inline void dwc_dma_clear_chan_intpr(int c)
{
	out_le32(&(sata_dma_regs->interrupt_clear.tfr.low), DMA_CHANNEL(c));
	out_le32(&(sata_dma_regs->interrupt_clear.block.low), DMA_CHANNEL(c));
	out_le32(&(sata_dma_regs->interrupt_clear.srctran.low), DMA_CHANNEL(c));
	out_le32(&(sata_dma_regs->interrupt_clear.dsttran.low), DMA_CHANNEL(c));
	out_le32(&(sata_dma_regs->interrupt_clear.error.low), DMA_CHANNEL(c));
}

static struct ata_queued_cmd *dwc_get_active_qc(struct ata_port *ap)
{
	struct ata_queued_cmd *qc;

	qc = ata_qc_from_tag(ap, ap->link.active_tag);
	if (qc && !(qc->tf.flags & ATA_TFLAG_POLLING))
		return qc;
	return NULL;
}

/*
 *  dwc_dma_request_channel - request channel for DMA transfer
 *  @ap: port to request DMA channel
 *  
 *  Check the ChEnReg register to see if the requested DMA channel
 *  is currently available.
 *
 *  RETURNS:
 *    DMA channel is available. -1 if DMA channel not available
 */
static int dwc_dma_request_channel(struct ata_port *ap)
{
	struct sata_dwc_device *hsdev = HSDEV_FROM_AP(ap);

	// Check if channel is available
	if (!(in_le32(&(sata_dma_regs->dma_chan_en.low)) & DMA_CHANNEL(hsdev->dma_channel))) {
		return (hsdev->dma_channel);
	}
	// Retry channel request
	dwc_dma_terminate(ap, hsdev->dma_channel);
	// Recheck channel available
	if (!(in_le32(&(sata_dma_regs->dma_chan_en.low)) & DMA_CHANNEL(hsdev->dma_channel))) {
		return (hsdev->dma_channel);
	}

	return -1;
}



/**
 *  dwc_dma_interrupt - process DMA transfer complete interrupt
 *  @irq: interrupt number
 *  @hsdev_instance: SATA DWC device instance
 *
 *  Interrupt Handler for DW AHB SATA DMA - 
 *  Clear DMA control registers and DMA interrupt.
 *  In case of error in DMA transfer, terminate the
 *  transfer.
 *  
 *  RETURNS:
 *    IRQ_HANDLED
 */
static int dwc_dma_interrupt(int irq, void *hsdev_instance)
{
	volatile u32 tfr_reg, err_reg;
	unsigned long flags;
	struct sata_dwc_device *hsdev =
		(struct sata_dwc_device *)hsdev_instance;
	struct ata_host *host = (struct ata_host *)hsdev->host;
	struct ata_port *ap;
	struct sata_dwc_device_port *hsdevp;
	u8 tag = 0;
	int chan;
	unsigned int port = 0;
	spin_lock_irqsave(&host->lock, flags);

	ap = host->ports[port];
	hsdevp = HSDEVP_FROM_AP(ap);
	if ( ap->link.active_tag != ATA_TAG_POISON )
		tag = ap->link.active_tag;

	dwc_port_dbg(ap, "%s: DMA interrupt in channel %d\n", __func__, hsdev->dma_channel);
	tfr_reg = in_le32(&(sata_dma_regs->interrupt_status.tfr.low));
	err_reg = in_le32(&(sata_dma_regs->interrupt_status.error.low));

	dwc_port_vdbg(ap, "eot=0x%08x err=0x%08x pending=%d active port=%d\n",
		tfr_reg, err_reg, hsdevp->dma_pending[tag], port);
	chan = hsdev->dma_channel;				
	
	if (tfr_reg & DMA_CHANNEL(chan)) {
		// Clear DMA control register
		dwc_dma_clear_ctrlreg(hsdevp, tag);

		/* Clear the interrupt */
		out_le32(&(sata_dma_regs->interrupt_clear.tfr.low),
			  DMA_CHANNEL(chan));
	}

	/* Process error interrupt. */
	// We do not expect error happen
	if (unlikely(err_reg & DMA_CHANNEL(chan))) {
		// TODO Need error handler
		dwc_port_info(ap, "%s - error interrupt err_reg=0x%08x\n",
				__func__, err_reg);
		ap->hsm_task_state = HSM_ST_ERR;
		// disable DMAC
		dwc_dma_terminate(ap, chan);

		// 
		dwc_dma_clear_ctrlreg(hsdevp, tag);

		// Clear the interrupt.
		out_le32(&(sata_dma_regs->interrupt_clear.error.low),
			  DMA_CHANNEL(chan));
		// Complete the QC
		dwc_current_qc_complete(ap, 1);
	}
	hsdevp->dma_pending[tag] = DWC_DMA_NONE;
	spin_unlock_irqrestore(&host->lock, flags);
	return IRQ_HANDLED;
}

static irqreturn_t dwc_dma_handler(int irq, void *hsdev_instance)
{
	volatile u32 tfr_reg, err_reg;
	int chan;

	tfr_reg = in_le32(&(sata_dma_regs->interrupt_status.tfr.low));	
	err_reg = in_le32(&(sata_dma_regs->interrupt_status.error.low));

	for (chan = 0; chan < DMA_NUM_CHANS; chan++) {
		/* Check for end-of-transfer interrupt. */
		if (tfr_reg & DMA_CHANNEL(chan)) {
			dwc_dma_interrupt(irq, dwc_dev_list[chan]);
		}
		else
		/* Check for error interrupt. */
		if (err_reg & DMA_CHANNEL(chan)) {
			printk("****** %s: error interrupt on channel %d\n", __func__, chan);
			dwc_dma_interrupt(irq, dwc_dev_list[chan]);
		}
	}

	return IRQ_HANDLED;
}

static int dwc_dma_register_intpr (struct sata_dwc_device *hsdev)
{
	int retval = 0;
	int irq = hsdev->irq_dma;
	/* 
	 * FIXME: 2 SATA controllers share the same DMA engine so
	 * currently, they also share same DMA interrupt
	 */
	if (!dma_intr_registered) {
		printk("%s register irq (%d)\n", __func__, irq);
		retval = request_irq(irq, dwc_dma_handler, IRQF_SHARED, "SATA DMA", hsdev);
		//retval = request_irq(irq, dwc_dma_handler, IRQF_DISABLED, "SATA DMA", NULL);
		if (retval) {
			dev_err(hsdev->dev, "%s: could not get IRQ %d\n", __func__, irq);
			return -ENODEV;
		}
		//dma_intr_registered = 1;
	}
	return retval;
}

/*
 * dwc_dma_request_intpr
 * arguments: hsdev
 * returns status
 * This function registers ISR for a particular DMA channel interrupt
 */
static int dwc_dma_request_intpr(struct sata_dwc_device *hsdev, int irq)
{
	int retval = 0;
	int dma_chan = hsdev->dma_channel;

	/* Unmask error interrupt */
	out_le32(&sata_dma_regs->interrupt_mask.error.low,
			 in_le32(&sata_dma_regs->interrupt_mask.error.low) | DMA_ENABLE_CHAN(dma_chan));

	/* Unmask end-of-transfer interrupt */
	out_le32(&sata_dma_regs->interrupt_mask.tfr.low,
			in_le32(&sata_dma_regs->interrupt_mask.tfr.low) | DMA_ENABLE_CHAN(dma_chan));

	dwc_dev_vdbg(hsdev->dev, "Current value of interrupt_mask.error=0x%0x\n", in_le32(&sata_dma_regs->interrupt_mask.error.low));
	dwc_dev_vdbg(hsdev->dev, "Current value of interrupt_mask.tfr=0x%0x\n", in_le32(&sata_dma_regs->interrupt_mask.tfr.low));
	/* */
	//in_le32(&sata_dma_regs->statusInt.low) | 0x00000010;
#if 0
	out_le32(&sata_dma_regs->interrupt_mask.block.low,
			DMA_ENABLE_CHAN(dma_chan));

	out_le32(&sata_dma_regs->interrupt_mask.srctran.low,
			DMA_ENABLE_CHAN(dma_chan));

	out_le32(&sata_dma_regs->interrupt_mask.dsttran.low,
			DMA_ENABLE_CHAN(dma_chan));
#endif
	return retval;
}

/**
 *  dwc_map_sg_to_lli - map scatter/gather list to LLI structure
 *  @qc: Command queue to be processed
 *  @lli: scatter/gather list(sg)
 *	@dma_lli: LLI table
 *	@dmadr_addr: destination address
 * 
 *  This function creates a list of LLIs for DMA Xfr and returns the number
 *  of elements in the DMA linked list.
 *
 *  Note that the Synopsis driver has a comment proposing that better performance
 *  is possible by only enabling interrupts on the last item in the linked list.
 *  However, it seems that could be a problem if an error happened on one of the
 *  first items.  The transfer would halt, but no error interrupt would occur.
 *
 *  Currently this function sets interrupts enabled for each linked list item:
 *  DMA_CTL_INT_EN.
 *
 *  RETURNS:
 *   Array of AHB DMA Linked List Items
 */
static int dwc_map_sg_to_lli(struct ata_queued_cmd *qc, struct lli *lli,
			 dma_addr_t dma_lli, void __iomem *dmadr_addr)
{
	struct scatterlist *sg = qc->sg;
	struct device *dwc_dev = qc->ap->dev;
	int num_elems = qc->n_elem;
	int dir = qc->dma_dir;
	struct sata_dwc_device_port *hsdevp = HSDEVP_FROM_AP(qc->ap);
	
	int i, idx = 0;
	int fis_len = 0;
	dma_addr_t next_llp;
	int bl;
	unsigned int dma_ts = 0;

	dwc_port_vdbg(qc->ap, "%s: sg=%p nelem=%d lli=%p dma_lli=0x%08x "
		"dmadr=0x%08x\n", __func__, sg, num_elems, lli, (u32)dma_lli,
		(u32)dmadr_addr);

	bl = get_burst_length_encode(AHB_DMA_BRST_DFLT);

	for (i = 0; i < num_elems; i++, sg++) {
		u32 addr, offset;
		u32 sg_len, len;

		addr = (u32) sg_dma_address(sg);
		sg_len = sg_dma_len(sg);

		dwc_port_vdbg(qc->ap, "%s: elem=%d sg_addr=0x%x sg_len=%d\n",
			__func__, i, addr, sg_len);

		while (sg_len) {

			if (unlikely(idx >= DWC_DMAC_LLI_NUM)) {
				/* The LLI table is not large enough. */
				dev_err(dwc_dev, "LLI table overrun (idx=%d)\n",
						idx);
				break;
			}
			len = (sg_len > DWC_DMAC_CTRL_TSIZE_MAX) ?
				DWC_DMAC_CTRL_TSIZE_MAX : sg_len;

			offset = addr & 0xffff;
			if ((offset + sg_len) > 0x10000)
				len = 0x10000 - offset;

			/*
			 * Make sure a LLI block is not created that will span a
			 * 8K max FIS boundary.  If the block spans such a FIS
			 * boundary, there is a chance that a DMA burst will
			 * cross that boundary -- this results in an error in
			 * the host controller.
			 */
			if (unlikely(fis_len + len > 8192)) {
				dwc_port_vdbg(qc->ap, "SPLITTING: fis_len=%d(0x%x) "
					"len=%d(0x%x)\n", fis_len, fis_len,
					len, len);
				len = 8192 - fis_len;
				fis_len = 0;
			} else {
				fis_len += len;
			}
			if (fis_len == 8192)
				fis_len = 0;

			/*
			 * Set DMA addresses and lower half of control register
			 * based on direction.
			 */
			dwc_port_vdbg(qc->ap, "%s: sg_len = %d, len = %d\n", __func__, sg_len, len);

#if defined(CONFIG_APM82181)
			 if (dir == DMA_FROM_DEVICE) {
                                lli[idx].dar = cpu_to_le32(addr);
                                lli[idx].sar = cpu_to_le32((u32)dmadr_addr);
				if (hsdevp->hsdev->dma_channel == 0) {/* DMA channel 0 */
		                        lli[idx].ctl.low = cpu_to_le32(
                                        DMA_CTL_TTFC(DMA_CTL_TTFC_P2M_DMAC) |
                                        DMA_CTL_SMS(1) |	/* Source: Master 2 */
                                        DMA_CTL_DMS(0) |	/* Dest: Master 1 */
                                        DMA_CTL_SRC_MSIZE(bl) |
                                        DMA_CTL_DST_MSIZE(bl) |
                                        DMA_CTL_SINC_NOCHANGE |
                                        DMA_CTL_SRC_TRWID(2) |
                                        DMA_CTL_DST_TRWID(2) |
                                        DMA_CTL_INT_EN |
                                        DMA_CTL_LLP_SRCEN |
                                        DMA_CTL_LLP_DSTEN);
				} else if (hsdevp->hsdev->dma_channel == 1) {/* DMA channel 1 */
					lli[idx].ctl.low = cpu_to_le32(
                                        DMA_CTL_TTFC(DMA_CTL_TTFC_P2M_DMAC) |
                                        DMA_CTL_SMS(2) |	/* Source: Master 3 */
                                        DMA_CTL_DMS(0) |	/* Dest: Master 1 */
                                        DMA_CTL_SRC_MSIZE(bl) |
                                        DMA_CTL_DST_MSIZE(bl) |
                                        DMA_CTL_SINC_NOCHANGE |
                                        DMA_CTL_SRC_TRWID(2) |
                                        DMA_CTL_DST_TRWID(2) |
                                        DMA_CTL_INT_EN |
                                        DMA_CTL_LLP_SRCEN |
                                        DMA_CTL_LLP_DSTEN);
				}
                        } else {        /* DMA_TO_DEVICE */
                                lli[idx].sar = cpu_to_le32(addr);
                                lli[idx].dar = cpu_to_le32((u32)dmadr_addr);
				if (hsdevp->hsdev->dma_channel == 0) {/* DMA channel 0 */
	                                lli[idx].ctl.low = cpu_to_le32(
                                        DMA_CTL_TTFC(DMA_CTL_TTFC_M2P_PER) |
                                        DMA_CTL_SMS(0) |
                                        DMA_CTL_DMS(1) |
                                        DMA_CTL_SRC_MSIZE(bl) |
                                        DMA_CTL_DST_MSIZE(bl) |
                                        DMA_CTL_DINC_NOCHANGE |
                                        DMA_CTL_SRC_TRWID(2) |
                                        DMA_CTL_DST_TRWID(2) |
                                        DMA_CTL_INT_EN |
                                        DMA_CTL_LLP_SRCEN |
                                        DMA_CTL_LLP_DSTEN);
				} else if (hsdevp->hsdev->dma_channel == 1) {/* DMA channel 1 */
	                                lli[idx].ctl.low = cpu_to_le32(
                                        DMA_CTL_TTFC(DMA_CTL_TTFC_M2P_PER) |
                                        DMA_CTL_SMS(0) |
                                        DMA_CTL_DMS(2) |
                                        DMA_CTL_SRC_MSIZE(bl) |
                                        DMA_CTL_DST_MSIZE(bl) |
                                        DMA_CTL_DINC_NOCHANGE |
                                        DMA_CTL_SRC_TRWID(2) |
                                        DMA_CTL_DST_TRWID(2) |
                                        DMA_CTL_INT_EN |
                                        DMA_CTL_LLP_SRCEN |
                                        DMA_CTL_LLP_DSTEN);
				}
                        }
#else
			if (dir == DMA_FROM_DEVICE) {
				lli[idx].dar = cpu_to_le32(addr);
				lli[idx].sar = cpu_to_le32((u32)dmadr_addr);

				lli[idx].ctl.low = cpu_to_le32(
					DMA_CTL_TTFC(DMA_CTL_TTFC_P2M_DMAC) |
					DMA_CTL_SMS(0) |
					DMA_CTL_DMS(1) |
					DMA_CTL_SRC_MSIZE(bl) |
					DMA_CTL_DST_MSIZE(bl) |
					DMA_CTL_SINC_NOCHANGE |
					DMA_CTL_SRC_TRWID(2) |
					DMA_CTL_DST_TRWID(2) |
					DMA_CTL_INT_EN |
					DMA_CTL_LLP_SRCEN |
					DMA_CTL_LLP_DSTEN);
			} else {	/* DMA_TO_DEVICE */
				lli[idx].sar = cpu_to_le32(addr);
				lli[idx].dar = cpu_to_le32((u32)dmadr_addr);

				lli[idx].ctl.low = cpu_to_le32(
					DMA_CTL_TTFC(DMA_CTL_TTFC_M2P_PER) |
					DMA_CTL_SMS(1) |
					DMA_CTL_DMS(0) |
					DMA_CTL_SRC_MSIZE(bl) |
					DMA_CTL_DST_MSIZE(bl) |
					DMA_CTL_DINC_NOCHANGE |
					DMA_CTL_SRC_TRWID(2) |
					DMA_CTL_DST_TRWID(2) |
					DMA_CTL_INT_EN |
					DMA_CTL_LLP_SRCEN |
					DMA_CTL_LLP_DSTEN);
			}
#endif
			dwc_port_vdbg(qc->ap, "%s setting ctl.high len: 0x%08x val: "
					"0x%08x\n", __func__, len,
					DMA_CTL_BLK_TS(len / 4));

			/* Program the LLI CTL high register */
			dma_ts = DMA_CTL_BLK_TS(len / 4);
			lli[idx].ctl.high = cpu_to_le32(dma_ts);

			/*
			 *Program the next pointer.  The next pointer must be
			 * the physical address, not the virtual address.
			 */
			next_llp = (dma_lli + ((idx + 1) * sizeof(struct lli)));

			/* The last 2 bits encode the list master select. */
#if defined(CONFIG_APM82181)
			next_llp = DMA_LLP_LMS(next_llp, DMA_LLP_AHBMASTER1);
#else
			next_llp = DMA_LLP_LMS(next_llp, DMA_LLP_AHBMASTER2);
#endif

			lli[idx].llp = cpu_to_le32(next_llp);

			dwc_port_vdbg(qc->ap, "%s: index %d\n", __func__, idx);
			dwc_port_vdbg(qc->ap, "%s setting ctl.high with val: 0x%08x\n", __func__, lli[idx].ctl.high);
			dwc_port_vdbg(qc->ap, "%s setting ctl.low with val: 0x%08x\n", __func__, lli[idx].ctl.low);
			dwc_port_vdbg(qc->ap, "%s setting lli.dar with val: 0x%08x\n", __func__, lli[idx].dar);
			dwc_port_vdbg(qc->ap, "%s setting lli.sar with val: 0x%08x\n", __func__, lli[idx].sar);
			dwc_port_vdbg(qc->ap, "%s setting next_llp with val: 0x%08x\n", __func__, lli[idx].llp);

			idx++;
			sg_len -= len;
			addr += len;
		}
	}

	/*
	 * The last next ptr has to be zero and the last control low register
	 * has to have LLP_SRC_EN and LLP_DST_EN (linked list pointer source
	 * and destination enable) set back to 0 (disabled.)  This is what tells
	 * the core that this is the last item in the linked list.
	 */
	if (likely(idx)) {
		lli[idx-1].llp = 0x00000000;
		lli[idx-1].ctl.low &= DMA_CTL_LLP_DISABLE_LE32;

		/* Flush cache to memory */
		dma_cache_sync(NULL, lli, (sizeof(struct lli) * idx),
			       DMA_BIDIRECTIONAL);
	}

	dwc_port_vdbg(qc->ap, "%s: Final index %d\n", __func__, idx-1);
	dwc_port_vdbg(qc->ap, "%s setting ctl.high with val: 0x%08x\n", __func__, lli[idx-1].ctl.high);
	dwc_port_vdbg(qc->ap, "%s setting ctl.low with val: 0x%08x\n", __func__, lli[idx-1].ctl.low);
	dwc_port_vdbg(qc->ap, "%s setting lli.dar with val: 0x%08x\n", __func__, lli[idx-1].dar);
	dwc_port_vdbg(qc->ap, "%s setting lli.sar with val: 0x%08x\n", __func__, lli[idx-1].sar);
	dwc_port_vdbg(qc->ap, "%s setting next_llp with val: 0x%08x\n", __func__, lli[idx-1].llp);

	return idx;
}


/**
 *  Configure DMA channel registers ready for data transfer
 */
static void dwc_dma_chan_config(int dma_ch, dma_addr_t dma_lli) {
	/* Clear channel interrupts */
	dwc_dma_clear_chan_intpr(dma_ch);
	
	/* Program the CFG register. */
#if defined(CONFIG_APM82181)
	if (dma_ch == 0) {
		/* Buffer mode enabled, FIFO_MODE=0 */
		out_le32(&(sata_dma_regs->chan_regs[dma_ch].cfg.high), 0x000000d);
		/* Channel 0 bit[7:5] */
		out_le32(&(sata_dma_regs->chan_regs[dma_ch].cfg.low), 0x00000020);
	} else if (dma_ch == 1) {
		/* Buffer mode enabled, FIFO_MODE=0 */
		out_le32(&(sata_dma_regs->chan_regs[dma_ch].cfg.high), 0x0000088d);
		/* Channel 1 bit[7:5] */
		out_le32(&(sata_dma_regs->chan_regs[dma_ch].cfg.low), 0x00000020);
	}
#else
	out_le32(&(sata_dma_regs->chan_regs[dma_ch].cfg.high),
		 DMA_CFG_PROTCTL | DMA_CFG_FCMOD_REQ);
	out_le32(&(sata_dma_regs->chan_regs[dma_ch].cfg.low), 0);
#endif

	/* Program the address of the linked list */
#if defined(CONFIG_APM82181)
	out_le32(&(sata_dma_regs->chan_regs[dma_ch].llp.low),
                 DMA_LLP_LMS(dma_lli, DMA_LLP_AHBMASTER1));
#else
	out_le32(&(sata_dma_regs->chan_regs[dma_ch].llp.low),
		 DMA_LLP_LMS(dma_lli, DMA_LLP_AHBMASTER2));
#endif

	/* Program the CTL register with src enable / dst enable */
	out_le32(&(sata_dma_regs->chan_regs[dma_ch].ctl.low),
		 DMA_CTL_LLP_SRCEN | DMA_CTL_LLP_DSTEN);
	//out_le32(&(sata_dma_regs->chan_regs[dma_ch].ctl.low), 0x18000000);
}

/*
 * Check if the selected DMA channel is currently enabled.
 * 
 * RETURNS:
 *  1 if it is currently in use. 0 otherwise
 */
static int dwc_dma_chk_en(int ch)
{
	u32 dma_chan_reg;

	// Read the DMA channel register
	dma_chan_reg = in_le32(&(sata_dma_regs->dma_chan_en.low));
	if (dma_chan_reg & DMA_CHANNEL(ch))
		return 1;

	return 0;
}

/** 
 *  dwc_dma_terminate - Terminate the current 
 *  @ap: port that DMA channel need to terminate
 *  @dma_ch: DMA channel to terminate
 *
 *  Refer to the "Abnormal Transfer Termination" section
 *  Disable the corresponding bit in the ChEnReg register
 *  and poll that register to until the channel is terminated.
 *
 *  RETURNS: none
 */
static void dwc_dma_terminate(struct ata_port *ap, int dma_ch)
{
	int enabled = dwc_dma_chk_en(dma_ch);

	// If the channel is currenly in use, disable it.
	if (enabled)  {
		dwc_port_dbg(ap, "%s terminate DMA on channel=%d (mask=0x%08x) ...", __func__, dma_ch, DMA_DISABLE_CHAN(dma_ch));
		dwc_port_dbg(ap, "ChEnReg=0x%08x\n", in_le32(&(sata_dma_regs->dma_chan_en.low)));
		// Disable the selected channel
		out_le32(&(sata_dma_regs->dma_chan_en.low),
			DMA_DISABLE_CHAN(dma_ch));

		// Wait for the channel is disabled
		do {
			enabled = dwc_dma_chk_en(dma_ch);
			ndelay(1000);
		} while (enabled);
		dwc_port_dbg(ap, "done\n");
	}
}

/*
 * Function: dwc_dma_exit
 * arguments: None
 * returns status
 * This function exits the SATA DMA driver
 */
static void dwc_dma_exit(struct sata_dwc_device *hsdev)
{
	dwc_dev_vdbg(hsdev->dev, "%s:\n", __func__);
	if (sata_dma_regs)
		iounmap(sata_dma_regs);

	if (hsdev->irq_dma)
		free_irq(hsdev->irq_dma, hsdev);
}

/*
 * Function: dwc_dma_init
 * arguments: hsdev
 * returns status
 * This function initializes the SATA DMA driver
 */
static int dwc_dma_init(struct sata_dwc_device *hsdev)
{
	int err;
	int irq = hsdev->irq_dma;

	err = dwc_dma_request_intpr(hsdev, irq);
	if (err) {
		dev_err(hsdev->dev, "%s: dwc_dma_request_intpr returns %d\n",
			__func__, err);
		goto error_out;
	}

	/* Enabe DMA */
	out_le32(&(sata_dma_regs->dma_cfg.low), DMA_EN);

	dev_notice(hsdev->dev, "DMA initialized\n");
	dev_notice(hsdev->dev, "DMA CFG = 0x%08x\n", in_le32(&(sata_dma_regs->dma_cfg.low)));
	dwc_dev_vdbg(hsdev->dev, "SATA DMA registers=0x%p\n", sata_dma_regs);

	return 0;

error_out:
	dwc_dma_exit(hsdev);

	return err;
}

/**
 *  dwc_dev_config - Configure the device at first plugged
 *  adev: Device to be configured
 *
 *  - Enable assynchronous notification
 *  - Disable NCQ on PMP transfer.
 */
static void dwc_dev_config(struct ata_device *adev)
{
	if (adev->flags & ATA_DFLAG_NCQ) {
		// Disable NCQ since it is not stable now
		adev->flags &= ~ATA_DFLAG_NCQ;
		/*
		 * Our hardware support PMP command based switching. This causes NCQ can not work over
		 * PMP transfer. However, the sata_pmp_qc_defer_cmd_switch function has also deferred
		 * more link to be executed. So, no need to disable the ATA_FLAG_NCQ here
		 */
		/*if (sata_pmp_attached(adev->link->ap)) {
			adev->flags &= ~ATA_DFLAG_NCQ;
			ata_dev_printk(adev, KERN_INFO,
				"NCQ disabled for command-based switching\n");
		}*/
	}

	/*
	 * Since the sata_pmp_error_handler function in libata-pmp 
	 * make FLAG_AN disabled in the first time SATA port is configured.
	 * Asynchronous notification is not configured.
	 * This will enable the AN feature manually.
	 */
	adev->flags |= ATA_DFLAG_AN;
}


static int dwc_scr_read(struct ata_link *link, unsigned int scr, u32 *val)
{
	if (unlikely(scr > SCR_NOTIFICATION)) {
		dev_err(link->ap->dev, "%s: Incorrect SCR offset 0x%02x\n",
				__func__, scr);
		return -EINVAL;
	}

	*val = in_le32((void *)link->ap->ioaddr.scr_addr + (scr * 4));
	dwc_dev_vdbg(link->ap->dev, "%s: id=%d reg=%d val=val=0x%08x\n",
		__func__, link->ap->print_id, scr, *val);

	return 0;
}

static int dwc_scr_write(struct ata_link *link, unsigned int scr, u32 val)
{
	dwc_dev_vdbg(link->ap->dev, "%s: id=%d reg=%d val=val=0x%08x\n",
		__func__, link->ap->print_id, scr, val);
	if (unlikely(scr > SCR_NOTIFICATION)) {
		dev_err(link->ap->dev, "%s: Incorrect SCR offset 0x%02x\n",
				__func__, scr);
		return -EINVAL;
	}
	out_le32((void *)link->ap->ioaddr.scr_addr + (scr * 4), val);

	return 0;
}

static inline u32 dwc_core_scr_read ( struct ata_port *ap, unsigned int scr)
{
	struct sata_dwc_device *hsdev = HSDEV_FROM_AP(ap);
	return in_le32((void __iomem *)hsdev->scr_base + (scr * 4));
}


static inline void dwc_core_scr_write ( struct ata_port *ap, unsigned int scr, u32 val)
{
	struct sata_dwc_device *hsdev = HSDEV_FROM_AP(ap);
	out_le32((void __iomem *)hsdev->scr_base + (scr * 4), val);
}

static inline void dwc_clear_serror(struct ata_port *ap)
{
	struct sata_dwc_device *hsdev = HSDEV_FROM_AP(ap);
	out_le32( (void __iomem *)hsdev->scr_base + 4,
		in_le32((void __iomem *)hsdev->scr_base + 4));
}

static inline void dwc_clear_intpr(struct sata_dwc_device *hsdev)
{
	out_le32(&hsdev->sata_dwc_regs->intpr,
		 in_le32(&hsdev->sata_dwc_regs->intpr));
}

static inline void dwc_clear_intpr_bit(struct sata_dwc_device *hsdev, u32 bit)
{
	out_le32(&hsdev->sata_dwc_regs->intpr, bit);
}

/*
 * dwc_an_chk - check Assynchronous Notification Register
 *
 * Timer to monitor SCR_NOTIFICATION registers on the 
 * SATA port periodly.
 *
 */
static void dwc_an_chk(unsigned long arg)
{
	struct ata_port *ap = (struct ata_port *)arg;
	struct sata_dwc_device *hsdev = HSDEV_FROM_AP(ap);
	unsigned long flags;
	int rc = 0x0;
	u32 sntf = 0x0;

	spin_lock_irqsave(ap->lock, flags);
	rc = sata_scr_read(&ap->link, SCR_NOTIFICATION, &sntf);

	// If some changes on the SCR4, call asynchronous notification
	if ( (rc == 0) && (sntf  != 0)) {
		dwc_port_dbg(ap, "Call assynchronous notification sntf=0x%08x\n", sntf);
		printk("Call assynchronous notification sntf=0x%08x\n", sntf);
		sata_async_notification(ap);
		hsdev->an_timer.expires = jiffies + msecs_to_jiffies(3000);
	} else {
		hsdev->an_timer.expires = jiffies + msecs_to_jiffies(1000);
	}
	add_timer(&hsdev->an_timer);
	spin_unlock_irqrestore(ap->lock, flags);
}


/**
 *  dwc_pmp_select - Set the PMP field in SControl to the specified port number.
 *  @ap: ATA port to set the PMP field to.
 *  @port: PMP port
 *  
 *  RETURNS:
 *  The old value of the PMP field.
 */
static u32 dwc_pmp_select(struct ata_port *ap, u32 port)
{
	u32 scontrol, old_port;
	if (sata_pmp_supported(ap)) {
		scontrol = dwc_core_scr_read(ap, SCR_CONTROL);
		old_port = SCONTROL_TO_PMP(scontrol);

		// Select new PMP port
		if ( port != old_port )  {
			scontrol &= ~SCONTROL_PMP_MASK;
			dwc_core_scr_write(ap, SCR_CONTROL, scontrol | PMP_TO_SCONTROL(port));
			dwc_port_vdbg(ap, "%s: old port=%d new port=%d\n", __func__, old_port, port);
		}
		return old_port;
	} 
	else
		return port;
}

/*
 * Get the current PMP port
 */
static inline u32 dwc_current_pmp(struct ata_port *ap)
{
	return SCONTROL_TO_PMP(dwc_core_scr_read(ap, SCR_CONTROL));
}


/* 
 * Process when a PMP card is attached in the SATA port.
 * @ap: ATA port
 *
 * Add timer to check the SNotification register to see if
 * any device is plugged or unplugged on the PMP port.
 *
 */
static void dwc_pmp_attach ( struct ata_port *ap)
{
	struct sata_dwc_device *hsdev = HSDEV_FROM_AP(ap);

	dev_info(ap->dev, "Attach SATA port multiplier with %d ports\n", ap->nr_pmp_links);

	// Initialize timer for checking AN
	init_timer(&hsdev->an_timer);
	hsdev->an_timer.expires = jiffies + msecs_to_jiffies(20000);
	hsdev->an_timer.function = dwc_an_chk;
	hsdev->an_timer.data = (unsigned long)(ap);
	add_timer(&hsdev->an_timer);
}

/*
 * Process when PMP card is removed from the SATA port.
 * Re-enable NCQ for using by the SATA drive in the future
 */
static void dwc_pmp_detach ( struct ata_port *ap)
{
	struct sata_dwc_device *hsdev = HSDEV_FROM_AP(ap);

	dev_info(ap->dev, "Detach SATA port\n");
	dwc_pmp_select(ap, 0);

	// Delete timer since PMP card is detached
	del_timer(&hsdev->an_timer);
}

/*
 * Porting the ata_bus_softreset function from the libata-sff.c library.
 */
static int dwc_bus_softreset(struct ata_port *ap, unsigned int devmask,
			     unsigned long deadline)
{
	struct ata_ioports *ioaddr = &ap->ioaddr;

	DPRINTK("ata%u: bus reset via SRST\n", ap->print_id);

	/* software reset.  causes dev0 to be selected */
	iowrite8(ap->ctl, ioaddr->ctl_addr);
	udelay(20);	/* FIXME: flush */
	iowrite8(ap->ctl | ATA_SRST, ioaddr->ctl_addr);
	udelay(20);	/* FIXME: flush */
	iowrite8(ap->ctl, ioaddr->ctl_addr);
	ap->last_ctl = ap->ctl;

	/* wait the port to become ready */
	return ata_sff_wait_after_reset(&ap->link, devmask, deadline);
}

/*
 * Do soft reset on the current SATA link.
 * It first set the PMP value, then porting the ata_sff_softreset function
 * from the libata-sff.c library.
 */
static int dwc_softreset(struct ata_link *link, unsigned int *classes,
				unsigned long deadline)
{
	int rc = 0;
	u8 err;
	struct ata_port *ap = link->ap;
	unsigned int devmask = 0;
	int pmp = sata_srst_pmp(link);
	struct sata_dwc_device *hsdev = HSDEV_FROM_AP(ap);

	dwc_link_info(link, "Softreset on PMP port %d\n", pmp);
	// Select active PMP port
	dwc_pmp_select(link->ap, sata_srst_pmp(link));
	
	DPRINTK("ENTER\n");
	//rc = ata_sff_softreset(link, classes, deadline);

	/* select device 0 again */
	ap->ops->sff_dev_select(ap, 0);

	/* issue bus reset */
	DPRINTK("about to softreset, devmask=%x\n", devmask);
	rc = dwc_bus_softreset(ap, devmask, deadline);
	/* if link is occupied, -ENODEV too is an error */
	if (rc && (rc != -ENODEV || sata_scr_valid(link))) {
		ata_link_printk(link, KERN_ERR, "SRST failed (errno=%d)\n", rc);
		return rc;
	}

	/* determine by signature whether we have ATA or ATAPI devices */
	classes[0] = ata_sff_dev_classify(&link->device[0],
					  devmask & (1 << 0), &err);
	DPRINTK("EXIT, classes[0]=%u [1]=%u\n", classes[0], classes[1]);
	
	dwc_clear_serror(link->ap);
	// Terminate DMA if it is currently in use
	dwc_dma_terminate(link->ap, hsdev->dma_channel);

	return rc;
}

/**
 *  dwc_reset_internal_params - reset internal parameters
 *  @ap: port to reset
 *
 *  Reset all internal parameters to default values. It is needed
 *  when SATA transfers are terminated abnormally.
 *
 *  RETURNS: none
 */
static void dwc_reset_internal_params(struct ata_port *ap) {
	struct sata_dwc_device_port *hsdevp = HSDEVP_FROM_AP(ap);
	int i;
	
	for ( i=0; i < DWC_QCMD_MAX; i++) {
		hsdevp->dma_pending[i] = DWC_DMA_NONE;
		//hsdevp->dma_chan[i] = -1;
	}

	hsdevp->sactive_issued = 0;
	hsdevp->sactive_queued = 0;
}

/*
 * dwc_hardreset - Do hardreset the SATA controller
 */
static int dwc_hardreset(struct ata_link *link, unsigned int *classes,
			   unsigned long deadline)
{
	int rc;
	const unsigned long *timing = sata_ehc_deb_timing(&link->eh_context);
	bool online;

	dwc_link_info(link, "%s\n", __func__);

	dwc_pmp_select(link->ap, sata_srst_pmp(link));

	// Reset all internal parameters to default
	dwc_reset_internal_params(link->ap);

	// Call standard hard reset
	rc = sata_link_hardreset(link, timing, deadline, &online, NULL);

	// Reconfigure the port after hard reset
	if ( ata_link_online(link) )
		sata_dwc_init_port(link->ap);

	return online ? -EAGAIN : rc;
}

/*
 * Do hard reset on each PMP link
 */
static int dwc_pmp_hardreset(struct ata_link *link, unsigned int *classes,
			   unsigned long deadline)
{
	int rc = 0;
	int pmp = sata_srst_pmp(link);
	dwc_link_info(link, "Hardreset on PMP port %d\n", pmp);
	dwc_pmp_select(link->ap, sata_srst_pmp(link));
	rc = sata_std_hardreset(link, classes, deadline);
	return rc;
}

/* See ahci.c */
/*
 * Process error when the SATAn_INTPR's ERR bit is set
 * The processing is based on SCR_ERROR register content
 */
static void dwc_error_intr(struct ata_port *ap,
				struct sata_dwc_device *hsdev, uint intpr)
{
	struct ata_eh_info *ehi = &ap->link.eh_info;
	struct ata_link *link = &ap->link;
	struct ata_queued_cmd *active_qc = NULL;
	u32 serror;
	u8 status = ap->ops->sff_check_altstatus(ap);;
	bool freeze = false, abort = false;
	int ret, pmp;
	unsigned int err_mask = 0, action = 0;

	// Read value of SERROR
	serror = dwc_core_scr_read(ap, SCR_ERROR);
	dwc_port_dbg(ap, "%s serror = 0x%08x\n", __func__, serror);

	// Clear SERROR and interrupt bit
	dwc_clear_serror(ap);
	dwc_clear_intpr(hsdev);

	// Print out for test only
	if ( serror )
		dwc_err_2_txt(serror);

	/* Record irq stat */
	active_qc = dwc_get_active_qc(ap);
	ata_ehi_clear_desc(ehi);
	ata_ehi_push_desc(ehi, "irq_stat 0x%08x, serror 0x%08x", intpr, serror);

#if defined(CONFIG_SATA_DWC_VDEBUG)
	dwc_dma_chan_display(hsdev->dma_channel);
#endif

	// Process hotplug for SATA port
	if ( serror & (DWC_SERR_DIAGX | DWC_SERR_DIAGW)) {
		ata_ehi_hotplugged(ehi);
		ata_ehi_push_desc(ehi, serror & DWC_SERR_DIAGN ? "PHY RDY changed" : "device exchanged");
		freeze = 1;
	}

	// Work on PMP link if the PMP card has been attached
	if (sata_pmp_attached(ap)) {
		pmp = dwc_current_pmp(ap);
		if (pmp < ap->nr_pmp_links) {
			link = &ap->pmp_link[pmp];
			ehi = &link->eh_info;
			ata_ehi_clear_desc(ehi);
			ata_ehi_push_desc(ehi, "irq_stat 0x%08x, serror 0x%08x", intpr, serror);
			//active_qc = ata_qc_from_tag(ap, link->active_tag);
		}
		else {
			err_mask |= AC_ERR_HSM;
			action |= ATA_EH_RESET;
			freeze = 1;
		}
	}
	
	// Process Internal host adapter error
	if ( serror & DWC_SERR_ERRE ) {
		ata_ehi_push_desc(ehi, "internal host adapter error");
		err_mask |= AC_ERR_SYSTEM;
		action   |= ATA_EH_RESET;
		freeze = 1;
	}

	// Process protocol error
	if ( serror & DWC_SERR_ERRP ) {
		ata_ehi_push_desc(ehi, "protocol error");
		err_mask |= AC_ERR_HSM;
		action |= ATA_EH_RESET;
		freeze = 1;
	}

	if ( serror & DWC_SERR_ERRC ) {
		ata_ehi_push_desc(ehi, "persistent communication error");
		err_mask |= AC_ERR_ATA_BUS;
		action |= ATA_EH_RESET;
		abort = 1;
	}
	// Data integrity error, abort that qc
	if ( serror & (DWC_SERR_ERRT | DWC_SERR_DIAGC) ) {
		ata_ehi_push_desc(ehi, "data integrity error");
		err_mask |= AC_ERR_ATA_BUS;
		action |= ATA_EH_RESET;
		abort = 1;
	}

	//ehi->serror |= serror;
	ehi->action |= action;

	if ( active_qc) {
		active_qc->err_mask |= err_mask;
	} else {
		ehi->err_mask = err_mask;
	}

	if ( status & ATA_BUSY ) {
		//dwc_port_info(ap, "%s -- ATA_BUSY -- \n", __func__);
		ndelay(100);
	}

	// If ATA port needs to be freezed --> hard reset will be done on all links
	if ( freeze ) {
		ret = ata_port_freeze(ap);
		ata_port_printk(ap, KERN_INFO, "Freeze port with %d QCs aborted\n", ret);
	}
	// If we need only to abort current QCs
	else if (abort) {
		// Abort all QCs on the link
		if (active_qc) {
			ret = ata_link_abort(active_qc->dev->link);
			ata_link_printk(link, KERN_INFO, "Abort %d QCs\n", ret);
			dwc_dma_terminate(ap, hsdev->dma_channel);
		}
		// Abort all QCs on the port
		else {
			ret = ata_port_abort(ap);
			ata_port_printk(ap, KERN_INFO, "Abort %d QCs on the SATA port\n", ret);
		}
	}
}


/*
 * Function : sata_dwc_isr
 * arguments : irq, void *dev_instance, struct pt_regs *regs
 * Return value : irqreturn_t - status of IRQ
 * This Interrupt handler called via port ops registered function.
 * .irq_handler = sata_dwc_isr
 */
static irqreturn_t sata_dwc_isr(int irq, void *dev_instance)
{
	struct ata_host *host = (struct ata_host *)dev_instance;
	struct sata_dwc_device *hsdev = HSDEV_FROM_HOST(host);
	struct ata_port *ap;
	struct ata_queued_cmd *qc;
	unsigned long flags;
	u8 status, tag;
	int handled, rc, port = 0;
	u32 intpr, sactive, tag_mask;
	u32 sntf;
	struct sata_dwc_device_port *hsdevp;

	spin_lock_irqsave(&host->lock, flags);

	/* Read the interrupt register */
	intpr = in_le32(&hsdev->sata_dwc_regs->intpr);

	ap = host->ports[port];
	hsdevp = HSDEVP_FROM_AP(ap);

	dwc_port_vdbg(ap, "%s: INTMR=0x%08x, ERRMR=0x%08x\n", __func__, in_le32(&hsdev->sata_dwc_regs->intmr), in_le32(&hsdev->sata_dwc_regs->errmr));
	if ( intpr & (~(DWC_INTPR_IPF | DWC_INTPR_CMDGOOD | DWC_INTPR_CMDABORT | DWC_INTPR_NEWFP))) {
		dwc_port_dbg(ap, "%s intpr=0x%08x active_tag=0x%0x\n", __func__, intpr, ap->link.active_tag);
	}

	// Check if any change on the SATA PMP
	status = ap->ops->sff_check_altstatus(ap);
	rc = sata_scr_read(&ap->link, SCR_NOTIFICATION, &sntf);
	if ( (rc == 0) && (sntf  != 0)) {
		dwc_port_info(ap, "%s - Call assynchronous notification sntf=0x%08x\n", __func__, sntf);
		sata_async_notification(ap);
	}
/*	if ( status & ATA_BUSY ) {
		dwc_port_info(ap, "%s - ATA_BUSY\n", __func__);
		//handled = 1;
		//goto done_irqrestore;
	}*/

	// Check for error interrupt, if any error, the ERR bit will enable
	// --> call error interrupt handling for parsing error before sending
	// it to error handler
	if (intpr & DWC_INTPR_ERR) {
		dwc_error_intr(ap, hsdev, intpr);
		handled = 1;
		goto done_irqrestore;
	}

	/*
	 * Check for DMA SETUP FIS (FP DMA) interrupt - NCQ command only
	 * With non-NCQ command, this interrupt will never occur.
	 * This is step 5 of the First Party DMA transfer
	 */
	if (intpr & DWC_INTPR_NEWFP) {
		dwc_port_dbg(ap, "%s: NEWFP INTERRUPT in HSDEV with DMA channel %d\n", __func__, hsdev->dma_channel);
		// Clear interrupt
		dwc_clear_intpr_bit(hsdev, DWC_INTPR_NEWFP);
		if ( ap->qc_allocated == 0x0 ) {
			handled = 1;
			goto done_irqrestore;
		}
		// Read the FPTAGR register for the NCQ tag
		tag = (u8)(in_le32(&hsdev->sata_dwc_regs->fptagr));
		dwc_port_dbg(ap, "%s: NEWFP tag=%d\n", __func__, tag);

		/*
		 * Start FP DMA for NCQ command.  At this point the tag is the
		 * active tag.  It is the tag that matches the command about to
		 * be completed.
		 */
		qc = ata_qc_from_tag(ap, tag);
		if ( qc ) {
			// Prevent issue more commands
			ap->link.active_tag = tag;
			hsdevp->sactive_issued |= dwc_qctag_to_mask(tag);
			dwc_bmdma_start_by_tag(qc, tag);
			qc->ap->hsm_task_state = HSM_ST_LAST;
		} else {
			hsdevp->sactive_issued &= ~dwc_qctag_to_mask(tag);
			dev_warn(ap->dev, "No QC available for tag %d (intpr=0x%08x, qc_allocated=0x%08x, qc_active=0x%08x)\n", tag, intpr, ap->qc_allocated, ap->qc_active);
		}
		handled = 1;
		goto done_irqrestore;
	}

	/*
	 * Process non-NCQ command interrupt, signal when QC complete
	 */
	sactive = dwc_core_scr_read(ap, SCR_ACTIVE);
	tag_mask = (hsdevp->sactive_issued | sactive) ^ sactive;
	/* If no sactive issued and tag_mask is zero then this is not NCQ */
	if (hsdevp->sactive_issued == 0 && tag_mask == 0) {
		dwc_port_dbg(ap, "%s: none-NCQ interrupt\n", __func__);
		// Get status and clear interrupt
		status = ap->ops->sff_check_status(ap);

	

		// Get the active QC to be processed
		qc = dwc_get_active_qc(ap);
		// DEV interrupt with no active qc --> warning and bypass
		if (unlikely(!qc)) {
			dwc_port_dbg(ap, "%s intr with no active qc qc=%p\n",
				__func__, qc);
			handled = 1;
			goto done_irqrestore;
		}
		dwc_dev_dbg(ap->dev, "%s non-NCQ cmd interrupt, protocol: %s\n",
			__func__, prot_2_txt(qc->tf.protocol));

drv_still_busy:
		if (ata_is_dma(qc->tf.protocol)) {
			// Complete the command when the SATA controller operation
			// done interrupt occurs.
			dwc_current_qc_complete(ap, 1);
		} else if ( (ata_is_pio(qc->tf.protocol)) ||
				(ata_is_nodata(qc->tf.protocol)) ) {
			//LGNAS 20110524 omw for handling atapi no data
			ata_sff_hsm_move(ap, qc, status, 0);
		} else {
			if (unlikely(dwc_qc_complete(ap, qc, 1))) {
				dwc_port_info(ap, "%s - unlikely call QC complete\n",__func__);
				goto drv_still_busy;
			}
		}

		handled = 1;
		goto done_irqrestore;
	}

	/*
	 * This is a NCQ command.  At this point we need to figure out for which
	 * tags we have gotten a completion interrupt.  One interrupt may serve
	 * as completion for more than one operation when commands are queued
	 * (NCQ).  We need to process each completed command.
	 */
	sactive = dwc_core_scr_read(ap, SCR_ACTIVE);
	tag_mask = (hsdevp->sactive_issued | sactive) ^ sactive;
	if (sactive != 0 || hsdevp->sactive_issued > 1 || tag_mask > 1) {
		dwc_port_dbg(ap, "%s NCQ: sactive=0x%08x  sactive_issued=0x%08x"
			" tag_mask=0x%08x\n", __func__, sactive,
			hsdevp->sactive_issued, tag_mask);
	}

	if (unlikely((tag_mask | hsdevp->sactive_issued) != hsdevp->sactive_issued)) {
		dev_warn(ap->dev, "Bad tag mask?  sactive=0x%08x "
			 "sactive_issued=0x%08x  tag_mask=0x%08x\n",
			 sactive, hsdevp->sactive_issued, tag_mask);
	}

	// Read just to clear interrupt ... not bad if currently still busy
	status = ap->ops->sff_check_status(ap);
	dwc_port_dbg(ap, "%s process NCQ - status=0x%x, tag_mask=0x%x\n", __func__, status, tag_mask);

	for(tag=0; tag<32; tag++) {
		if ( tag_mask & dwc_qctag_to_mask(tag) ) {
			qc = ata_qc_from_tag(ap, tag);
			if ( ! qc ) {
				dwc_port_info(ap, "error: Tag %d is set but not available\n", tag);
				continue;
			}
			// FIXME: should review this carefully to avoid uncompleted job.
			dwc_qc_complete(ap, qc, 1);
		}
	}
	handled = 1;

done_irqrestore:
	spin_unlock_irqrestore(&host->lock, flags);
	return IRQ_RETVAL(handled);
}


/*
 * Clear DMA Control Register after completing transferring data
 * using AHB DMA.
 */
static void dwc_dma_clear_ctrlreg(struct sata_dwc_device_port *hsdevp, u8 tag)
{
	struct sata_dwc_device *hsdev = HSDEV_FROM_HSDEVP(hsdevp);

	if (hsdevp->dma_pending[tag] == DWC_DMA_PENDING_RX) {
		// Clear receive channel enable bit
		out_le32(&(hsdev->sata_dwc_regs->dmacr),
			 DWC_DMACR_RX_CLEAR(
				 in_le32(&(hsdev->sata_dwc_regs->dmacr))));
	} else if (hsdevp->dma_pending[tag] == DWC_DMA_PENDING_TX) {
		// Clear transmit channel enable bit
		out_le32(&(hsdev->sata_dwc_regs->dmacr),
			 DWC_DMACR_TX_CLEAR(
				 in_le32(&(hsdev->sata_dwc_regs->dmacr))));
	} else {
		/*
		 * This should not happen, it indicates the driver is out of
		 * sync.  If it does happen, clear dmacr anyway.
		 */
		dwc_dev_dbg(hsdev->dev, "%s DMA protocol RX and TX DMA not pending "
			"tag=0x%02x pending=%d dmacr: 0x%08x\n",
			__func__, tag, hsdevp->dma_pending[tag],
			in_le32(&(hsdev->sata_dwc_regs->dmacr)));

		// Clear all transmit and receive bit, but TXMOD bit is set to 1
		out_le32(&(hsdev->sata_dwc_regs->dmacr),
				DWC_DMACR_TXRXCH_CLEAR);
	}
}


/**
 *  dwc_qc_complete - complete the QC.
 *  @ap: ATA port
 *  @qc: command queue to be completed
 *  @check_status: check device status or not.
 *
 *  Do some post processing and (if required) check device
 *  status. Then call ata_qc_complete to complete the QC.
 *
 *  RETURNS:
 *   0 if successful. 1 for device busy.
 */
static int dwc_qc_complete(struct ata_port *ap, struct ata_queued_cmd *qc,
				u32 check_status)
{
	u8 status = 0;
	int i = 0;
	u8 tag = qc->tag;
	struct sata_dwc_device_port *hsdevp = HSDEVP_FROM_AP(ap);
	struct sata_dwc_device *hsdev = HSDEV_FROM_AP(ap);
	u32 serror;

	dwc_port_vdbg(ap, "%s checkstatus? %s\n", __func__, check_status? "yes" : "no");
	if (check_status) {
		i = 0;
		do {
			/* check main status, not clearing INTRQ */
			status = ap->ops->sff_check_altstatus(ap);
			if (status & ATA_BUSY) {
				dwc_port_vdbg(ap, "STATUS BUSY (0x%02x) [%d]\n",
						status, i);
			}
			if (++i > 10) break;
		} while (status & ATA_BUSY);

		status = ap->ops->sff_check_altstatus(ap);
		if (unlikely(status & ATA_BUSY)) {
			dwc_port_info(ap, "QC complete cmd=0x%02x STATUS BUSY "
				"(0x%02x) [%d]\n", qc->tf.command, status, i);
			return 1;
		}

		// Check error ==> need to process error here
		serror = dwc_core_scr_read(ap, SCR_ERROR);
		if (unlikely(serror & DWC_SERR_ERR_BITS))
		{
			dev_err(ap->dev, "****** SERROR=0x%08x ******\n", serror);
			ap->link.eh_context.i.action |= ATA_EH_RESET;
			dwc_dma_terminate(ap, hsdevp->dma_chan[tag]);
		}
	}
	dwc_port_vdbg(ap, "QC complete cmd=%s status=0x%02x ata%u: "
		"protocol=%d\n", ata_cmd_2_txt(&qc->tf), status, ap->print_id,
		qc->tf.protocol);

	// Complete taskfile transaction (does not read SCR registers)
	if (qc->tf.protocol == ATAPI_PROT_DMA) {
		ata_sff_hsm_move(ap, qc, status, 0);
	} else {
		ata_qc_complete(qc);
	}

	/* clear active bit */
	hsdevp->sactive_issued &= ~dwc_qctag_to_mask(tag);
	dwc_port_vdbg(ap, "%s sactive_issued=0x%08x\n",__func__, hsdevp->sactive_issued);
	dwc_port_vdbg(ap, "dmacr=0x%08x\n",in_le32(&(hsdev->sata_dwc_regs->dmacr)));

	// If no QC remain, return active_tag to ATA_TAG_POISON so that new QC will be submitted
	if ( hsdevp->sactive_queued == 0)
		ap->link.active_tag = ATA_TAG_POISON;
	return 0;
}

/**
 *  Complete the current QC in ATA port.
 *  @ap: port to complete current QC
 *  @check_status: check the status
 *
 *  Get the current QC and then complete it. The QC can be got
 *  by checking the active_tag from ATA port.
 */
static void dwc_current_qc_complete(struct ata_port *ap, u32 check_status)
{
	struct ata_queued_cmd *qc = dwc_get_active_qc(ap);

	if ( !qc) {
		dwc_port_dbg(ap, "%s - no QC available\n", __func__);
		return;
	}
	
	// Call function to complete the QC.
	dwc_qc_complete(ap, qc, check_status);
}

/*
 * Clear interrupt and error flags in DMA status register.
 */
void dwc_irq_clear (struct ata_port *ap)
{
	struct sata_dwc_device *hsdev = HSDEV_FROM_AP(ap);
	dwc_port_dbg(ap,"%s\n",__func__);

	// Clear DMA interrupts
	dwc_dma_clear_chan_intpr(hsdev->dma_channel);
	//sata_dma_regs
	out_le32(&hsdev->sata_dwc_regs->intmr,
			in_le32(&hsdev->sata_dwc_regs->intmr) & ~DWC_INTPR_ERR);
	//out_le32(&hsdev->sata_dwc_regs->errmr, 0x0);
	//ap->ops->sff_check_status(ap);
}

/*
 * Turn on IRQ
 */
void dwc_irq_on(struct ata_port *ap)
{
	struct ata_ioports *ioaddr = &ap->ioaddr;
	struct sata_dwc_device *hsdev = HSDEV_FROM_AP(ap);

	dwc_port_dbg(ap,"%s\n",__func__);
	ap->ctl &= ~ATA_NIEN;
	ap->last_ctl = ap->ctl;

	if (ioaddr->ctl_addr)
		iowrite8(ap->ctl, ioaddr->ctl_addr);
	ata_wait_idle(ap);

	ap->ops->sff_irq_clear(ap);

	// Enable Error interrupt
	out_le32(&hsdev->sata_dwc_regs->intmr,
		 in_le32(&hsdev->sata_dwc_regs->intmr) | DWC_INTPR_ERR);
	out_le32(&hsdev->sata_dwc_regs->errmr, DWC_SERR_ERR_BITS);
}


/*
 * This function enables the interrupts in IMR and unmasks them in ERRMR
 * 
 */
static void dwc_enable_interrupts(struct sata_dwc_device *hsdev)
{
	// Enable interrupts
	out_le32(&hsdev->sata_dwc_regs->intmr,
		 DWC_INTPR_ERR |
		 DWC_INTPR_NEWFP |
		 DWC_INTPR_PMABRT |
		 DWC_INTPR_DMAT);

	/*
	 * Unmask the error bits that should trigger an error interrupt by
	 * setting the error mask register.
	 */
	out_le32(&hsdev->sata_dwc_regs->errmr, DWC_SERR_ERR_BITS);

	dwc_dev_dbg(hsdev->dev, "%s: INTMR = 0x%08x, ERRMR = 0x%08x\n", __func__,
		in_le32(&hsdev->sata_dwc_regs->intmr),
		in_le32(&hsdev->sata_dwc_regs->errmr));
}

/* 
 * Configure DMA and interrupts on SATA port. This should be called after
 * hardreset is executed on the SATA port.
 */
static void sata_dwc_init_port ( struct ata_port *ap ) {
	struct sata_dwc_device *hsdev = HSDEV_FROM_AP(ap);

	// Configure DMA
	if (ap->port_no == 0)  {
		dwc_dev_dbg(ap->dev, "%s: clearing TXCHEN, RXCHEN in DMAC\n",
				__func__);

		// Clear all transmit/receive bits
		out_le32(&hsdev->sata_dwc_regs->dmacr,
			 DWC_DMACR_TXRXCH_CLEAR);

		dwc_dev_dbg(ap->dev, "%s: setting burst size in DBTSR\n", __func__);
		out_le32(&hsdev->sata_dwc_regs->dbtsr,
			 (DWC_DMA_DBTSR_MWR(AHB_DMA_BRST_DFLT) |
			  DWC_DMA_DBTSR_MRD(AHB_DMA_BRST_DFLT)));
	}

	// Enable interrupts
	dwc_enable_interrupts(hsdev);
}


/*
 * Setup SATA ioport with corresponding register addresses
 */
static void dwc_setup_port(struct ata_ioports *port, unsigned long base)
{
	port->cmd_addr = (void *)base + 0x00;
	port->data_addr = (void *)base + 0x00;

	port->error_addr = (void *)base + 0x04;
	port->feature_addr = (void *)base + 0x04;

	port->nsect_addr = (void *)base + 0x08;

	port->lbal_addr = (void *)base + 0x0c;
	port->lbam_addr = (void *)base + 0x10;
	port->lbah_addr = (void *)base + 0x14;

	port->device_addr = (void *)base + 0x18;
	port->command_addr = (void *)base + 0x1c;
	port->status_addr = (void *)base + 0x1c;

	port->altstatus_addr = (void *)base + 0x20;
	port->ctl_addr = (void *)base + 0x20;
}


/*
 * dwc_port_start - 
 * @ap: ATA port
 * 
 * Allocates the scatter gather LLI table for AHB DMA
 *
 * RETURNS:
 *  0 if success, error code otherwise
 */
static int dwc_port_start(struct ata_port *ap)
{
	int err = 0;
	struct sata_dwc_device *hsdev;
	struct sata_dwc_device_port *hsdevp = NULL;
	struct device *pdev;
	u32 sstatus;
	int i;

	hsdev = HSDEV_FROM_AP(ap);

	dwc_dev_dbg(ap->dev, "%s: port_no=%d\n", __func__, ap->port_no);

	hsdev->host = ap->host;
	pdev = ap->host->dev;
	if (!pdev) {
		dev_err(ap->dev, "%s: no ap->host->dev\n", __func__);
		err = -ENODEV;
		goto cleanup_exit;
	}

	/* Allocate Port Struct */
	hsdevp = kzalloc(sizeof(*hsdevp), GFP_KERNEL);
	if (!hsdevp) {
		dev_err(ap->dev, "%s: kmalloc failed for hsdevp\n", __func__);
		err = -ENOMEM;
		goto cleanup_exit;
	}
	memset(hsdevp, 0, sizeof(*hsdevp));
	hsdevp->hsdev = hsdev;

	ap->bmdma_prd = 0;	/* set these so libata doesn't use them */
	ap->bmdma_prd_dma = 0;

	/*
	 * DMA - Assign scatter gather LLI table. We can't use the libata
	 * version since it's PRD is IDE PCI specific.
	 */
	for (i = 0; i < DWC_QCMD_MAX; i++) {
		hsdevp->llit[i] = dma_alloc_coherent(pdev,
						     DWC_DMAC_LLI_TBL_SZ,
						     &(hsdevp->llit_dma[i]),
						     GFP_ATOMIC);
		if (!hsdevp->llit[i]) {
			dev_err(ap->dev, "%s: dma_alloc_coherent failed size "
				"0x%x\n", __func__, DWC_DMAC_LLI_TBL_SZ);
			err = -ENOMEM;
			goto cleanup_exit;
		}
	}

	if (ap->port_no == 0)  {
		dwc_dev_vdbg(ap->dev, "%s: clearing TXCHEN, RXCHEN in DMAC\n",
				__func__);

		out_le32(&hsdev->sata_dwc_regs->dmacr,
			 DWC_DMACR_TXRXCH_CLEAR);

		dwc_dev_vdbg(ap->dev, "%s: setting burst size in DBTSR\n", __func__);
		out_le32(&hsdev->sata_dwc_regs->dbtsr,
			 (DWC_DMA_DBTSR_MWR(AHB_DMA_BRST_DFLT) |
			  DWC_DMA_DBTSR_MRD(AHB_DMA_BRST_DFLT)));
		 ata_port_printk(ap, KERN_INFO, "%s: setting burst size in DBTSR: 0x%08x\n", 
			__func__, in_le32(&hsdev->sata_dwc_regs->dbtsr));
	}

	/* Clear any error bits before libata starts issuing commands */
	dwc_clear_serror(ap);

	ap->private_data = hsdevp;

	/* Are we in Gen I or II */
	sstatus = dwc_core_scr_read(ap, SCR_STATUS);
	switch (DWC_SCR0_SPD_GET(sstatus)) {
	case 0x0:
		dev_info(ap->dev, "**** No neg speed (nothing attached?) \n");
		break;
	case 0x1:
		dev_info(ap->dev, "**** GEN I speed rate negotiated \n");
		break;
	case 0x2:
		dev_info(ap->dev, "**** GEN II speed rate negotiated \n");
		break;
	}

cleanup_exit:
	if (err) {
		kfree(hsdevp);
		dwc_port_stop(ap);
		dwc_dev_vdbg(ap->dev, "%s: fail\n", __func__);
	} else {
		dwc_dev_vdbg(ap->dev, "%s: done\n", __func__);
	}

	return err;
}


static void dwc_port_stop(struct ata_port *ap)
{
	int i;
	struct sata_dwc_device *hsdev = HSDEV_FROM_AP(ap);
	struct sata_dwc_device_port *hsdevp = HSDEVP_FROM_AP(ap);

	dwc_port_dbg(ap, "%s: stop port\n", __func__);

	if (hsdevp && hsdev) {
		/* deallocate LLI table */
		for (i = 0; i < DWC_QCMD_MAX; i++) {
			dma_free_coherent(ap->host->dev,
					  DWC_DMAC_LLI_TBL_SZ,
					  hsdevp->llit[i], hsdevp->llit_dma[i]);
		}

		kfree(hsdevp);
	}
	ap->private_data = NULL;
}

/**
 *  dwc_dev_select - select ATA device.
 *  Since the SATA DWC is master only. The dev select operation will 
 *  be removed.
 *  The device_addr is always 0.
 */
void dwc_dev_select(struct ata_port *ap, unsigned int device)
{
	// Do nothing
	ndelay(100);
}

/**
 *	dwc_check_atapi_dma - Filter ATAPI cmds which are unsuitable for DMA.
 *	@qc: queued command to check for chipset/DMA compatibility.
 *
 *	The bmdma engines cannot handle speculative data sizes
 *	(bytecount under/over flow).  So only allow DMA for
 *	data transfer commands with known data sizes.
 *
 *  RETURNS:
 *    0 for using DMA, 1 for PIO.
 *	LOCKING:
 *	Inherited from caller.
 */
static int dwc_check_atapi_dma(struct ata_queued_cmd *qc) {
	struct scsi_cmnd *scmd = qc->scsicmd;
	int pio = 1; /* atapi dma off by default */
	unsigned int lba;

	if (scmd) {
		switch (scmd->cmnd[0]) {
		case WRITE_6:
		case WRITE_10:
		case WRITE_12:
		case READ_6:
		case READ_10:
		case READ_12:
		//case GPCMD_READ_CD:
		//case GPCMD_SEND_DVD_STRUCTURE:
		//case GPCMD_SEND_CUE_SHEET:
			pio = 0; /* DMA is safe */
			break;
		}
		
		/* -45150 (FFFF4FA2) to -1 (FFFFFFFF) shall use PIO mode */
		if (scmd->cmnd[0] == WRITE_10) {
			lba = (scmd->cmnd[2] << 24) |
				  (scmd->cmnd[3] << 16) |
				  (scmd->cmnd[4] << 8) |
				   scmd->cmnd[5];
			if (lba >= 0xFFFF4FA2)
				pio = 1;
		}
		/*
		* WORK AROUND: Fix DMA issue when blank CD/DVD disc in the drive 
		* and user use the 'fdisk -l' command. No DMA data returned so we 
		* can not complete the QC.
		*/
		else if (scmd->cmnd[0] == READ_10) {
			lba = (scmd->cmnd[2] << 24) |
				  (scmd->cmnd[3] << 16) |
				  (scmd->cmnd[4] << 8) |
				   scmd->cmnd[5];
			if ( lba < 0x20)
				pio = 1;
		}
	}
	dwc_port_dbg(qc->ap, "%s - using %s mode for command cmd=0x%02x\n", __func__, (pio? "PIO" : "DMA"), scmd->cmnd[0]);
	return pio;
}

/**
 *  sata_dwc_exec_command_by_tag
 *  @ap:
 *  @tf:
 *  @tag:
 * 
 *  Keeps track of individual command tag ids and calls
 *  ata_exec_command in libata
 *
 *  RETURNS: none
 */
static void sata_dwc_exec_command_by_tag(struct ata_port *ap,
					 struct ata_taskfile *tf,
					 u8 tag)
{
	unsigned long flags;
	struct sata_dwc_device_port *hsdevp = HSDEVP_FROM_AP(ap);

	dwc_dev_dbg(ap->dev, "%s cmd(0x%02x): %s tag=%d, ap->link->tag=0x%08x\n", __func__, tf->command,
		ata_cmd_2_txt(tf), tag, ap->link.active_tag);

	spin_lock_irqsave(&ap->host->lock, flags);
	hsdevp->sactive_queued |= dwc_qctag_to_mask(tag);
	spin_unlock_irqrestore(&ap->host->lock, flags);

	/*
	 * Clear SError before executing a new command.
	 *
	 * TODO if we read a PM's registers now, we will throw away the task
	 * file values loaded into the shadow registers for this command.
	 *
	 * dwc_scr_write and read can not be used here. Clearing the PM
	 * managed SError register for the disk needs to be done before the
	 * task file is loaded.
	 */
	dwc_clear_serror(ap);
	ap->ops->sff_exec_command(ap, tf);
}

static void dwc_bmdma_setup(struct ata_queued_cmd *qc)
{
	u8 tag = qc->tag;

	dwc_port_vdbg(qc->ap, "%s\n", __func__);
	if (ata_is_ncq(qc->tf.protocol)) {
		dwc_dev_vdbg(qc->ap->dev, "%s: ap->link.sactive=0x%08x tag=%d\n",
			__func__, qc->ap->link.sactive, tag);
	} else {
		tag = 0;
	}
	sata_dwc_exec_command_by_tag(qc->ap, &qc->tf, tag);
}

static void dwc_bmdma_start_by_tag(struct ata_queued_cmd *qc, u8 tag)
{
	volatile int start_dma;
	u32 reg;
	struct sata_dwc_device *hsdev = HSDEV_FROM_QC(qc);
	struct ata_port *ap = qc->ap;
	struct sata_dwc_device_port *hsdevp = HSDEVP_FROM_AP(ap);
	int dma_chan;

	/* Used for ata_bmdma_start(qc) -- we are not BMDMA compatible */
	// Request DMA channel for transfer
	dma_chan = dwc_dma_request_channel(qc->ap);
	if ( dma_chan < 0) {
		dev_err(qc->ap->dev, "%s: dma channel unavailable\n", __func__);
		// Offending this QC
		qc->err_mask |= AC_ERR_TIMEOUT;
		return;
	}
	hsdevp->dma_chan[tag] = dma_chan;
	dwc_dma_chan_config(dma_chan, hsdevp->llit_dma[tag]);

	if ( hsdevp->sactive_queued & dwc_qctag_to_mask(tag)) {
		start_dma = 1;
		if (qc->dma_dir == DMA_TO_DEVICE)
			hsdevp->dma_pending[tag] = DWC_DMA_PENDING_TX;
		else
			hsdevp->dma_pending[tag] = DWC_DMA_PENDING_RX;
	} else {
		dev_err(ap->dev, "%s: No pending command at tag %d\n", __func__, tag);
		start_dma = 0;
	}

	dwc_dev_dbg(ap->dev, "%s qc=%p tag: %x cmd: 0x%02x dma_dir: %s "
			"start_dma? %x\n", __func__, qc, tag, qc->tf.command,
			dir_2_txt(qc->dma_dir), start_dma);
	sata_dwc_tf_dump(hsdev->dev, &(qc->tf));

	// Start DMA transfer
	if (start_dma) {
		reg = dwc_core_scr_read(ap, SCR_ERROR);
		if (unlikely(reg & DWC_SERR_ERR_BITS)) {
			dev_err(ap->dev, "%s: ****** SError=0x%08x ******\n",
				__func__, reg);
		}

		// Set DMA control registers
		if (qc->dma_dir == DMA_TO_DEVICE)
			out_le32(&hsdev->sata_dwc_regs->dmacr,
					DWC_DMACR_TXCHEN);
		else
			out_le32(&hsdev->sata_dwc_regs->dmacr,
					DWC_DMACR_RXCHEN);

		dwc_port_vdbg(ap, "%s: setting DMACR: 0x%08x\n", __func__, in_le32(&hsdev->sata_dwc_regs->dmacr));
		// Enable AHB DMA transfer on the specified channel
		out_le32(&(sata_dma_regs->dma_chan_en.low),
			in_le32(&(sata_dma_regs->dma_chan_en.low)) |
			DMA_ENABLE_CHAN(dma_chan));
		hsdevp->sactive_queued &= ~dwc_qctag_to_mask(tag);
	}
}


static void dwc_bmdma_start(struct ata_queued_cmd *qc)
{
	u8 tag = qc->tag;

	if (ata_is_ncq(qc->tf.protocol)) {
	} else {
		tag = 0;
	}

	dwc_dev_vdbg(qc->ap->dev, "%s: ap->link.sactive=0x%08x tag=%d\n", __func__, qc->ap->link.sactive, tag);
	dwc_bmdma_start_by_tag(qc, tag);
}

/**
 *	dwc_exec_command - issue ATA command to host controller
 *	@ap: port to which command is being issued
 *	@tf: ATA taskfile register set
 *
 *	Issues ATA command, with proper synchronization with interrupt
 *	handler / other threads.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 */
void dwc_exec_command(struct ata_port *ap, const struct ata_taskfile *tf)
{
	iowrite8(tf->command, ap->ioaddr.command_addr);
	/*	If we have an mmio device with no ctl and no altstatus
	 *	method this will fail. No such devices are known to exist.
	 */
	if (ap->ioaddr.altstatus_addr)
		ioread8(ap->ioaddr.altstatus_addr);

	ndelay(400);
}

/*
 * Process command queue issue
 */
static unsigned int dwc_qc_issue(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	int ret = 0;
	struct ata_eh_info *ehi;
	u32 scontrol, sstatus, sactive;
	u8 status;
	scontrol = dwc_core_scr_read(ap, SCR_CONTROL);

	ehi = &ap->link.eh_info;
	status = ap->ops->sff_check_altstatus(ap);
	/*
	 * Fix the problem when PMP card is unplugged from the SATA port.
	 * QC is still issued but no device present. Ignore the current QC.
	 * and pass error to error handler
	 */
	sstatus = dwc_core_scr_read(ap, SCR_STATUS);
	if ( sstatus == 0x0) {
		dwc_port_dbg(ap, "Detect connection lost while commands are executing --> ignore current command\n");
		ata_ehi_hotplugged(ehi);
		ap->link.eh_context.i.action |= ATA_EH_RESET;
		return AC_ERR_ATA_BUS;
	}

	// Set PMP field in the SCONTROL register
	if ( sata_pmp_attached(ap) )
		dwc_pmp_select(ap, qc->dev->link->pmp);

#ifdef DEBUG_NCQ
	if ((qc->tag > 0 && qc->tag != 31) || ap->link.sactive > 1) {
		dwc_port_info(ap, "%s ap id=%d cmd(0x%02x)=%s qc tag=%d prot=%s"
			" ap active_tag=0x%08x ap sactive=0x%08x\n",
			__func__, ap->print_id, qc->tf.command,
			ata_cmd_2_txt(&qc->tf), qc->tag,
			prot_2_txt(qc->tf.protocol), ap->link.active_tag,
			ap->link.sactive);
	}
#endif

	// Process NCQ
	if (ata_is_ncq(qc->tf.protocol)) {
		dwc_link_vdbg(qc->dev->link, "%s --> process NCQ , ap->link.active_tag=0x%08x, active_tag=0%08x\n", __func__, ap->link.active_tag, qc->tag);
		/*status = ap->ops->sff_check_altstatus(ap);
		if ( status & ATA_BUSY ) {
			// Ignore the QC when device is BUSY more than 1000 ms
			sactive = dwc_core_scr_read(qc->ap, SCR_ACTIVE);
			ata_port_printk(ap, KERN_INFO, "Ignore current QC because of device BUSY (tag=%d, sactive=0x%08x)\n", qc->tag, sactive);
			return AC_ERR_SYSTEM;
		}*/
		ap->ops->sff_tf_load(ap, &qc->tf);
		sata_dwc_exec_command_by_tag(ap, &qc->tf, qc->tag);

		// Write the QC tag to the SACTIVE register
		/*sactive = dwc_core_scr_read(qc->ap, SCR_ACTIVE);
		sactive |= (0x00000001 << qc->tag);
		dwc_core_scr_write(qc->ap, SCR_ACTIVE, sactive);*/

		/*
		* FPDMA Step 2.
		* Check to see if device clears BUSY bit.
		* If not, set the link.active_tag to the value different than
		* ATA_TAG_POISON so that the qc_defer will defer additional QCs
		* (no more QC is queued)
		*/
		if ( ap->link.active_tag != ATA_TAG_POISON)
			dwc_port_dbg(ap, "Some process change ap->link.active_tag to 0x%0x\n", ap->link.active_tag);
		status = ap->ops->sff_check_altstatus(ap);
		// Prevent more QC if it is currently BUSY
		if ( status & ATA_BUSY ) {
			ap->link.active_tag = qc->tag;
			dwc_port_dbg(ap, "%s - prevent more QC because of ATA_BUSY\n", __func__);
		}
	} else {
		dwc_link_dbg(qc->dev->link, "%s --> non NCQ process, ap->link.active_tag=0x%0x, active_tag=0%08x\n", __func__, ap->link.active_tag, qc->tag);
		// Sync ata_port with qc->tag
		ap->link.active_tag = qc->tag;
		// Pass QC to libata-sff to process
		ret = ata_bmdma_qc_issue(qc);
	}

	if (status & ATA_BUSY) {
		dwc_port_info(ap, "%s - ATA_BUSY\n", __func__);
		ret = AC_ERR_HOST_BUS;
	}
	return ret;
}

/*
 *  dwc_qc_prep - prepare data for command queue
 *  @qc: command queue
 *
 *  Configure DMA registers ready for doing DMA transfer.
 */
static void dwc_qc_prep(struct ata_queued_cmd *qc)
{
	u32 sactive;
	u8 tag = qc->tag;
	int num_lli;
	struct ata_port *ap = qc->ap;
	struct sata_dwc_device *hsdev = HSDEV_FROM_AP(ap);
	struct sata_dwc_device_port *hsdevp = HSDEVP_FROM_AP(ap);

	// Do nothing if not DMA command.
	if ((qc->dma_dir == DMA_NONE) || (qc->tf.protocol == ATA_PROT_PIO) ||(qc->tf.protocol == ATAPI_PROT_PIO))
		return;

#ifdef DEBUG_NCQ
	if (qc->tag > 0 && qc->tag != 31) {
		dwc_port_info(qc->ap, "%s: qc->tag=%d ap->active_tag=0x%08x\n",
			 __func__, tag, qc->ap->link.active_tag);
	}
#endif

	if (ata_is_ncq(qc->tf.protocol) ) {
		// FPDMA stage 1: Update the SActive register for new QC
		sactive = dwc_core_scr_read(qc->ap, SCR_ACTIVE);
		sactive |= (0x00000001 << tag);
		dwc_core_scr_write(qc->ap, SCR_ACTIVE, sactive);
		dwc_port_vdbg(qc->ap, "%s: tag=%d ap->link.sactive = 0x%08x "
			"sactive=0x%08x\n", __func__, tag, qc->ap->link.sactive,
			sactive);
	} else {
		tag = 0;
	}

	/* Convert SG list to linked list of items (LLIs) for AHB DMA */
	num_lli = dwc_map_sg_to_lli(qc, hsdevp->llit[tag], hsdevp->llit_dma[tag], (void *__iomem)(&hsdev->sata_dwc_regs->dmadr));
}

/*
 * dwc_lost_interrupt -  check and process if interrupt is lost.
 * @ap: ATA port
 *
 * Process the command when it is timeout.
 * Check to see if interrupt is lost. If yes, complete the qc.
 */
static void dwc_lost_interrupt(struct ata_port *ap) {
	u8 status;
	struct sata_dwc_device *hsdev = HSDEV_FROM_AP(ap);
	struct ata_queued_cmd *qc;

	dwc_port_dbg(ap, "%s - \n",__func__);
	/* Only one outstanding command per SFF channel */
	qc = dwc_get_active_qc(ap);
	/* We cannot lose an interrupt on a non-existent or polled command */
	if (!qc)
		return;

	/* See if the controller thinks it is still busy - if so the command
	   isn't a lost IRQ but is still in progress */
	status = ap->ops->sff_check_altstatus(ap);
	if (status & ATA_BUSY) {
		dwc_port_info(ap, "%s - ATA_BUSY\n", __func__);
		return;
	}

	/* There was a command running, we are no longer busy and we have
	   no interrupt. */
	ata_link_printk(qc->dev->link, KERN_WARNING, "lost interrupt (Status 0x%x)\n",
								status);
	/* Run the host interrupt logic as if the interrupt had not been lost */
	if (dwc_dma_chk_en(hsdev->dma_channel)) {
		// When DMA does transfer does not complete, see if DMA fails 
		qc->err_mask |= AC_ERR_DEV;
		ap->hsm_task_state = HSM_ST_ERR;
		dwc_dma_terminate(ap, hsdev->dma_channel);
	}
	dwc_qc_complete(ap, qc, 1);
}

static void dwc_post_internal_cmd(struct ata_queued_cmd *qc)
{
	//LGNAS 20110524 omw for handling atapi no data
	dwc_link_dbg(qc->dev->link, "%s - qc->flags=0x%05x\n", __func__, qc->flags);
#if 0
	if (qc->flags & ATA_QCFLAG_FAILED && qc->tf.protocol != ATAPI_PROT_NODATA) {
		ata_eh_freeze_port(qc->ap);
	}
#endif
}

static void dwc_error_handler(struct ata_port *ap)
{
	dwc_port_dbg(ap, "%s - \n", __func__);
	dwc_port_vdbg(ap, "qc_active=0x%08x, qc_allocated=0x%08x, active_tag=%d\n", ap->qc_active, ap->qc_allocated, ap->link.active_tag);

	sata_pmp_error_handler(ap);
}

/*
 * dwc_check_status - Get value of the Status Register
 * @ap: Port to check
 *
 * Output content of the status register (CDR7)
 */
u8 dwc_check_status(struct ata_port *ap)
{
	return ioread8(ap->ioaddr.status_addr);
}

/*
 * sata_dwc_check_altstatus - Get value of the Alternative Status
 * Register
 * @ap: Port to check
 *
 * Output content of the status register (CDR7)
 */
u8 dwc_check_altstatus(struct ata_port *ap)
{
	return ioread8(ap->ioaddr.altstatus_addr);
}

/*
 * dwc_freeze - Freeze the ATA port
 * @ap: Port to freeze
 * 
 * Stop the current DMA transfer, then clear all interrupts
 */
void dwc_freeze(struct ata_port *ap)
{
	struct sata_dwc_device *hsdev = HSDEV_FROM_AP(ap);
	dwc_port_info(ap, "call %s ...\n",__func__);
	// Terminate DMA if it is currently in use
	dwc_dma_terminate(ap, hsdev->dma_channel);
	// turn IRQ off
	ap->ops->sff_check_status(ap);
	dwc_clear_intpr(hsdev);
	dwc_clear_serror(ap);
}

/*
 * Thaw the port by turning IRQ on
 */
void dwc_thaw(struct ata_port *ap)
{
	struct sata_dwc_device *hsdev = HSDEV_FROM_AP(ap);

	dwc_port_info(ap, "call %s ...\n",__func__);
	// Clear IRQ
	dwc_clear_intpr(hsdev);
	// Turn IRQ back on
	dwc_enable_interrupts(hsdev);
}


/*
 * scsi mid-layer and libata interface structures
 */
static struct scsi_host_template sata_dwc_sht = {
	ATA_NCQ_SHT(DRV_NAME),
	/*
	 * test-only: Currently this driver doesn't handle NCQ
	 * correctly. We enable NCQ but set the queue depth to a
	 * max of 1. This will get fixed in in a future release.
	 */
    .sg_tablesize           = 64,//LIBATA_MAX_PRD, 
	.can_queue 		= ATA_MAX_QUEUE, //ATA_DEF_QUEUE,
	.dma_boundary 		= ATA_DMA_BOUNDARY,
};


static struct ata_port_operations sata_dwc_ops = {
	.inherits		= &sata_pmp_port_ops,
	.dev_config		= dwc_dev_config,
	
	.error_handler	= dwc_error_handler,
	.softreset		= dwc_softreset,
	.hardreset		= dwc_hardreset,
	.pmp_softreset	= dwc_softreset,
	.pmp_hardreset	= dwc_pmp_hardreset,

	.qc_defer		= sata_pmp_qc_defer_cmd_switch,
	.qc_prep		= dwc_qc_prep,
	.qc_issue		= dwc_qc_issue,
	.qc_fill_rtf	= ata_sff_qc_fill_rtf,

	.scr_read		= dwc_scr_read,
	.scr_write		= dwc_scr_write,

	.port_start		= dwc_port_start,
	.port_stop		= dwc_port_stop,

	.check_atapi_dma = dwc_check_atapi_dma,
	.bmdma_setup	= dwc_bmdma_setup,
	.bmdma_start	= dwc_bmdma_start,
	// Reuse some SFF functions
	.sff_check_status	= dwc_check_status,
	.sff_check_altstatus = dwc_check_altstatus,
	.sff_tf_read	= ata_sff_tf_read,
	.sff_data_xfer	= ata_sff_data_xfer,
	.sff_tf_load	= ata_sff_tf_load,
	.sff_dev_select	= dwc_dev_select,
	.sff_exec_command = dwc_exec_command,

	//.sff_irq_on		= dwc_irq_on,
	//.sff_irq_clear	= ata_bmdma_irq_clear,
	//.sff_irq_clear	= dwc_irq_clear,
	.freeze			= dwc_freeze,
	.thaw			= dwc_thaw,
	.pmp_attach		= dwc_pmp_attach,
	.pmp_detach		= dwc_pmp_detach,
	.post_internal_cmd	= dwc_post_internal_cmd,
	.lost_interrupt = dwc_lost_interrupt,
};

static const struct ata_port_info sata_dwc_port_info[] = {
	{
		/*
		 * test-only: Currently this driver doesn't handle NCQ
		 * correctly. So we disable NCQ here for now. To enable
		 * it ATA_FLAG_NCQ needs to be added to the flags below.
		 */
		.flags		= ATA_FLAG_SATA | ATA_FLAG_NO_LEGACY |
				  ATA_FLAG_MMIO | ATA_FLAG_NCQ |
				  ATA_FLAG_PMP | ATA_FLAG_AN, 
		.pio_mask	= 0x1f,	/* pio 0-4 */ 
		.udma_mask	= ATA_UDMA6,
		.port_ops	= &sata_dwc_ops,
	},
};

static int sata_dwc_probe(struct platform_device *ofdev,
			const struct of_device_id *match)
{
	struct sata_dwc_device *hsdev;
	u32 idr, versionr;
	char *ver = (char *)&versionr;
	u8 *base = NULL;
	int err = 0;
	int irq;
	struct ata_host *host;
	struct ata_port_info pi = sata_dwc_port_info[0];
	const struct ata_port_info *ppi[] = { &pi, NULL };

	const unsigned int *dma_channel;

	// Check if device is enabled
	if (!of_device_is_available(ofdev->dev.of_node)) {
		printk(KERN_INFO "%s: Port disabled via device-tree\n", 
			ofdev->dev.of_node->full_name);
		return 0;
	}

	// Allocate DWC SATA device
	hsdev = kmalloc(sizeof(*hsdev), GFP_KERNEL);
	if (hsdev == NULL) {
		dev_err(&ofdev->dev, "kmalloc failed for hsdev\n");
		err = -ENOMEM;
		goto error_out;
	}
	memset(hsdev, 0, sizeof(*hsdev));


	// Identify SATA DMA channel used for the current SATA device
	dma_channel = of_get_property(ofdev->dev.of_node, "dma-channel", NULL);
	if ( dma_channel ) {
		dev_notice(&ofdev->dev, "Gettting DMA channel %d\n", *dma_channel);
		hsdev->dma_channel = *dma_channel;
	} else
		hsdev->dma_channel = 0;

	// Ioremap SATA registers
	base = of_iomap(ofdev->dev.of_node, 0);
	if (!base) {
		dev_err(&ofdev->dev, "ioremap failed for SATA register address\n");
		err = -ENODEV;
		goto error_out;
	}
	hsdev->reg_base = base;
	dwc_dev_vdbg(&ofdev->dev, "ioremap done for SATA register address\n");

	// Synopsys DWC SATA specific Registers
	hsdev->sata_dwc_regs = (void *__iomem)(base + DWC_REG_OFFSET);

	// Allocate and fill host
	host = ata_host_alloc_pinfo(&ofdev->dev, ppi, SATA_DWC_MAX_PORTS);
	if (!host) {
		dev_err(&ofdev->dev, "ata_host_alloc_pinfo failed\n");
		err = -ENOMEM;
		goto error_out;
	}
	host->private_data = hsdev;

	// Setup port
	host->ports[0]->ioaddr.cmd_addr = base;
	host->ports[0]->ioaddr.scr_addr = base + DWC_SCR_OFFSET;
	hsdev->scr_base = (u8 *)(base + DWC_SCR_OFFSET);
	dwc_setup_port(&host->ports[0]->ioaddr, (unsigned long)base);

	// Read the ID and Version Registers
	idr = in_le32(&hsdev->sata_dwc_regs->idr);
	versionr = in_le32(&hsdev->sata_dwc_regs->versionr);
	dev_notice(&ofdev->dev, "id %d, controller version %c.%c%c\n",
		   idr, ver[0], ver[1], ver[2]);

	// Get SATA DMA interrupt number
	irq = irq_of_parse_and_map(ofdev->dev.of_node, 1);
	if (irq == NO_IRQ) {
		dev_err(&ofdev->dev, "no SATA DMA irq\n");
		err = -ENODEV;
		goto error_out;
	}

	// Get physical SATA DMA register base address
	if (!sata_dma_regs) {
	sata_dma_regs = of_iomap(ofdev->dev.of_node, 1);
		if (!sata_dma_regs) {
			dev_err(&ofdev->dev, "ioremap failed for AHBDMA register address\n");
			err = -ENODEV;
			goto error_out;
		}
	}
	// Save dev for later use in dev_xxx() routines
	hsdev->dev = &ofdev->dev;

	// Init glovbal dev list
	dwc_dev_list[hsdev->dma_channel] = hsdev;

	// Initialize AHB DMAC
	hsdev->irq_dma = irq;
	dwc_dma_init(hsdev);
	dwc_dma_register_intpr(hsdev);


	// Enable SATA Interrupts
	dwc_enable_interrupts(hsdev);

	// Get SATA interrupt number
	irq = irq_of_parse_and_map(ofdev->dev.of_node, 0);
	if (irq == NO_IRQ) {
		dev_err(&ofdev->dev, "no SATA irq\n");
		err = -ENODEV;
		goto error_out;
	}

	/*
	 * Now, register with libATA core, this will also initiate the
	 * device discovery process, invoking our port_start() handler &
	 * error_handler() to execute a dummy Softreset EH session
	 */
	ata_host_activate(host, irq, sata_dwc_isr, IRQF_SHARED, &sata_dwc_sht);
	dev_set_drvdata(&ofdev->dev, host);

	// Everything is fine
	return 0;

error_out:
	// Free SATA DMA resources
	dwc_dma_exit(hsdev);

	if (base)
		iounmap(base);

	if (hsdev)
		kfree(hsdev);

	return err;
}

static int sata_dwc_remove(struct platform_device *ofdev)
{
	struct device *dev = &ofdev->dev;
	struct ata_host *host = dev_get_drvdata(dev);
	struct sata_dwc_device *hsdev = host->private_data;

	ata_host_detach(host);

	dev_set_drvdata(dev, NULL);

	// Free SATA DMA resources
	dwc_dma_exit(hsdev);

	iounmap(hsdev->reg_base);
	kfree(hsdev);
	kfree(host);

	dwc_dev_vdbg(&ofdev->dev, "done\n");

	return 0;
}

static const struct of_device_id sata_dwc_match[] = {
	{ .compatible = "amcc,sata-460ex", },
	{ .compatible = "amcc,sata-apm82181", },
	{}
};
MODULE_DEVICE_TABLE(of, sata_dwc_match);

static struct of_platform_driver sata_dwc_driver = {
	.driver = {
		.name = DRV_NAME,
		.owner = THIS_MODULE,
		.of_match_table = sata_dwc_match,
	},
	.probe = sata_dwc_probe,
	.remove = sata_dwc_remove,
};

static int __init sata_dwc_init(void)
{
	return of_register_platform_driver(&sata_dwc_driver);
}

static void __exit sata_dwc_exit(void)
{
	of_unregister_platform_driver(&sata_dwc_driver);
}

module_init(sata_dwc_init);
module_exit(sata_dwc_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mark Miesfeld <mmiesfeld@amcc.com>");
MODULE_DESCRIPTION("DesignWare Cores SATA controller low lever driver");
MODULE_VERSION(DRV_VERSION);