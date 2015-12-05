/*
 * Copyright(c) 2006 DENX Engineering. All rights reserved.
 *
 * Author: Tirumala Marr <tmarri@amcc.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 * The full GNU General Public License is included in this distribution in the
 * file called COPYING.
 */

/*
 *  This driver supports the asynchrounous DMA copy and RAID engines available
 * on the AMCC PPC460ex Processors.
 *  Based on the Intel Xscale(R) family of I/O Processors (IOP 32x, 33x, 134x)
 * ADMA driver written by D.Williams.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/async_tx.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/uaccess.h>
#include <linux/of_platform.h>
#include <linux/proc_fs.h>
#include <asm/dcr.h>
#include <asm/dcr-regs.h>
#include <asm/ppc460ex_plb_adma.h>
#include <asm/ppc460ex_xor.h>
#define PPC44x_SRAM_ADDR       0x00000000400048000ULL
//#define PPC44x_SRAM_SIZE               0x10000        /* 64 Kb*/ 
#define PPC44x_SRAM_SIZE               0x8000        /* 32 Kb*/ 
//#define CONFIG_ADMA_SRAM 1

/* The list of channels exported by ppc460ex ADMA */
struct list_head
ppc_adma_p_chan_list = LIST_HEAD_INIT(ppc_adma_p_chan_list);

/* This flag is set when want to refetch the xor chain in the interrupt
 *	handler
 */
static u32 do_xor_refetch = 0;

/* Pointers to last submitted to DMA0, DMA1 CDBs */
static ppc460ex_p_desc_t *chan_last_sub[4];
static ppc460ex_p_desc_t *chan_first_cdb[4];

/* Pointer to last linked and submitted xor CB */
static ppc460ex_p_desc_t *xor_last_linked = NULL;
static ppc460ex_p_desc_t *xor_last_submit = NULL;


/* Since RXOR operations use the common register (MQ0_CF2H) for setting-up
 * the block size in transactions, then we do not allow to activate more than
 * only one RXOR transactions simultaneously. So use this var to store
 * the information about is RXOR currently active (PPC460EX_RXOR_RUN bit is
 * set) or not (PPC460EX_RXOR_RUN is clear).
 */

/* /proc interface is used here to enable the h/w RAID-6 capabilities
 */
static struct proc_dir_entry *ppc460ex_proot;

/* These are used in enable & check routines
 */
static u32 ppc460ex_r6_enabled;
static u32 ppc460ex_r5_enabled;
static ppc460ex_p_ch_t *ppc460ex_r6_tchan;
static ppc460ex_p_ch_t *ppc460ex_dma_tchan;
static struct completion ppc460ex_r6_test_comp;
static struct completion ppc460ex_r5_test_comp;

#if 1
static inline  void pr_dma(int x, char *str)
{
	if(mfdcr(0x60)) {
		printk("<%s>  Line:%d\n",str,x);
	}
}
#else
static inline  void pr_dma(int x, char *str)
{
}
#endif
static phys_addr_t fixup_bigphys_addr(phys_addr_t addr, phys_addr_t size)
{
	phys_addr_t page_4gb = 0;
	 
	return (page_4gb | addr);
}


/******************************************************************************
 * Command (Descriptor) Blocks low-level routines
 ******************************************************************************/
/**
 * ppc460ex_desc_init_interrupt - initialize the descriptor for INTERRUPT
 * pseudo operation
 */
static inline void ppc460ex_desc_init_interrupt (ppc460ex_p_desc_t *desc,
							ppc460ex_p_ch_t *chan)
{
	u32 base = 0;
	dma_cdb_t *hw_desc;


	hw_desc = desc->hw_desc;

		
	memset (desc->hw_desc, 0, sizeof(dma_cdb_t));
	switch (chan->chan_id) {
	case PPC460EX_PDMA0_ID:
		base = DCR_DMA0_BASE;
		break;
	case PPC460EX_PDMA1_ID:
		base = DCR_DMA1_BASE;
		break;
	case PPC460EX_PDMA2_ID:
		base = DCR_DMA2_BASE;
		break;
	case PPC460EX_PDMA3_ID:
		base = DCR_DMA3_BASE;
		break;
	default:
		printk(KERN_ERR "Unsupported id %d in %s\n", chan->device->id,
				__FUNCTION__);
		BUG();
		break;
	}
		hw_desc->ctrl = mfdcr(base + DCR_DMA2P40_CTC0);
		set_bit(PPC460EX_DESC_INT, &desc->flags);
		set_bit(DMA_CIE_ENABLE,hw_desc->ctrl);
}

/**
 * ppc460ex_desc_init_memcpy - initialize the descriptor for MEMCPY operation
 */
static inline void ppc460ex_desc_init_memcpy(ppc460ex_p_desc_t *desc,
		unsigned long flags)
{

	memset (desc->hw_desc, 0, sizeof(dma_cdb_t));
	desc->hw_next = NULL;
	desc->src_cnt = 1;
	desc->dst_cnt = 1;

	if (flags & DMA_PREP_INTERRUPT)
		set_bit(PPC460EX_DESC_INT, &desc->flags);
	else
		clear_bit(PPC460EX_DESC_INT, &desc->flags);

}

/**
 * ppc460ex_desc_init_memset - initialize the descriptor for MEMSET operation
 */
static inline void ppc460ex_desc_init_memset(ppc460ex_p_desc_t *desc, int value,
		unsigned long flags)
{
	dma_cdb_t *hw_desc = desc->hw_desc;

	memset (desc->hw_desc, 0, sizeof(dma_cdb_t));
	desc->hw_next = NULL;
	desc->src_cnt = 1;
	desc->dst_cnt = 1;

	if (flags & DMA_PREP_INTERRUPT)
		set_bit(PPC460EX_DESC_INT, &desc->flags);
	else
		clear_bit(PPC460EX_DESC_INT, &desc->flags);

}

/**
 * ppc460ex_desc_set_src_addr - set source address into the descriptor
 */
static inline void ppc460ex_desc_set_src_addr( ppc460ex_p_desc_t *desc,
					ppc460ex_p_ch_t *chan, 
					dma_addr_t addrh, dma_addr_t addrl)
{
	dma_cdb_t *dma_hw_desc;
	phys_addr_t addr64, tmplow, tmphi;
	u32 base = 0;

	dma_hw_desc = desc->hw_desc;
	switch (chan->chan_id) {
	case PPC460EX_PDMA0_ID:
		base = DCR_DMA0_BASE;
		break;
	case PPC460EX_PDMA1_ID:
		base = DCR_DMA1_BASE;
		break;
	case PPC460EX_PDMA2_ID:
		base = DCR_DMA2_BASE;
		break;
	case PPC460EX_PDMA3_ID:
		base = DCR_DMA3_BASE;
		break;
	default:
		BUG();
	}
	if (!addrh) {
		addr64 = fixup_bigphys_addr(addrl, sizeof(phys_addr_t));
		tmphi = (addr64 >> 32);
		tmplow = (addr64 & 0xFFFFFFFF);
	} else {
		tmphi = addrh;
		tmplow = addrl;
	}
	dma_hw_desc->src_hi = tmphi;
	dma_hw_desc->src_lo = tmplow;
}


/**
 * ppc460ex_desc_set_dest_addr - set destination address into the descriptor
 */
static inline void ppc460ex_desc_set_dest_addr(ppc460ex_p_desc_t *desc,
				ppc460ex_p_ch_t *chan,
				dma_addr_t addrh, dma_addr_t addrl)
{
	dma_cdb_t *dma_hw_desc;
	phys_addr_t addr64, tmphi, tmplow;

	dma_hw_desc = desc->hw_desc;
	switch (chan->chan_id) {
	case PPC460EX_PDMA0_ID:
	case PPC460EX_PDMA1_ID:
	case PPC460EX_PDMA2_ID:
	case PPC460EX_PDMA3_ID:
		break;
	default :
		BUG();
	}

	if (!addrh) {
		addr64 = fixup_bigphys_addr(addrl, sizeof(phys_addr_t));
		tmphi = (addr64 >> 32);
		tmplow = (addr64 & 0xFFFFFFFF);
	} else {
		tmphi = addrh;
		tmplow = addrl;
	}
	dma_hw_desc->dest_hi = tmphi;
	dma_hw_desc->dest_lo = tmplow;
}

/**
 * ppc460ex_desc_set_byte_count - set number of data bytes involved
 * into the operation
 */
static inline void ppc460ex_desc_set_byte_count(ppc460ex_p_desc_t *desc,
					ppc460ex_p_ch_t *chan, u32 byte_count)
{
	dma_cdb_t *dma_hw_desc;
	u32 base = 0;
	u32 count = 0;
	u32 error = 0;

	dma_hw_desc = desc->hw_desc;
	switch (chan->chan_id) {
	case PPC460EX_PDMA0_ID:
		base = DCR_DMA0_BASE;
		break;
	case PPC460EX_PDMA1_ID:
		base = DCR_DMA1_BASE;
		break;
	case PPC460EX_PDMA2_ID:
		base = DCR_DMA2_BASE;
		break;
	case PPC460EX_PDMA3_ID:
		base = DCR_DMA3_BASE;
		break;
	}
	switch (chan->pwidth) {
	case PW_8:
		break;
	case PW_16:
		if (count & 0x1)
			error = 1;
		break;
	case PW_32:
		if (count & 0x3)
			error = 1;
		break;
	case PW_64:
		if (count & 0x7)
			error = 1;
		break;
	 
	case PW_128:
	        if (count & 0xf)
		        error = 1;
		break;
	default:
		printk("set_dma_count: invalid bus width: 0x%x\n",
		       chan->pwidth);
		return;
	}
	if (error)
		printk
		    ("Warning: set_dma_count count 0x%x bus width %d\n",
		     count, chan->pwidth);

	count = count >> chan->shift;
	dma_hw_desc->cnt = count;
	 

}

/**
 * ppc460ex_desc_set_link - set the address of descriptor following this
 * descriptor in chain
 */
static inline void ppc460ex_desc_set_link(ppc460ex_p_ch_t *chan,
		ppc460ex_p_desc_t *prev_desc, ppc460ex_p_desc_t *next_desc)
{
	unsigned long flags;
	ppc460ex_p_desc_t *tail = next_desc;

	if (unlikely(!prev_desc || !next_desc ||
		(prev_desc->hw_next && prev_desc->hw_next != next_desc))) {
		/* If previous next is overwritten something is wrong.
		 * though we may refetch from append to initiate list
		 * processing; in this case - it's ok.
		 */
		printk(KERN_ERR "%s: prev_desc=0x%p; next_desc=0x%p; "
			"prev->hw_next=0x%p\n", __FUNCTION__, prev_desc,
			next_desc, prev_desc ? prev_desc->hw_next : 0);
		BUG();
	}

	local_irq_save(flags);

	/* do s/w chaining both for DMA and XOR descriptors */
	prev_desc->hw_next = next_desc;

	switch (chan->chan_id) {
	case PPC460EX_PDMA0_ID:
	case PPC460EX_PDMA1_ID:
	case PPC460EX_PDMA2_ID:
	case PPC460EX_PDMA3_ID:
		break;
	default:
		BUG();
	}

	local_irq_restore(flags);
}

/**
 * ppc460ex_desc_get_src_addr - extract the source address from the descriptor
 */
static inline u32 ppc460ex_desc_get_src_addr(ppc460ex_p_desc_t *desc,
					ppc460ex_p_ch_t *chan, int src_idx)
{
	dma_cdb_t *dma_hw_desc;
	u32 base;
		
	dma_hw_desc = desc->hw_desc;

	switch (chan->chan_id) {
	case PPC460EX_PDMA0_ID:
		base = DCR_DMA0_BASE;
		break;
	case PPC460EX_PDMA1_ID:
		base = DCR_DMA1_BASE;
		break;
	case PPC460EX_PDMA2_ID:
		base = DCR_DMA2_BASE;
		break;
	case PPC460EX_PDMA3_ID:
		base = DCR_DMA3_BASE;
		break;
	default:
		return 0;
	}
		/* May have 0, 1, 2, or 3 sources */
		return (dma_hw_desc->src_lo); 
}

/**
 * ppc460ex_desc_get_dest_addr - extract the destination address from the
 * descriptor
 */
static inline u32 ppc460ex_desc_get_dest_addr(ppc460ex_p_desc_t *desc,
		ppc460ex_p_ch_t *chan, int idx)
{
	dma_cdb_t *dma_hw_desc;
	u32 base;
		
	dma_hw_desc = desc->hw_desc;

	switch (chan->chan_id) {
	case PPC460EX_PDMA0_ID:
		base = DCR_DMA0_BASE;
		break;
	case PPC460EX_PDMA1_ID:
		base = DCR_DMA1_BASE;
		break;
	case PPC460EX_PDMA2_ID:
		base = DCR_DMA2_BASE;
		break;
	case PPC460EX_PDMA3_ID:
		base = DCR_DMA3_BASE;
		break;
	default:
		return 0;
	}
		
	/* May have 0, 1, 2, or 3 sources */
	return (dma_hw_desc->dest_lo); 
}

/**
 * ppc460ex_desc_get_byte_count - extract the byte count from the descriptor
 */
static inline u32 ppc460ex_desc_get_byte_count(ppc460ex_p_desc_t *desc,
		ppc460ex_p_ch_t *chan)
{
	dma_cdb_t *dma_hw_desc;
	u32 base;
		
	dma_hw_desc = desc->hw_desc;

	switch (chan->chan_id) {
	case PPC460EX_PDMA0_ID:
		base = DCR_DMA0_BASE;
		break;
	case PPC460EX_PDMA1_ID:
		base = DCR_DMA1_BASE;
		break;
	case PPC460EX_PDMA2_ID:
		base = DCR_DMA2_BASE;
		break;
	case PPC460EX_PDMA3_ID:
		base = DCR_DMA3_BASE;
		break;
	default:
		return 0;
	}
		/* May have 0, 1, 2, or 3 sources */
		return (dma_hw_desc->cnt); 
}


/**
 * ppc460ex_desc_get_link - get the address of the descriptor that
 * follows this one
 */
static inline u32 ppc460ex_desc_get_link(ppc460ex_p_desc_t *desc,
		ppc460ex_p_ch_t *chan)
{
	if (!desc->hw_next)
		return 0;

	return desc->hw_next->phys;
}

/**
 * ppc460ex_desc_is_aligned - check alignment
 */
static inline int ppc460ex_desc_is_aligned(ppc460ex_p_desc_t *desc,
		int num_slots)
{
	return (desc->idx & (num_slots - 1)) ? 0 : 1;
}



/******************************************************************************
 * ADMA channel low-level routines
 ******************************************************************************/

static inline u32 ppc460ex_chan_get_current_descriptor(ppc460ex_p_ch_t *chan);
static inline void ppc460ex_chan_append(ppc460ex_p_ch_t *chan);

/*
 * ppc460ex_adma_device_clear_eot_status - interrupt ack to XOR or DMA engine
 */
static inline void ppc460ex_adma_device_clear_eot_status (ppc460ex_p_ch_t *chan)
{
	u8 *p = chan->dma_desc_pool_virt;
	dma_cdb_t *cdb;
	u32 rv ;
	u32 base;

	switch (chan->chan_id) {
	case PPC460EX_PDMA0_ID:
		base = DCR_DMA0_BASE;
		break;
	case PPC460EX_PDMA1_ID:
		base = DCR_DMA1_BASE;
		break;
	case PPC460EX_PDMA2_ID:
		base = DCR_DMA2_BASE;
		break;
	case PPC460EX_PDMA3_ID:
		base = DCR_DMA3_BASE;
		break;

		rv = mfdcr(base + DCR_DMA2P40_CR0) & ((DMA_CH0_ERR >> chan->chan_id));
		if (rv) {
			printk("DMA%d err status: 0x%x\n", chan->device->id,
				rv);
			/* write back to clear */
			mtdcr(base + DCR_DMA2P40_CR0, rv);
		}
		break;
	default:
		break;
	}

}

/*
 * ppc460ex_chan_is_busy - get the channel status
 */

static inline int ppc460ex_chan_is_busy(ppc460ex_p_ch_t *chan)
{
	int busy = 0;
	u32 base = 0;

	switch (chan->chan_id) {
	case PPC460EX_PDMA0_ID:
		base = DCR_DMA0_BASE;
		break;
	case PPC460EX_PDMA1_ID:
		base = DCR_DMA1_BASE;
		break;
	case PPC460EX_PDMA2_ID:
		base = DCR_DMA2_BASE;
		break;
	case PPC460EX_PDMA3_ID:
		base = DCR_DMA3_BASE;
		break;
	default:
		BUG();
	}
	if(mfdcr((DCR_DMA2P40_SR) & 0x00000800))
		busy = 1;
	else
		busy = 0;

	return busy;
}

/**
 * ppc460ex_dma_put_desc - put DMA0,1 descriptor to FIFO
 */
static inline void ppc460ex_dma_put_desc(ppc460ex_p_ch_t *chan,
		ppc460ex_p_desc_t *desc)
{
	unsigned int control;
	u32 sg_cmd;
	u32 sg_hi;
	u32 sg_lo;
	u32 base = 0;

	sg_lo = desc->phys;

	control |= (chan->mode | DMA_CE_ENABLE);
	control |= DMA_BEN;
	switch (chan->chan_id) {
	case PPC460EX_PDMA0_ID:
		base = DCR_DMA0_BASE;
		break;
	case PPC460EX_PDMA1_ID:
		base = DCR_DMA1_BASE;
		break;
	case PPC460EX_PDMA2_ID:
		base = DCR_DMA2_BASE;
		break;
	case PPC460EX_PDMA3_ID:
		base = DCR_DMA3_BASE;
		break;
	default:
		BUG();
	}
	chan->in_use = 1;
	sg_cmd = mfdcr(DCR_DMA2P40_SGC);
	sg_cmd = sg_cmd | SSG_ENABLE(chan->chan_id);
	sg_cmd = sg_cmd & 0xF0FFFFFF;
	mtdcr(base + DCR_DMA2P40_SGL0, sg_lo);
#ifdef PPC4xx_DMA_64BIT 
	mtdcr(base + DCR_DMA2P40_SGH0, sg_hi);
#endif
	mtdcr(DCR_DMA2P40_SGC,sg_cmd);
}

/**
 * ppc460ex_chan_append - update the h/w chain in the channel
 */
static inline void ppc460ex_chan_append(ppc460ex_p_ch_t *chan)
{
	ppc460ex_p_desc_t *iter;
	u32 cur_desc;
	unsigned long flags;

	switch (chan->chan_id) {
	case PPC460EX_PDMA0_ID:
	case PPC460EX_PDMA1_ID:
	case PPC460EX_PDMA2_ID:
	case PPC460EX_PDMA3_ID:
		cur_desc = ppc460ex_chan_get_current_descriptor(chan);

		if (likely(cur_desc)) {
			iter = chan_last_sub[chan->device->id];
			BUG_ON(!iter);
		} else {
			/* first peer */
			iter = chan_first_cdb[chan->device->id];
			BUG_ON(!iter);
			ppc460ex_dma_put_desc(chan, iter);
			chan->hw_chain_inited = 1;
		}

		/* is there something new to append */
		if (!iter->hw_next)
			return;

		/* flush descriptors from the s/w queue to fifo */
		list_for_each_entry_continue(iter, &chan->chain, chain_node) {
			ppc460ex_dma_put_desc(chan, iter);
			if (!iter->hw_next)
				break;
		}
		break;
	default:
		BUG();
	}
}

/**
 * ppc460ex_chan_get_current_descriptor - get the currently executed descriptor
 */
static inline u32 ppc460ex_chan_get_current_descriptor(ppc460ex_p_ch_t *chan)
{
	u32 base;
	

	if (unlikely(!chan->hw_chain_inited))
		/* h/w descriptor chain is not initialized yet */
		return 0;

	switch (chan->chan_id) {
	case PPC460EX_PDMA0_ID:
		base = DCR_DMA0_BASE;
		break;
	case PPC460EX_PDMA1_ID:
		base = DCR_DMA1_BASE;
		break;
	case PPC460EX_PDMA2_ID:
		base = DCR_DMA2_BASE;
		break;
	case PPC460EX_PDMA3_ID:
		base = DCR_DMA3_BASE;
		break;
	default:
		BUG();
	}

	return (mfdcr(base + DCR_DMA2P40_SGH0));
}


/******************************************************************************
 * ADMA device level
 ******************************************************************************/

static void ppc460ex_chan_start_null_xor(ppc460ex_p_ch_t *chan);
static int ppc460ex_adma_alloc_chan_resources(struct dma_chan *chan);
static dma_cookie_t ppc460ex_adma_tx_submit(
		struct dma_async_tx_descriptor *tx);

static void ppc460ex_adma_set_dest(
		ppc460ex_p_desc_t *tx,
		dma_addr_t addr, int index);



/**
 * ppc460ex_adma_device_estimate - estimate the efficiency of processing
 *	the operation given on this channel. It's assumed that 'chan' is
 *	capable to process 'cap' type of operation.
 * @chan: channel to use
 * @cap: type of transaction
 * @src_lst: array of source pointers
 * @src_cnt: number of source operands
 * @src_sz: size of each source operand
 */
int ppc460ex_adma_p_estimate (struct dma_chan *chan,
	enum dma_transaction_type cap, struct page **src_lst,
	int src_cnt, size_t src_sz)
{
	int ef = 1;

	if (cap == DMA_PQ || cap == DMA_PQ_ZERO_SUM) {
		/* If RAID-6 capabilities were not activated don't try
		 * to use them
		 */
		if (unlikely(!ppc460ex_r6_enabled))
			return -1;
	}
	/* channel idleness increases the priority */
	if (likely(ef) &&
	    !ppc460ex_chan_is_busy(to_ppc460ex_adma_chan(chan)))
		ef++;

	return ef;
}

/**
 * ppc460ex_get_group_entry - get group entry with index idx
 * @tdesc: is the last allocated slot in the group.
 */
static inline ppc460ex_p_desc_t *
ppc460ex_get_group_entry ( ppc460ex_p_desc_t *tdesc, u32 entry_idx)
{
	ppc460ex_p_desc_t *iter = tdesc->group_head;
	int i = 0;

	if (entry_idx < 0 || entry_idx >= (tdesc->src_cnt + tdesc->dst_cnt)) {
		printk("%s: entry_idx %d, src_cnt %d, dst_cnt %d\n",
				__func__, entry_idx, tdesc->src_cnt, tdesc->dst_cnt);
		BUG();
	}
	list_for_each_entry(iter, &tdesc->group_list, chain_node) {
		if (i++ == entry_idx)
			break;
	}
	return iter;
}

/**
 * ppc460ex_adma_free_slots - flags descriptor slots for reuse
 * @slot: Slot to free
 * Caller must hold &ppc460ex_chan->lock while calling this function
 */
static void ppc460ex_adma_free_slots(ppc460ex_p_desc_t *slot,
		ppc460ex_p_ch_t *chan)
{
	int stride = slot->slots_per_op;

	while (stride--) {
		/*async_tx_clear_ack(&slot->async_tx);*/ /* Don't need to clear. It is hack*/
		slot->slots_per_op = 0;
		slot = list_entry(slot->slot_node.next,
				ppc460ex_p_desc_t,
				slot_node);
	}
}

static void 
ppc460ex_adma_unmap(ppc460ex_p_ch_t *chan, ppc460ex_p_desc_t *desc)
{
	u32 src_cnt, dst_cnt;
	dma_addr_t addr;
	/*
	 * get the number of sources & destination
	 * included in this descriptor and unmap
	 * them all
	 */
	src_cnt = 1; 
	dst_cnt = 1; 

	/* unmap destinations */
	if (!(desc->async_tx.flags & DMA_COMPL_SKIP_DEST_UNMAP)) {
		while (dst_cnt--) {
			addr = ppc460ex_desc_get_dest_addr(
				desc, chan, dst_cnt);
			dma_unmap_page(&chan->device->odev->dev,
					addr, desc->unmap_len,
					DMA_FROM_DEVICE);
		}
	}

	/* unmap sources */
	if (!(desc->async_tx.flags & DMA_COMPL_SKIP_SRC_UNMAP)) {
		while (src_cnt--) {
			addr = ppc460ex_desc_get_src_addr(
				desc, chan, src_cnt);
			dma_unmap_page(&chan->device->odev->dev,
					addr, desc->unmap_len,
					DMA_TO_DEVICE);
		}
	}

}
/**
 * ppc460ex_adma_run_tx_complete_actions - call functions to be called
 * upon complete
 */
static dma_cookie_t ppc460ex_adma_run_tx_complete_actions(
		ppc460ex_p_desc_t *desc,
		ppc460ex_p_ch_t *chan,
		dma_cookie_t cookie)
{
	int i;
	enum dma_data_direction dir;

	BUG_ON(desc->async_tx.cookie < 0);
	if (desc->async_tx.cookie > 0) {
		cookie = desc->async_tx.cookie;
		desc->async_tx.cookie = 0;

		/* call the callback (must not sleep or submit new
		 * operations to this channel)
		 */
		if (desc->async_tx.callback)
			desc->async_tx.callback(
				desc->async_tx.callback_param);

		/* unmap dma addresses
		 * (unmap_single vs unmap_page?)
		 *
		 * actually, ppc's dma_unmap_page() functions are empty, so
		 * the following code is just for the sake of completeness
		 */
		if (chan && chan->needs_unmap && desc->group_head &&
		     desc->unmap_len) {
			ppc460ex_p_desc_t *unmap = desc->group_head;
			/* assume 1 slot per op always */
			u32 slot_count = unmap->slot_cnt;

			/* Run through the group list and unmap addresses */
			for (i = 0; i < slot_count; i++) {
				BUG_ON(!unmap);
				ppc460ex_adma_unmap(chan, unmap);
				unmap = unmap->hw_next;
			}
			desc->group_head = NULL;
		}
	}

	/* run dependent operations */
	dma_run_dependencies(&desc->async_tx);

	return cookie;
}

/**
 * ppc460ex_adma_clean_slot - clean up CDB slot (if ack is set)
 */
static int ppc460ex_adma_clean_slot(ppc460ex_p_desc_t *desc,
		ppc460ex_p_ch_t *chan)
{
	/* the client is allowed to attach dependent operations
	 * until 'ack' is set
	 */
	if (!async_tx_test_ack(&desc->async_tx))
		return 0;

	/* leave the last descriptor in the chain
	 * so we can append to it
	 */
	if (list_is_last(&desc->chain_node, &chan->chain) ||
	    desc->phys == ppc460ex_chan_get_current_descriptor(chan))
		return 1;

	dev_dbg(chan->device->common.dev, "\tfree slot %x: %d stride: %d\n",
		desc->phys, desc->idx, desc->slots_per_op);

	list_del(&desc->chain_node);
	ppc460ex_adma_free_slots(desc, chan);
	return 0;
}

/**
 * #define DEBUG 1__ppc460ex_adma_slot_cleanup - this is the common clean-up routine
 *	which runs through the channel CDBs list until reach the descriptor
 *	currently processed. When routine determines that all CDBs of group
 *	are completed then corresponding callbacks (if any) are called and slots
 *	are freed.
 */
static void __ppc460ex_adma_slot_cleanup(ppc460ex_p_ch_t *chan)
{
	ppc460ex_p_desc_t *iter, *_iter, *group_start = NULL;
	dma_cookie_t cookie = 0;
	u32 current_desc = ppc460ex_chan_get_current_descriptor(chan);
	int busy = ppc460ex_chan_is_busy(chan);
	int seen_current = 0, slot_cnt = 0, slots_per_op = 0;

	dev_dbg(chan->device->common.dev, "ppc460ex adma%d: %s\n",
		chan->device->id, __FUNCTION__);

	if (!current_desc) {
		/*  There were no transactions yet, so
		 * nothing to clean
		 */
		return;
	}

	/* free completed slots from the chain starting with
	 * the oldest descriptor
	 */
	list_for_each_entry_safe(iter, _iter, &chan->chain,
					chain_node) {
		dev_dbg(chan->device->common.dev, "\tcookie: %d slot: %d "
		    "busy: %d this_desc: %#x next_desc: %#x cur: %#x ack: %d\n",
			iter->async_tx.cookie, iter->idx, busy, iter->phys,
		    ppc460ex_desc_get_link(iter, chan), current_desc,
			async_tx_test_ack(&iter->async_tx));
		prefetch(_iter);
		prefetch(&_iter->async_tx);

		/* do not advance past the current descriptor loaded into the
		 * hardware channel,subsequent descriptors are either in process
		 * or have not been submitted
		 */
		if (seen_current)
			break;

		/* stop the search if we reach the current descriptor and the
		 * channel is busy, or if it appears that the current descriptor
		 * needs to be re-read (i.e. has been appended to)
		 */
		if (iter->phys == current_desc) {
			BUG_ON(seen_current++);
			if (busy || ppc460ex_desc_get_link(iter, chan)) {
				/* not all descriptors of the group have
				 * been completed; exit.
				 */
				break;
			}
		}

		/* detect the start of a group transaction */
		if (!slot_cnt && !slots_per_op) {
			slot_cnt = iter->slot_cnt;
			slots_per_op = iter->slots_per_op;
			if (slot_cnt <= slots_per_op) {
				slot_cnt = 0;
				slots_per_op = 0;
			}
		}

		if (slot_cnt) {
			if (!group_start)
				group_start = iter;
			slot_cnt -= slots_per_op;
		}

		/* all the members of a group are complete */
		if (slots_per_op != 0 && slot_cnt == 0) {
			ppc460ex_p_desc_t *grp_iter, *_grp_iter;
			int end_of_chain = 0;

			/* clean up the group */
			slot_cnt = group_start->slot_cnt;
			grp_iter = group_start;
			list_for_each_entry_safe_from(grp_iter, _grp_iter,
				&chan->chain, chain_node) {

				cookie = ppc460ex_adma_run_tx_complete_actions(
					grp_iter, chan, cookie);

				slot_cnt -= slots_per_op;
				end_of_chain = ppc460ex_adma_clean_slot(
				    grp_iter, chan);
				if (end_of_chain && slot_cnt) {
					/* Should wait for ZeroSum complete */
					if (cookie > 0)
						chan->completed_cookie = cookie;
					return;
				}

				if (slot_cnt == 0 || end_of_chain)
					break;
			}

			/* the group should be complete at this point */
			BUG_ON(slot_cnt);

			slots_per_op = 0;
			group_start = NULL;
			if (end_of_chain)
				break;
			else
				continue;
		} else if (slots_per_op) /* wait for group completion */
			continue;

		cookie = ppc460ex_adma_run_tx_complete_actions(iter, chan,
		    cookie);

		if (ppc460ex_adma_clean_slot(iter, chan))
			break;
	}

	BUG_ON(!seen_current);

	if (cookie > 0) {
		chan->completed_cookie = cookie;
		pr_debug("\tcompleted cookie %d\n", cookie);
	}

}

/**
 * ppc460ex_adma_tasklet - clean up watch-dog initiator
 */
static void ppc460ex_adma_tasklet (unsigned long data)
{
	ppc460ex_p_ch_t *chan = (ppc460ex_p_ch_t *) data;
	__ppc460ex_adma_slot_cleanup(chan);
}

/**
 * ppc460ex_adma_slot_cleanup - clean up scheduled initiator
 */
static void ppc460ex_adma_slot_cleanup (ppc460ex_p_ch_t *chan)
{
	spin_lock_bh(&chan->lock);
	__ppc460ex_adma_slot_cleanup(chan);
	spin_unlock_bh(&chan->lock);
}

/**
 * ppc460ex_adma_alloc_slots - allocate free slots (if any)
 */
static ppc460ex_p_desc_t *ppc460ex_adma_alloc_slots(
		ppc460ex_p_ch_t *chan, int num_slots,
		int slots_per_op)
{
	ppc460ex_p_desc_t *iter = NULL, *_iter, *alloc_start = NULL;
	struct list_head chain = LIST_HEAD_INIT(chain);
	int slots_found, retry = 0;


	BUG_ON(!num_slots || !slots_per_op);
	/* start search from the last allocated descrtiptor
	 * if a contiguous allocation can not be found start searching
	 * from the beginning of the list
	 */
retry:
	slots_found = 0;
	if (retry == 0)
		iter = chan->last_used;
	else
		iter = list_entry(&chan->all_slots, ppc460ex_p_desc_t,
			slot_node);
	prefetch(iter);
	list_for_each_entry_safe_continue(iter, _iter, &chan->all_slots,
	    slot_node) {
		prefetch(_iter);
		prefetch(&_iter->async_tx);
		if (iter->slots_per_op) {
			slots_found = 0;
			continue;
		}

		/* start the allocation if the slot is correctly aligned */
		if (!slots_found++)
			alloc_start = iter;
		if (slots_found == num_slots) {
			ppc460ex_p_desc_t *alloc_tail = NULL;
			ppc460ex_p_desc_t *last_used = NULL;
			iter = alloc_start;
			while (num_slots) {
				int i;
	
				/* pre-ack all but the last descriptor */
				if (num_slots != slots_per_op) {
					async_tx_ack(&iter->async_tx); 
				}
#if 0
				else
					/* Don't need to clear. It is hack*/
					async_tx_clear_ack(&iter->async_tx); 
#endif

				list_add_tail(&iter->chain_node, &chain);
				alloc_tail = iter;
				iter->async_tx.cookie = 0;
				iter->hw_next = NULL;
				iter->flags = 0;
				iter->slot_cnt = num_slots;
				for (i = 0; i < slots_per_op; i++) {
					iter->slots_per_op = slots_per_op - i;
					last_used = iter;
					iter = list_entry(iter->slot_node.next,
						ppc460ex_p_desc_t,
						slot_node);
				}
				num_slots -= slots_per_op;
			}
			alloc_tail->group_head = alloc_start;
			alloc_tail->async_tx.cookie = -EBUSY;
			list_splice(&chain, &alloc_tail->group_list);
			chan->last_used = last_used;
			return alloc_tail;
		}
	}
	if (!retry++)
		goto retry;
	static empty_slot_cnt;
	if(!(empty_slot_cnt%100))
		printk(KERN_INFO"No empty slots trying to free some\n");
	empty_slot_cnt++;
	/* try to free some slots if the allocation fails */
	tasklet_schedule(&chan->irq_tasklet);
	return NULL;
}

/**
 * ppc460ex_adma_alloc_chan_resources -  allocate pools for CDB slots
 */
static int ppc460ex_adma_alloc_chan_resources(struct dma_chan *chan)
{
	ppc460ex_p_ch_t *ppc460ex_chan = to_ppc460ex_adma_chan(chan);
	ppc460ex_p_desc_t *slot = NULL;
	char *hw_desc;
	int i, db_sz;
	int init = ppc460ex_chan->slots_allocated ? 0 : 1;
	int pool_size = DMA_FIFO_SIZE * DMA_CDB_SIZE;

	chan->chan_id = ppc460ex_chan->device->id;

	/* Allocate descriptor slots */
	i = ppc460ex_chan->slots_allocated;
	db_sz = sizeof (dma_cdb_t);

	for (; i < (pool_size/db_sz); i++) {
		slot = kzalloc(sizeof(ppc460ex_p_desc_t), GFP_KERNEL);
		if (!slot) {
			printk(KERN_INFO "GT ADMA Channel only initialized"
				" %d descriptor slots", i--);
			break;
		}

		hw_desc = (char *) ppc460ex_chan->dma_desc_pool_virt;
		slot->hw_desc = (void *) &hw_desc[i * db_sz];
		dma_async_tx_descriptor_init(&slot->async_tx, chan);
		slot->async_tx.tx_submit = ppc460ex_adma_tx_submit;
		INIT_LIST_HEAD(&slot->chain_node);
		INIT_LIST_HEAD(&slot->slot_node);
		INIT_LIST_HEAD(&slot->group_list);
		hw_desc = (char *) ppc460ex_chan->dma_desc_pool;
		slot->phys = (dma_addr_t) &hw_desc[i * db_sz];
		slot->idx = i;
		spin_lock_bh(&ppc460ex_chan->lock);
		ppc460ex_chan->slots_allocated++;
		list_add_tail(&slot->slot_node, &ppc460ex_chan->all_slots);
		spin_unlock_bh(&ppc460ex_chan->lock);
	}

	if (i && !ppc460ex_chan->last_used) {
		ppc460ex_chan->last_used =
			list_entry(ppc460ex_chan->all_slots.next,
				ppc460ex_p_desc_t,
				slot_node);
	}

	dev_dbg(ppc460ex_chan->device->common.dev,
		"ppc460ex adma%d: allocated %d descriptor slots\n",
		ppc460ex_chan->device->id, i);

	/* initialize the channel and the chain with a null operation */
	if (init) {
		switch (ppc460ex_chan->chan_id)
		{
		case PPC460EX_PDMA0_ID:
		case PPC460EX_PDMA1_ID:
			ppc460ex_chan->hw_chain_inited = 0;
			/* Use WXOR for self-testing */
			if (!ppc460ex_dma_tchan)
				ppc460ex_dma_tchan = ppc460ex_chan;
			if (!ppc460ex_r6_tchan)
				ppc460ex_r6_tchan = ppc460ex_chan;
			break;
		default:
			BUG();
		}
		ppc460ex_chan->needs_unmap = 1;
	}

	return (i > 0) ? i : -ENOMEM;
}

/**
 * ppc460ex_desc_assign_cookie - assign a cookie
 */
static dma_cookie_t ppc460ex_desc_assign_cookie(ppc460ex_p_ch_t *chan,
		ppc460ex_p_desc_t *desc)
{
	dma_cookie_t cookie = chan->common.cookie;
	cookie++;
	if (cookie < 0)
		cookie = 1;
	chan->common.cookie = desc->async_tx.cookie = cookie;
	return cookie;
}


/**
 * ppc460ex_adma_check_threshold - append CDBs to h/w chain if threshold
 *	has been achieved
 */
static void ppc460ex_adma_check_threshold(ppc460ex_p_ch_t *chan)
{
	dev_dbg(chan->device->common.dev, "ppc460ex adma%d: pending: %d\n",
		chan->device->id, chan->pending);

	if (chan->pending >= PPC460EX_ADMA_THRESHOLD) {
		chan->pending = 0;
		ppc460ex_chan_append(chan);
	}
}

/**
 * ppc460ex_adma_tx_submit - submit new descriptor group to the channel
 *	(it's not necessary that descriptors will be submitted to the h/w
 *	chains too right now)
 */
static dma_cookie_t ppc460ex_adma_tx_submit(struct dma_async_tx_descriptor *tx)
{
	ppc460ex_p_desc_t *sw_desc = tx_to_ppc460ex_adma_slot(tx);
	ppc460ex_p_ch_t *chan = to_ppc460ex_adma_chan(tx->chan);
	ppc460ex_p_desc_t *group_start, *old_chain_tail;
	int slot_cnt;
	int slots_per_op;
	dma_cookie_t cookie;

	group_start = sw_desc->group_head;
	slot_cnt = group_start->slot_cnt;
	slots_per_op = group_start->slots_per_op;

	spin_lock_bh(&chan->lock);

	cookie = ppc460ex_desc_assign_cookie(chan, sw_desc);

	if (unlikely(list_empty(&chan->chain))) {
		/* first peer */
		list_splice_init(&sw_desc->group_list, &chan->chain);
		chan_first_cdb[chan->device->id] = group_start;
	} else {
		/* isn't first peer, bind CDBs to chain */
		old_chain_tail = list_entry(chan->chain.prev,
			ppc460ex_p_desc_t, chain_node);
		list_splice_init(&sw_desc->group_list,
		    &old_chain_tail->chain_node);
		/* fix up the hardware chain */
		ppc460ex_desc_set_link(chan, old_chain_tail, group_start);
	}

	/* increment the pending count by the number of operations */
	chan->pending += slot_cnt / slots_per_op;
	ppc460ex_adma_check_threshold(chan);
	spin_unlock_bh(&chan->lock);

	dev_dbg(chan->device->common.dev,
		"ppc460ex adma%d: %s cookie: %d slot: %d tx %p\n",
		chan->device->id,__FUNCTION__,
		sw_desc->async_tx.cookie, sw_desc->idx, sw_desc);
	return cookie;
}

/**
 * ppc460ex_adma_prep_dma_interrupt - prepare CDB for a pseudo DMA operation
 */
static struct dma_async_tx_descriptor *ppc460ex_adma_prep_dma_interrupt(
		struct dma_chan *chan, unsigned long flags)
{
	ppc460ex_p_ch_t *ppc460ex_chan = to_ppc460ex_adma_chan(chan);
	ppc460ex_p_desc_t *sw_desc, *group_start;
	int slot_cnt, slots_per_op;

	dev_dbg(ppc460ex_chan->device->common.dev,
		"ppc460ex adma%d: %s\n", ppc460ex_chan->device->id,
		__FUNCTION__);

	spin_lock_bh(&ppc460ex_chan->lock);
	slot_cnt = slots_per_op = 1;
	sw_desc = ppc460ex_adma_alloc_slots(ppc460ex_chan, slot_cnt,
			slots_per_op);
	if (sw_desc) {
		group_start = sw_desc->group_head;
		ppc460ex_desc_init_interrupt(group_start, ppc460ex_chan);
		group_start->unmap_len = 0;
		sw_desc->async_tx.flags = flags;
	}
	spin_unlock_bh(&ppc460ex_chan->lock);

	return sw_desc ? &sw_desc->async_tx : NULL;
}

/**
 * ppc460ex_adma_prep_dma_memcpy - prepare CDB for a MEMCPY operation
 */
static struct dma_async_tx_descriptor *ppc460ex_adma_prep_dma_memcpy(
		struct dma_chan *chan, dma_addr_t dma_dest,
		dma_addr_t dma_src, size_t len, unsigned long flags)
{
	ppc460ex_p_ch_t *ppc460ex_chan = to_ppc460ex_adma_chan(chan);
	ppc460ex_p_desc_t *sw_desc, *group_start;
	int slot_cnt, slots_per_op;
	if (unlikely(!len))
		return NULL;
	BUG_ON(unlikely(len > PPC460EX_ADMA_DMA_MAX_BYTE_COUNT));

	spin_lock_bh(&ppc460ex_chan->lock);

	dev_dbg(ppc460ex_chan->device->common.dev,
		"ppc460ex adma%d: %s len: %u int_en %d \n",
		ppc460ex_chan->device->id, __FUNCTION__, len,
		flags & DMA_PREP_INTERRUPT ? 1 : 0);

	slot_cnt = slots_per_op = 1;
	sw_desc = ppc460ex_adma_alloc_slots(ppc460ex_chan, slot_cnt,
		slots_per_op);
	if (sw_desc) {
		group_start = sw_desc->group_head;
		prefetch(group_start);
		ppc460ex_desc_init_memcpy(group_start, flags);
		ppc460ex_desc_set_dest_addr(sw_desc->group_head, chan, dma_dest, 0);
		ppc460ex_desc_set_src_addr(sw_desc->group_head, chan, dma_src, 0);
		ppc460ex_desc_set_byte_count(group_start, ppc460ex_chan, len);
		sw_desc->unmap_len = len;
		sw_desc->async_tx.flags = flags;
			if(mfdcr(0x60) == 0xfee8) {
				printk("Byte Count = 0x%x\n",len);
				printk("src= 0x%x\n",dma_src);
				printk("Dest = 0x%x\n",dma_dest);
			}
	}
	spin_unlock_bh(&ppc460ex_chan->lock);
	return sw_desc ? &sw_desc->async_tx : NULL;
}

/**
 * ppc460ex_adma_prep_dma_memset - prepare CDB for a MEMSET operation
 */
static struct dma_async_tx_descriptor *ppc460ex_adma_prep_dma_memset(
		struct dma_chan *chan, dma_addr_t dma_dest, int value,
		size_t len, unsigned long flags)
{
	ppc460ex_p_ch_t *ppc460ex_chan = to_ppc460ex_adma_chan(chan);
	ppc460ex_p_desc_t *sw_desc, *group_start;
	int slot_cnt, slots_per_op;
	if (unlikely(!len))
		return NULL;
	BUG_ON(unlikely(len > PPC460EX_ADMA_DMA_MAX_BYTE_COUNT));

	spin_lock_bh(&ppc460ex_chan->lock);

	dev_dbg(ppc460ex_chan->device->common.dev,
		"ppc460ex adma%d: %s cal: %u len: %u int_en %d\n",
		ppc460ex_chan->device->id, __FUNCTION__, value, len,
		flags & DMA_PREP_INTERRUPT ? 1 : 0);

	slot_cnt = slots_per_op = 1;
	sw_desc = ppc460ex_adma_alloc_slots(ppc460ex_chan, slot_cnt,
		slots_per_op);
	if (sw_desc) {
		group_start = sw_desc->group_head;
		ppc460ex_desc_init_memset(group_start, value, flags);
		ppc460ex_adma_set_dest(group_start, dma_dest, 0);
		ppc460ex_desc_set_byte_count(group_start, ppc460ex_chan, len);
		sw_desc->unmap_len = len;
		sw_desc->async_tx.flags = flags;
	}
	spin_unlock_bh(&ppc460ex_chan->lock);

	return sw_desc ? &sw_desc->async_tx : NULL;
}


/**
 * ppc460ex_adma_set_dest - set destination address into descriptor
 */
static void ppc460ex_adma_set_dest(ppc460ex_p_desc_t *sw_desc,
		dma_addr_t addr, int index)
{
	ppc460ex_p_ch_t *chan = to_ppc460ex_adma_chan(sw_desc->async_tx.chan);
	BUG_ON(index >= sw_desc->dst_cnt);

	switch (chan->chan_id) {
	case PPC460EX_PDMA0_ID:
	case PPC460EX_PDMA1_ID:
	case PPC460EX_PDMA2_ID:
	case PPC460EX_PDMA3_ID:
		/* to do: support transfers lengths >
		 * PPC460EX_ADMA_DMA/XOR_MAX_BYTE_COUNT
		 */
		ppc460ex_desc_set_dest_addr(sw_desc->group_head,
		//	chan, 0x8, addr, index); // Enabling HB bus
			chan, 0, addr);
		break;
	default:
		BUG();
	}
}




/**
 * ppc460ex_adma_free_chan_resources - free the resources allocated
 */
static void ppc460ex_adma_free_chan_resources(struct dma_chan *chan)
{
	ppc460ex_p_ch_t *ppc460ex_chan = to_ppc460ex_adma_chan(chan);
	ppc460ex_p_desc_t *iter, *_iter;
	int in_use_descs = 0;

	ppc460ex_adma_slot_cleanup(ppc460ex_chan);

	spin_lock_bh(&ppc460ex_chan->lock);
	list_for_each_entry_safe(iter, _iter, &ppc460ex_chan->chain,
					chain_node) {
		in_use_descs++;
		list_del(&iter->chain_node);
	}
	list_for_each_entry_safe_reverse(iter, _iter,
			&ppc460ex_chan->all_slots, slot_node) {
		list_del(&iter->slot_node);
		kfree(iter);
		ppc460ex_chan->slots_allocated--;
	}
	ppc460ex_chan->last_used = NULL;

	dev_dbg(ppc460ex_chan->device->common.dev,
		"ppc460ex adma%d %s slots_allocated %d\n",
		ppc460ex_chan->device->id,
		__FUNCTION__, ppc460ex_chan->slots_allocated);
	spin_unlock_bh(&ppc460ex_chan->lock);

	/* one is ok since we left it on there on purpose */
	if (in_use_descs > 1)
		printk(KERN_ERR "GT: Freeing %d in use descriptors!\n",
			in_use_descs - 1);
}

/**
 * ppc460ex_adma_is_complete - poll the status of an ADMA transaction
 * @chan: ADMA channel handle
 * @cookie: ADMA transaction identifier
 */
static enum dma_status ppc460ex_adma_is_complete(struct dma_chan *chan,
	dma_cookie_t cookie, dma_cookie_t *done, dma_cookie_t *used)
{
	ppc460ex_p_ch_t *ppc460ex_chan = to_ppc460ex_adma_chan(chan);
	dma_cookie_t last_used;
	dma_cookie_t last_complete;
	enum dma_status ret;

	last_used = chan->cookie;
	last_complete = ppc460ex_chan->completed_cookie;

	if (done)
		*done= last_complete;
	if (used)
		*used = last_used;

	ret = dma_async_is_complete(cookie, last_complete, last_used);
	if (ret == DMA_SUCCESS)
		return ret;

	ppc460ex_adma_slot_cleanup(ppc460ex_chan);

	last_used = chan->cookie;
	last_complete = ppc460ex_chan->completed_cookie;

	if (done)
		*done= last_complete;
	if (used)
		*used = last_used;

	return dma_async_is_complete(cookie, last_complete, last_used);
}

/**
 * ppc460ex_adma_eot_handler - end of transfer interrupt handler
 */
static irqreturn_t ppc460ex_adma_eot_handler(int irq, void *data)
{
	ppc460ex_p_ch_t *chan = data;

	dev_dbg(chan->device->common.dev,
		"ppc460ex adma%d: %s\n", chan->device->id, __FUNCTION__);

	tasklet_schedule(&chan->irq_tasklet);
	ppc460ex_adma_device_clear_eot_status(chan);

	return IRQ_HANDLED;
}

/**
 * ppc460ex_adma_err_handler - DMA error interrupt handler;
 *	do the same things as a eot handler
 */
static irqreturn_t ppc460ex_adma_err_handler(int irq, void *data)
{
	ppc460ex_p_ch_t *chan = data;
	dev_dbg(chan->device->common.dev,
		"ppc460ex adma%d: %s\n", chan->device->id, __FUNCTION__);
	tasklet_schedule(&chan->irq_tasklet);
	ppc460ex_adma_device_clear_eot_status(chan);

	return IRQ_HANDLED;
}

static void ppc460ex_test_rad6_callback (void *unused)
{
	complete(&ppc460ex_r6_test_comp);
}
/**
 * ppc460ex_test_callback - called when test operation has been done
 */
static void ppc460ex_test_callback (void *unused)
{
	complete(&ppc460ex_r5_test_comp);
}

/**
 * ppc460ex_adma_issue_pending - flush all pending descriptors to h/w
 */
static void ppc460ex_adma_issue_pending(struct dma_chan *chan)
{
	ppc460ex_p_ch_t *ppc460ex_chan = to_ppc460ex_adma_chan(chan);

	if (ppc460ex_chan->pending) {
	dev_dbg(ppc460ex_chan->device->common.dev,
	    "ppc460ex adma%d: %s %d \n", ppc460ex_chan->device->id,
	    __FUNCTION__, ppc460ex_chan->pending);
		ppc460ex_chan->pending = 0;
		ppc460ex_chan_append(ppc460ex_chan);
	}
}

/**
 * ppc460ex_adma_remove - remove the asynch device
 */
static int __devexit ppc460ex_pdma_remove(struct platform_device *dev)
{
	ppc460ex_p_dev_t *device = platform_get_drvdata(dev);
	struct dma_chan *chan, *_chan;
	struct ppc_dma_chan_ref *ref, *_ref;
	ppc460ex_p_ch_t *ppc460ex_chan;
	int i;

	dma_async_device_unregister(&device->common);

	for (i = 0; i < 3; i++) {
		u32 irq;
		irq = platform_get_irq(dev, i);
		free_irq(irq, device);
	}
	

	do {
		struct resource *res;
		res = platform_get_resource(dev, IORESOURCE_MEM, 0);
		release_mem_region(res->start, res->end - res->start);
	} while (0);

	list_for_each_entry_safe(chan, _chan, &device->common.channels,
				device_node) {
		ppc460ex_chan = to_ppc460ex_adma_chan(chan);
		list_del(&chan->device_node);
		kfree(ppc460ex_chan);
	}

	list_for_each_entry_safe(ref, _ref, &ppc_adma_p_chan_list, node) {
		list_del(&ref->node);
		kfree(ref);
	}

	kfree(device);

	return 0;
}
/*
 * Per channel probe
 */
int __devinit ppc460ex_dma_per_chan_probe(struct of_device *ofdev,
		const struct of_device_id *match)
{
	int ret=0;
	ppc460ex_p_dev_t *adev;
	ppc460ex_p_ch_t *new_chan;
	int err;

	adev = dev_get_drvdata(ofdev->dev.parent);
	BUG_ON(!adev);
	if ((new_chan = kzalloc(sizeof(*new_chan), GFP_KERNEL)) == NULL) {
		 printk("ERROR:No Free memory for allocating dma channels\n");
		 ret = -ENOMEM;
		 goto err;
	}
	err = of_address_to_resource(ofdev->node,0,&new_chan->reg);
	if (err) {
		printk("ERROR:Can't get %s property reg\n", __FUNCTION__);
		goto err;
	}
	new_chan->device = &ofdev->dev;
	new_chan->reg_base = ioremap(new_chan->reg.start,
			new_chan->reg.end - new_chan->reg.start + 1);
	if ((new_chan->dma_desc_pool_virt = dma_alloc_coherent(&ofdev->dev,
	     DMA_FIFO_SIZE << 2, &new_chan->dma_desc_pool, GFP_KERNEL)) == NULL) {
		ret = -ENOMEM;
		goto err_dma_alloc;
	}
		new_chan->chan_id = ((new_chan->reg.start - 0x200)& 0xfff) >> 3;
		adev->chan[new_chan->chan_id] = new_chan;

		return 0;
err:
		 return ret;
err_dma_alloc:
err_chan_alloc:
	kfree(new_chan);
		 return ret;
}
static struct of_device_id dma_4chan_match[] = 
{
	{
		.compatible     = "amcc,dma",
	},
	{},
};
static struct of_device_id dma_per_chan_match[] = {
	{
		.compatible = "amcc,dma-4channel",
	},
	{},
};
/**
 * ppc460ex_adma_probe - probe the asynch device
 */
//static int __devinit ppc460ex_adma_probe(struct platform_device *pdev)
static int __devinit ppc460ex_pdma_probe(struct of_device *ofdev,
		const struct of_device_id *match)
{
	struct resource *res;
	int ret=0, irq; 
	ppc460ex_p_dev_t *adev;
	ppc460ex_p_ch_t *chan;
	struct ppc_dma_chan_ref *ref;


	if ((adev = kzalloc(sizeof(*adev), GFP_KERNEL)) == NULL) {
		 ret = -ENOMEM; 
		 goto err_adev_alloc;
	}
	adev->dev = &ofdev->dev;
	adev->id = PPC460EX_PDMA0_ID;
	/* create the DMA capability MASK . This used to come from resources structure*/
	dma_cap_set(DMA_MEMCPY, adev->common.cap_mask);
	dma_cap_set(DMA_INTERRUPT, adev->common.cap_mask);
	dma_cap_set(DMA_MEMSET, adev->common.cap_mask);
	adev->odev = ofdev;
	dev_set_drvdata(&(ofdev->dev), adev);

	INIT_LIST_HEAD(&adev->common.channels);

	/* set base routines */
	adev->common.device_alloc_chan_resources =
	    ppc460ex_adma_alloc_chan_resources;
	adev->common.device_free_chan_resources =
	    ppc460ex_adma_free_chan_resources;
	adev->common.device_is_tx_complete = ppc460ex_adma_is_complete;
	adev->common.device_issue_pending = ppc460ex_adma_issue_pending;
	adev->common.dev = &ofdev->dev;

	/* set prep routines based on capability */
	if (dma_has_cap(DMA_MEMCPY, adev->common.cap_mask)) {
		adev->common.device_prep_dma_memcpy =
		    ppc460ex_adma_prep_dma_memcpy;
	}
	if (dma_has_cap(DMA_MEMSET, adev->common.cap_mask)) {
		adev->common.device_prep_dma_memset =
		    ppc460ex_adma_prep_dma_memset;
	}

	if (dma_has_cap(DMA_INTERRUPT, adev->common.cap_mask)) {
		adev->common.device_prep_dma_interrupt =
		    ppc460ex_adma_prep_dma_interrupt;
	}

	/* create a channel */
	if ((chan = kzalloc(sizeof(*chan), GFP_KERNEL)) == NULL) {
		ret = -ENOMEM;
		goto err_chan_alloc;
	}

	tasklet_init(&chan->irq_tasklet, ppc460ex_adma_tasklet,
	    (unsigned long)chan);
	irq = irq_of_parse_and_map(ofdev->node, 0);
	printk("<%s> irq=0x%x\n",__FUNCTION__, irq);
	if (irq >= 0) {
		ret = request_irq(irq, ppc460ex_adma_eot_handler,
			IRQF_DISABLED, "adma-chan0", chan);
		if (ret) {
			printk("Failed to request IRQ %d\n",irq);
			ret = -EIO;
			goto err_irq;
		}

		irq = irq_of_parse_and_map(ofdev->node, 1);
		printk("<%s> irq=0x%x\n",__FUNCTION__, irq);
		if (irq >= 0) {
			ret = request_irq(irq, ppc460ex_adma_err_handler,
				IRQF_DISABLED, "adma-chan-1", chan);
			if (ret) {
				printk("Failed to request IRQ %d\n",irq);
				ret = -EIO;
				goto err_irq;
			}
			irq = irq_of_parse_and_map(ofdev->node, 2);
			printk("<%s> irq=0x%x\n",__FUNCTION__, irq);
			if (irq >= 0) {
				ret = request_irq(irq, ppc460ex_adma_err_handler,
					IRQF_DISABLED, "adma-chan2", chan);
				if (ret) {
					printk("Failed to request IRQ %d\n",irq);
					ret = -EIO;
					goto err_irq;
				}
				irq = irq_of_parse_and_map(ofdev->node, 3);
				printk("<%s> irq=0x%x\n",__FUNCTION__, irq);
				if (irq >= 0) {
					ret = request_irq(irq, ppc460ex_adma_err_handler,
						IRQF_DISABLED, "adma-chan3", chan);
					if (ret) {
						printk("Failed to request IRQ %d\n",irq);
						ret = -EIO;
						goto err_irq;
				}

		
				}

		
			}
		
		}
	} else
		ret = -ENXIO;

	chan->device = adev;
	/* pass the platform data */
	spin_lock_init(&chan->lock);
#if 0
	init_timer(&chan->cleanup_watchdog);
	chan->cleanup_watchdog.data = (unsigned long) chan;
	chan->cleanup_watchdog.function = ppc460ex_adma_tasklet;
#endif
	INIT_LIST_HEAD(&chan->chain);
	INIT_LIST_HEAD(&chan->all_slots);
	chan->common.device = &adev->common;
	list_add_tail(&chan->common.device_node, &adev->common.channels);

	dev_dbg(&ofdev->dev,  "AMCC(R) PPC440SP(E) ADMA Engine found [%d]: "
	  "( %s%s%s%s%s%s%s%s%s%s)\n",
	  adev->id,
	  dma_has_cap(DMA_PQ, adev->common.cap_mask) ? "pq_xor " : "",
	  dma_has_cap(DMA_PQ_UPDATE, adev->common.cap_mask) ? "pq_update " : "",
	  dma_has_cap(DMA_PQ_ZERO_SUM, adev->common.cap_mask) ? "pq_zero_sum " :
	    "",
	  dma_has_cap(DMA_XOR, adev->common.cap_mask) ? "xor " : "",
	  dma_has_cap(DMA_DUAL_XOR, adev->common.cap_mask) ? "dual_xor " : "",
	  dma_has_cap(DMA_ZERO_SUM, adev->common.cap_mask) ? "xor_zero_sum " :
	    "",
	  dma_has_cap(DMA_MEMSET, adev->common.cap_mask)  ? "memset " : "",
	  dma_has_cap(DMA_MEMCPY_CRC32C, adev->common.cap_mask) ? "memcpy+crc "
	    : "",
	  dma_has_cap(DMA_MEMCPY, adev->common.cap_mask) ? "memcpy " : "",
	  dma_has_cap(DMA_INTERRUPT, adev->common.cap_mask) ? "int " : "");

	of_platform_bus_probe(ofdev->node, dma_per_chan_match,&ofdev->dev);
	dma_async_device_register(&adev->common);
	ref = kmalloc(sizeof(*ref), GFP_KERNEL);
		printk("<%s> ret=0x%x\n", __FUNCTION__,ret);
	if (ref) {
		ref->chan = &chan->common;
		INIT_LIST_HEAD(&ref->node);
		list_add_tail(&ref->node, &ppc_adma_p_chan_list);
	} else
		printk(KERN_WARNING "%s: failed to allocate channel reference!\n",
		       __FUNCTION__);
	goto out;

err:
	ret = ret;
err_irq:
	kfree(chan);
err_chan_alloc:
err_dma_alloc:
	kfree(adev);
err_adev_alloc:
	release_mem_region(res->start, res->end - res->start);
out:
	return ret;
}

/**
 * ppc460ex_test_dma - test are RAID-5 capabilities enabled successfully.
 *	For this we just perform one WXOR operation with the same source
 *	and destination addresses, the GF-multiplier is 1; so if RAID-5
	o/of_platform_driver_unregister(&ppc460ex_pdma_driver);
 *	capabilities are enabled then we'll get src/dst filled with zero.
 */
static int ppc460ex_test_dma (ppc460ex_p_ch_t *chan)
{
	ppc460ex_p_desc_t *sw_desc, *iter;
	struct page *pg;
	char *a;
	dma_addr_t dma_addr;
	unsigned long op = 0;
	int rval = 0;

	if (!ppc460ex_dma_tchan)
		return -1;
	/*FIXME*/

	pg = alloc_page(GFP_KERNEL);
	if (!pg)
		return -ENOMEM;

	spin_lock_bh(&chan->lock);
	sw_desc = ppc460ex_adma_alloc_slots(chan, 1, 1);
	if (sw_desc) {
		/* 1 src, 1 dsr, int_ena */
		ppc460ex_desc_init_memcpy(sw_desc,0);
		list_for_each_entry(iter, &sw_desc->group_list, chain_node) {
			ppc460ex_desc_set_byte_count(iter, chan, PAGE_SIZE);
			iter->unmap_len = PAGE_SIZE;
		}
	} else {
		rval = -EFAULT;
		spin_unlock_bh(&chan->lock);
		goto exit;
	}
	spin_unlock_bh(&chan->lock);

	/* Fill the test page with ones */
	memset(page_address(pg), 0xFF, PAGE_SIZE);
	int i = 0;
	char *pg_addr = page_address(pg);
#if 0
	for(i=0;i < PAGE_SIZE; i+=64)
		printk("addr = 0x%x data = 0x%x\n",pg_addr + i,*(pg_addr+i));
#endif
	//dma_addr = dma_map_page(&chan->device->common, pg, 0, PAGE_SIZE,
	dma_addr = dma_map_page(&chan->device->odev->dev, pg, 0, PAGE_SIZE,
	    DMA_BIDIRECTIONAL);

	/* Setup adresses */
	ppc460ex_desc_set_src_addr(sw_desc, chan, dma_addr, 0);
	ppc460ex_desc_set_dest_addr(sw_desc, chan, dma_addr, 0);

	async_tx_ack(&sw_desc->async_tx);
	sw_desc->async_tx.callback = ppc460ex_test_callback;
	sw_desc->async_tx.callback_param = NULL;

	init_completion(&ppc460ex_r5_test_comp);

	ppc460ex_adma_tx_submit(&sw_desc->async_tx);
	ppc460ex_adma_issue_pending(&chan->common);

	wait_for_completion(&ppc460ex_r5_test_comp);

	/*Make sure cache is flushed to memory*/
	dma_addr = dma_map_page(&chan->device->odev->dev, pg, 0, PAGE_SIZE,
	    DMA_BIDIRECTIONAL);
	/* Now check is the test page zeroed */
	a = page_address(pg);
#if 0
	i = 0;
	for(i=0;i < PAGE_SIZE; i+=64)
		printk("addr = 0x%x data = 0x%x\n",a + i,*(a+i));
#endif
	if ((*(u32*)a) == 0 && memcmp(a, a+4, PAGE_SIZE-4)==0) {
		/* page is zero - RAID-5 enabled */
		rval = 0;
	} else {
		/* RAID-5 was not enabled */
		rval = -EINVAL;
	}
	pr_dma(__LINE__,__FUNCTION__);
exit:
	__free_page(pg);
	return rval;
}


static struct of_platform_driver ppc460ex_pdma_driver = {
	.name		= "plb_dma",
	.match_table = dma_4chan_match,

	.probe		= ppc460ex_pdma_probe,
	.remove		= ppc460ex_pdma_remove,
};
struct of_platform_driver ppc460ex_dma_per_chan_driver = {
	.name = "dma-4channel",
	.match_table = dma_per_chan_match,
	.probe = ppc460ex_dma_per_chan_probe,
};

static int ppc460ex_dma_read (char *page, char **start, off_t off,
	int count, int *eof, void *data)
{
	char *p = page;

	p += sprintf(p, "%s\n",
		ppc460ex_r5_enabled ?
		"PPC460Ex RAID-r5 capabilities are ENABLED.\n" :
		"PPC460Ex RAID-r5 capabilities are DISABLED.\n");

	return p - page;
}

static int ppc460ex_dma_write (struct file *file, const char __user *buffer,
	unsigned long count, void *data)
{
	/* e.g. 0xffffffff */
	char tmp[11];
	unsigned long val;

	if (!count || count > 11)
		return -EINVAL;

	if (!ppc460ex_dma_tchan)
		return -EFAULT;

	if (copy_from_user(tmp, buffer, count))
		return -EFAULT;

	/* Write a key */
	val = simple_strtoul(tmp, NULL, 16);
	if(!strcmp(val,"copy"))
		printk("Testing copy feature");
	/* Verify does it really work now */
	if (ppc460ex_test_dma(ppc460ex_dma_tchan) == 0) {
		/* PPC440SP(e) RAID-6 has been activated successfully */;
		printk(KERN_INFO "PPC460Ex RAID-5 has been activated "
		    "successfully\n");
		ppc460ex_r5_enabled = 1;
		ppc460ex_r6_enabled = 1;
	} else {
		/* PPC440SP(e) RAID-6 hasn't been activated! Error key ? */;
		printk(KERN_INFO "PPC460Ex RAID-5 hasn't been activated!"
		    " Error key ?\n");
		ppc460ex_r5_enabled = 0;
	}

	return count;
}

static int __init ppc460ex_adma_init (void)
{
	int rval;
	struct proc_dir_entry *p;

	rval = of_register_platform_driver(&ppc460ex_pdma_driver);

	if (rval == 0) {
		/* Create /proc entries */
		ppc460ex_proot = proc_mkdir(PPC460EX_DMA_PROC_ROOT, NULL);
		if (!ppc460ex_proot) {
			printk(KERN_ERR "%s: failed to create %s proc "
			    "directory\n",__FUNCTION__,PPC460EX_DMA_PROC_ROOT);
			/* User will not be able to enable h/w RAID-6 */
			return rval;
		}

		/* RAID-6 h/w enable entry */
		p = create_proc_entry("enable", 0, ppc460ex_proot);
		if (p) {
			p->read_proc = ppc460ex_dma_read;
			p->write_proc = ppc460ex_dma_write;
		}
	}
	return rval;
}

#if 0
static void __exit ppc460ex_adma_exit (void)
{
	of_unregister_platform_driver(&ppc460ex_pdma_driver);
	return;
}
module_exit(ppc460ex_adma_exit);
#endif

module_init(ppc460ex_adma_init);

MODULE_AUTHOR("Tirumala Marri<tmarri@amcc.com>");
MODULE_DESCRIPTION("PPC460EX ADMA Engine Driver");
MODULE_LICENSE("GPL");
