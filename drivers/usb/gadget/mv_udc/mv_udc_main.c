/*******************************************************************************
Copyright (C) Marvell International Ltd. and its affiliates

This software file (the "File") is owned and distributed by Marvell 
International Ltd. and/or its affiliates ("Marvell") under the following
alternative licensing terms.  Once you have made an election to distribute the
File under one of the following license alternatives, please (i) delete this
introductory statement regarding license alternatives, (ii) delete the two
license alternatives that you have not elected to use and (iii) preserve the
Marvell copyright notice above.

********************************************************************************
Marvell Commercial License Option

If you received this File from Marvell and you have entered into a commercial
license agreement (a "Commercial License") with Marvell, the File is licensed
to you under the terms of the applicable Commercial License.

********************************************************************************
Marvell GPL License Option

If you received this File from Marvell, you may opt to use, redistribute and/or 
modify this File in accordance with the terms and conditions of the General 
Public License Version 2, June 1991 (the "GPL License"), a copy of which is 
available along with the File in the license.txt file or by writing to the Free 
Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 or 
on the worldwide web at http://www.gnu.org/licenses/gpl.txt. 

THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE IMPLIED 
WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE ARE EXPRESSLY 
DISCLAIMED.  The GPL License provides additional details about this warranty 
disclaimer.
********************************************************************************
Marvell BSD License Option

If you received this File from Marvell, you may opt to use, redistribute and/or 
modify this File under the following licensing terms. 
Redistribution and use in source and binary forms, with or without modification, 
are permitted provided that the following conditions are met:

    *   Redistributions of source code must retain the above copyright notice,
        this list of conditions and the following disclaimer. 

    *   Redistributions in binary form must reproduce the above copyright
        notice, this list of conditions and the following disclaimer in the
        documentation and/or other materials provided with the distribution. 

    *   Neither the name of Marvell nor the names of its contributors may be 
        used to endorse or promote products derived from this software without 
        specific prior written permission. 
    
THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND 
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE 
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR 
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES 
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; 
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON 
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS 
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*******************************************************************************/

#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/pci.h>
#include <linux/io.h>
#include <linux/mbus.h>
#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>
#include <plat/mv_udc.h>

#include "mvUsbDevApi.h"
#include "mvUsbCh9.h"

#define mv_udc_read(off)	readl(the_controller->regs + (off))
#define mv_udc_write(off, val)	writel((val), the_controller->regs + (off))

#define USB_CMD			0x140
#define USB_MODE		0x1a8
#define USB_CAUSE		0x310
#define USB_MASK		0x314
#define USB_WINDOW_CTRL(i)	(0x320 + ((i) << 4))
#define USB_WINDOW_BASE(i)	(0x324 + ((i) << 4))
#define USB_IPG			0x360
#define USB_PHY_PWR_CTRL	0x400
#define USB_PHY_TX_CTRL		0x420
#define USB_PHY_RX_CTRL		0x430
#define USB_PHY_IVREF_CTRL	0x440
#define USB_PHY_TST_GRP_CTRL	0x450


#ifdef CONFIG_SMP
#define MV_SPIN_LOCK_IRQSAVE(spinlock,flags) \
if(!in_interrupt())  \
spin_lock_irqsave(spinlock, flags)

#define MV_SPIN_UNLOCK_IRQRESTORE(spinlock,flags) \
if(!in_interrupt())  \
spin_unlock_irqrestore(spinlock, flags)

#else /* CONFIG_SMP */

#define MV_SPIN_LOCK_IRQSAVE(spinlock,flags) spin_lock_irqsave(spinlock, flags)
#define MV_SPIN_UNLOCK_IRQRESTORE(spinlock,flags) spin_unlock_irqrestore(spinlock, flags)

#endif /* CONFIG_SMP */


#undef DEBUG

#ifdef DEBUG
#define DBGMSG(fmt,args...)    \
             printk(fmt , ## args)
#else
#   define DBGMSG(fmt,args...)
#endif /* DEBUG */

#define DRIVER_VERSION  "05-July-2006"
#define DRIVER_DESC "Marvell Gadget USB Peripheral Controller"

struct mv_usb_dev;

struct mv_usb_ep 
{
    struct usb_ep       ep;
    struct mv_usb_dev*  usb_dev;
    struct list_head    req_list;
    unsigned            num : 8,
                        is_enabled : 1,
                        is_in : 1;
};

struct mv_usb_dev 
{
    /* each pci device provides one gadget, several endpoints */
    struct usb_gadget           gadget;
    spinlock_t                  lock;
    struct usb_gadget_driver    *driver;
    struct mv_usb_ep            ep[2*ARC_USB_MAX_ENDPOINTS];
    unsigned                    enabled : 1,
                                protocol_stall : 1,
                                got_irq : 1;
    u16                         chiprev;
    struct device               *dev; 
    void*                       mv_usb_handle;
    unsigned char               vbus_gpp_no;
    
    struct mv_udc_platform_data *pd;
    int irq;
    void __iomem *regs;
    u64 rsrc_start;
    u64 rsrc_len;
};

static struct mv_usb_dev*   the_controller = NULL;

static const char driver_name [] = "mv_udc";
static const char driver_desc [] = DRIVER_DESC;

static char ep_names [2*ARC_USB_MAX_ENDPOINTS][10] = 
{
    "ep0out", "ep0in",
};

 

static struct usb_ep_ops mv_usb_ep_ops;

extern void   _usb_dci_vusb20_isr(pointer);

static void* mvUsbMalloc(unsigned int size)
{
    return kmalloc(size,GFP_ATOMIC);
}

static void mvUsbFree(void* buf)
{
    return kfree(buf);
}

static void* mvUsbIoUncachedMalloc( void* pDev, unsigned int size, unsigned int alignment, 
                                    unsigned long* pPhyAddr )
{
    return pci_alloc_consistent( pDev, size+alignment, (dma_addr_t *)pPhyAddr );
}

static void mvUsbIoUncachedFree( void* pDev, unsigned int size, unsigned long phyAddr, void* pVirtAddr )
{
    return pci_free_consistent( pDev, size, pVirtAddr, (dma_addr_t)phyAddr );
} 

static unsigned long mvUsbCacheInvalidate( void* pDev, void* p, int size )
{
    return pci_map_single( pDev, p, size, PCI_DMA_FROMDEVICE );
}

static unsigned long mvUsbCacheFlush( void* pDev, void* p, int size )
{
    return pci_map_single( pDev, p, size, PCI_DMA_TODEVICE );
}

static unsigned long mvUsbVirtToPhys(void* pDev, void* pVirtAddr)
{
    return virt_to_phys(pVirtAddr);
}

static void   usbDevResetComplete(int devNo)
{
    /* Set USB_MODE register */
    mv_udc_write(USB_MODE, 0xa); 
}

static unsigned int mvUsbGetCapRegAddr(int devNo)
{
	return (unsigned int)(the_controller->regs + 0x100);
}

USB_IMPORT_FUNCS    usbImportFuncs =
{
    printk,
    sprintf,
    mvUsbIoUncachedMalloc,
    mvUsbIoUncachedFree,
    mvUsbMalloc,
    mvUsbFree,
    memset,
    memcpy,
    mvUsbCacheFlush,
    mvUsbCacheInvalidate,
    mvUsbVirtToPhys,
    NULL,
    NULL,
    mvUsbGetCapRegAddr,
    usbDevResetComplete
};

static void mv_udc_conf_mbus_windows(struct mbus_dram_target_info *dram)
{
	int i;

	for (i = 0; i < 4; i++) {
		mv_udc_write(USB_WINDOW_CTRL(i), 0);
		mv_udc_write(USB_WINDOW_BASE(i), 0);
	}

	for (i = 0; i < dram->num_cs; i++) {
		struct mbus_dram_window *cs = dram->cs + i;

		mv_udc_write(USB_WINDOW_CTRL(i), ((cs->size - 1) & 0xffff0000) |
								(cs->mbus_attr << 8) |
								(dram->mbus_dram_target_id << 4) | 1);
		mv_udc_write(USB_WINDOW_BASE(i), cs->base);
	}
}

static void mv_udc_setup(unsigned int device_mode)
{
	if(device_mode)
	{
		mv_udc_write(USB_PHY_PWR_CTRL, mv_udc_read(USB_PHY_PWR_CTRL) | (7 << 24));
		mv_udc_write(USB_MODE, 0xa);
	}
	else
	{
		mv_udc_write(USB_PHY_PWR_CTRL, mv_udc_read(USB_PHY_PWR_CTRL) & ~(7 << 24));
		mv_udc_write(USB_MODE, 0x3);
	}
}

static void mv_usb_ep_cancel_all_req(struct mv_usb_ep *mv_ep)
{
    struct mv_usb_dev*      mv_dev = mv_ep->usb_dev;
    struct usb_request*     usb_req;
    int                     req_cntr, tr_cntr;

    req_cntr = tr_cntr = 0;

    /* Cancel all transfers */
    while(_usb_device_get_transfer_status(mv_dev->mv_usb_handle, mv_ep->num, 
           mv_ep->is_in ? ARC_USB_SEND : ARC_USB_RECV) != ARC_USB_STATUS_IDLE)
   {
        tr_cntr++;
       _usb_device_cancel_transfer(mv_dev->mv_usb_handle, mv_ep->num, 
                           mv_ep->is_in ? ARC_USB_SEND : ARC_USB_RECV);
    }
/*
    if(tr_cntr > 0)
    {
        DBGMSG("Cancel ALL transfers: ep=%d-%s, %d transfers\n", 
                        mv_ep->num, mv_ep->is_in ? "in" : "out", tr_cntr);
    }
*/
    while (!list_empty (&mv_ep->req_list)) 
    {
        usb_req = list_entry (mv_ep->req_list.next, struct usb_request, list);

        /* Dequeue request and call complete function */
        list_del_init (&usb_req->list);

        if (usb_req->status == -EINPROGRESS)
            usb_req->status = -ESHUTDOWN;

        usb_req->complete (&mv_ep->ep, usb_req);
        req_cntr++;
        if(req_cntr >= MAX_XDS_FOR_TR_CALLS)
            break;
    }
/*
    if(req_cntr > 0)
    {
        DBGMSG("Cancel ALL Requests: ep=%d-%s, %d requests\n", 
                        mv_ep->num, mv_ep->is_in ? "in" : "out", req_cntr);
        _usb_stats(mv_dev->mv_usb_handle);
    }
*/
}

static uint_8 mv_usb_start_ep0(struct mv_usb_dev *mv_dev)
{
    DBGMSG("%s: mv_dev=%p, mv_usb_handle=%p, mv_ep=%p, usb_ep=%p\n", 
           __FUNCTION__, mv_dev, mv_dev->mv_usb_handle, &mv_dev->ep[0], &mv_dev->ep[0].ep);

    /* Init ep0 IN and OUT */
    mv_dev->ep[0].is_enabled = 1;

    _usb_device_init_endpoint(mv_dev->mv_usb_handle, 0, mv_dev->ep[0].ep.maxpacket, 
                                ARC_USB_SEND,  ARC_USB_CONTROL_ENDPOINT, 0);

    _usb_device_init_endpoint(mv_dev->mv_usb_handle, 0, mv_dev->ep[0].ep.maxpacket, 
                                ARC_USB_RECV, ARC_USB_CONTROL_ENDPOINT, 0);

    return USB_OK;
}


static void   mv_usb_ep_init(struct mv_usb_ep *ep, int num, int is_in)

{

    sprintf(&ep_names[num*2+is_in][0], "ep%d%s", num, is_in ? "in" : "out");

    ep->ep.name = &ep_names[num*2+is_in][0];



    ep->num = num;

    ep->is_in = is_in;

    ep->is_enabled = 0;



    INIT_LIST_HEAD (&ep->req_list);

    

    ep->ep.maxpacket = ~0;

    ep->ep.ops = &mv_usb_ep_ops;

}


static uint_8 mv_usb_reinit (struct mv_usb_dev *usb_dev)
{
    int                 i, ep_num;
    struct mv_usb_ep    *ep;

    DBGMSG("%s: mv_dev=%p, mv_usb_handle=%p\n", 
           __FUNCTION__, usb_dev, usb_dev->mv_usb_handle);

    INIT_LIST_HEAD (&usb_dev->gadget.ep_list);



    /* Enumerate IN endpoints */

    ep_num = 1;

    for(i=0; i<_usb_device_get_max_endpoint(usb_dev->mv_usb_handle); i++)

    {

        ep = &usb_dev->ep[ep_num*2+1];

        if (ep_num != 0)

        {

            INIT_LIST_HEAD(&ep->ep.ep_list);

            list_add_tail (&ep->ep.ep_list, &usb_dev->gadget.ep_list);

        }

        mv_usb_ep_init(ep, ep_num, 1);

        ep->usb_dev = usb_dev;



        ep_num++;

        if(ep_num == _usb_device_get_max_endpoint(usb_dev->mv_usb_handle))

            ep_num = 0;

    }


    /* Enumerate OUT endpoints */

    ep_num = 1;
    for(i=0; i<_usb_device_get_max_endpoint(usb_dev->mv_usb_handle); i++)
    {
        ep = &usb_dev->ep[ep_num*2];

        if (ep_num != 0)

        {

            INIT_LIST_HEAD(&ep->ep.ep_list);

            list_add_tail (&ep->ep.ep_list, &usb_dev->gadget.ep_list);

        }

        mv_usb_ep_init(ep, ep_num, 0);

        ep->usb_dev = usb_dev;



        ep_num++;

        if(ep_num == _usb_device_get_max_endpoint(usb_dev->mv_usb_handle))

            ep_num = 0;

    }
    usb_dev->ep[0].ep.maxpacket = 64;
    usb_dev->gadget.ep0 = &usb_dev->ep[0].ep;
    INIT_LIST_HEAD (&usb_dev->gadget.ep0->ep_list);
    return USB_OK;
}

void mv_usb_bus_reset_service(void*      handle, 
                               uint_8     type, 
                               boolean    setup,
                               uint_8     direction, 
                               uint_8_ptr buffer,
                               uint_32    length, 
                               uint_8     error)
{
    int                     i;
    struct mv_usb_dev       *mv_dev = the_controller;
    struct mv_usb_ep        *mv_ep;

    if(setup == 0)
    {
        /* mv_usb_show(mv_dev, 0x3ff); */

        /* Stop Hardware and cancel all pending requests */
        for (i=0; i<2*_usb_device_get_max_endpoint(handle); i++)
        {
            mv_ep = &mv_dev->ep[i];

            if(mv_ep->is_enabled == 0)
                continue;

            mv_usb_ep_cancel_all_req(mv_ep);
        }
        /* If connected call Function disconnect callback */
        if( (mv_dev->gadget.speed != USB_SPEED_UNKNOWN) && 
            (mv_dev->driver != NULL) &&
            (mv_dev->driver->disconnect != NULL) )

        {
/*
            USB_printf("USB gadget device disconnect or port reset: frindex=0x%x\n",
                    MV_REG_READ(MV_USB_CORE_FRAME_INDEX_REG(dev_no)) );    
*/
            mv_dev->driver->disconnect (&mv_dev->gadget);
        }
        mv_dev->gadget.speed = USB_SPEED_UNKNOWN;

        /* Reinit all endpoints */
        mv_usb_reinit(mv_dev);
    }
    else
    {
        _usb_device_start(mv_dev->mv_usb_handle);
        /* Restart Control Endpoint #0 */
        mv_usb_start_ep0(mv_dev);
    }
}


void mv_usb_speed_service(void*      handle, 
                           uint_8     type, 
                           boolean    setup,
                           uint_8     direction, 
                           uint_8_ptr buffer,
                           uint_32    length, 
                           uint_8     error)
{
    struct mv_usb_dev       *mv_dev = the_controller;

    DBGMSG("Speed = %s\n", (length == ARC_USB_SPEED_HIGH) ? "High" : "Full");

    if(length == ARC_USB_SPEED_HIGH)
        mv_dev->gadget.speed = USB_SPEED_HIGH;
    else
        mv_dev->gadget.speed = USB_SPEED_FULL;

    return;
}

void mv_usb_suspend_service(void*      handle, 
                            uint_8     type, 
                            boolean    setup,
                            uint_8     direction, 
                            uint_8_ptr buffer,
                            uint_32    length, 
                            uint_8     error)
{
    struct mv_usb_dev       *mv_dev = the_controller;

    DBGMSG("%s\n", __FUNCTION__);

    if( (mv_dev->driver != NULL) &&
        (mv_dev->driver->suspend != NULL) )
        mv_dev->driver->suspend (&mv_dev->gadget);
}

void mv_usb_resume_service(void*      handle, 
                            uint_8     type, 
                            boolean    setup,
                            uint_8     direction, 
                            uint_8_ptr buffer,
                            uint_32    length, 
                            uint_8     error)
{
    struct mv_usb_dev       *mv_dev = the_controller;

    DBGMSG("%s\n", __FUNCTION__);

    if( (mv_dev->driver != NULL) &&
        (mv_dev->driver->resume != NULL) )
        mv_dev->driver->resume (&mv_dev->gadget);
}

void mv_usb_tr_complete_service(void*      handle, 
                                 uint_8     type, 
                                 boolean    setup,
                                 uint_8     direction, 
                                 uint_8_ptr buffer,
                                 uint_32    length, 
                                 uint_8     error)
{
    struct mv_usb_dev       *mv_dev = the_controller;
    struct mv_usb_ep       *mv_ep;
    struct usb_request      *usb_req;
    int                     ep_num = (type*2) + direction;

    DBGMSG("%s: ep_num=%d, setup=%s, direction=%s, pBuf=0x%x, length=%d, error=0x%x\n", 
             __FUNCTION__, type, setup ? "YES" : "NO", 
             (direction == ARC_USB_RECV) ? "RECV" : "SEND", 
             (unsigned)buffer, (int)length, error);

    mv_ep = &mv_dev->ep[ep_num];
    if( !list_empty(&mv_ep->req_list) )
    {
        usb_req = list_entry (mv_ep->req_list.next, struct usb_request, list);
        if(usb_req->buf != buffer)
        {
                DBGMSG("ep=%d-%s: req=%p, Unexpected buffer pointer: %p, len=%d, expected=%p\n", 
                    ep_num, (direction == ARC_USB_RECV) ? "out" : "in",
                    usb_req, buffer, length, usb_req->buf);
                return;       
        }
        /* Dequeue request and call complete function */
        list_del_init (&usb_req->list);

        usb_req->actual += length;
        usb_req->status = error;

        usb_req->complete (&mv_ep->ep, usb_req);
    }
    else
        DBGMSG("ep=%p, epName=%s, epNum=%d - reqList EMPTY\n", 
                mv_ep, mv_ep->ep.name, mv_ep->num);
}

void mv_usb_ep0_complete_service(void*      handle, 
                                 uint_8     type, 
                                 boolean    setup,
                                 uint_8     direction, 
                                 uint_8_ptr buffer,
                                 uint_32    length, 
                                 uint_8     error)
{ /* Body */
    struct mv_usb_dev       *mv_dev = the_controller;
    struct mv_usb_ep       *mv_ep;
    struct usb_request*     usb_req;
    int                     rc;
    boolean                 is_delegate = FALSE;
    SETUP_STRUCT            ctrl_req_org;
    static SETUP_STRUCT     mv_ctrl_req;
   
    DBGMSG("%s: EP0(%d), setup=%s, direction=%s, pBuf=0x%x, length=%d, error=0x%x\n", 
                __FUNCTION__, type, setup ? "YES" : "NO", 
                (direction == ARC_USB_RECV) ? "RECV" : "SEND", 
                (unsigned)buffer, (int)length, error);

    mv_ep = &mv_dev->ep[type];

    if (setup) 
    {
        _usb_device_read_setup_data(handle, type, (u8 *)&ctrl_req_org);
        mv_ctrl_req.REQUESTTYPE = ctrl_req_org.REQUESTTYPE;
        mv_ctrl_req.REQUEST = ctrl_req_org.REQUEST;
        mv_ctrl_req.VALUE = le16_to_cpu (ctrl_req_org.VALUE);
        mv_ctrl_req.INDEX = le16_to_cpu (ctrl_req_org.INDEX);
        mv_ctrl_req.LENGTH = le16_to_cpu (ctrl_req_org.LENGTH);

        while(_usb_device_get_transfer_status(handle, mv_ep->num, 
                ARC_USB_SEND) != ARC_USB_STATUS_IDLE)
        {
            _usb_device_cancel_transfer(mv_dev->mv_usb_handle, mv_ep->num, 
                           ARC_USB_SEND);
        }
        while(_usb_device_get_transfer_status(handle, mv_ep->num, 
                ARC_USB_RECV) != ARC_USB_STATUS_IDLE)
        {
            _usb_device_cancel_transfer(mv_dev->mv_usb_handle, mv_ep->num, 
                           ARC_USB_RECV);
        }
        /* make sure any leftover request state is cleared */
        while (!list_empty (&mv_ep->req_list)) 
        {
            usb_req = list_entry (mv_ep->req_list.next, struct usb_request, list);

            /* Dequeue request and call complete function */
            list_del_init (&usb_req->list);

            if (usb_req->status == -EINPROGRESS)
                usb_req->status = -EPROTO;

            usb_req->complete (&mv_ep->ep, usb_req);
        }
    }
    /* Setup request direction */
    mv_ep->is_in = (mv_ctrl_req.REQUESTTYPE & REQ_DIR_IN) != 0;     

    if(setup)
        DBGMSG("Setup: dir=%s, reqType=0x%x, req=0x%x, value=0x%02x, index=0x%02x, length=0x%02x\n", 
                (direction == ARC_USB_SEND) ? "In" : "Out",
                mv_ctrl_req.REQUESTTYPE, mv_ctrl_req.REQUEST, mv_ctrl_req.VALUE,
                mv_ctrl_req.INDEX, mv_ctrl_req.LENGTH); 

    /* Handle most lowlevel requests;
     * everything else goes uplevel to the gadget code.
     */
    if( (mv_ctrl_req.REQUESTTYPE & REQ_TYPE_MASK) == REQ_TYPE_STANDARD)
    {
        switch (mv_ctrl_req.REQUEST) 
        {
            case REQ_GET_STATUS: 
                mvUsbCh9GetStatus(handle, setup, &mv_ctrl_req);
                break;

            case REQ_CLEAR_FEATURE:
                mvUsbCh9ClearFeature(handle, setup, &mv_ctrl_req);
                break;

            case REQ_SET_FEATURE:
                mvUsbCh9SetFeature(handle, setup, &mv_ctrl_req);
                break;

            case REQ_SET_ADDRESS:
                mvUsbCh9SetAddress(handle, setup, &mv_ctrl_req);
                break;

            default:
                /* All others delegate call up-layer gadget code */
                is_delegate = TRUE;
        }
    }
    else
        is_delegate = TRUE;

    /* delegate call up-layer gadget code */
    if(is_delegate)
    {
        if(setup)
        {
            rc = mv_dev->driver->setup (&mv_dev->gadget, (struct usb_ctrlrequest*)&ctrl_req_org);
            if(rc < 0)
            {
                DBGMSG("Setup is failed: rc=%d, req=0x%02x, reqType=0x%x, value=0x%04x, index=0x%04x\n", 
                    rc, ctrl_req_org.REQUEST, ctrl_req_org.REQUESTTYPE, 
                    ctrl_req_org.VALUE, ctrl_req_org.INDEX);
                _usb_device_stall_endpoint(handle, 0, ARC_USB_RECV);
                return;
            }
            /* Acknowledge  */
            if( mv_ep->is_in ) {
                _usb_device_recv_data(handle, 0, NULL, 0);
            } 
            else if( mv_ctrl_req.LENGTH ) {
                _usb_device_send_data(handle, 0, NULL, 0);
            }
        }
    }

    if(!setup)
    {
        if( !list_empty(&mv_ep->req_list) )
        {
            usb_req = list_entry (mv_ep->req_list.next, struct usb_request, list);

            /* Dequeue request and call complete function */
            list_del_init (&usb_req->list);

            usb_req->actual = length;
            usb_req->status = error;
            usb_req->complete (&mv_ep->ep, usb_req);
        }
        DBGMSG("Setup complete: dir=%s, is_in=%d, length=%d\n", 
                (direction == ARC_USB_SEND) ? "In" : "Out",
                mv_ep->is_in, length);
    }
}

static void mvGppPolaritySet(unsigned int pin, unsigned int value)
{
	u32 u;
	
	u = readl(GPIO_IN_POL(pin));
	u &= ~(1 << (pin & 31));
	u |= (value & (1 << (pin & 31)));
	writel(u, GPIO_IN_POL(pin));
}

static unsigned int mvGppPolarityGet(unsigned int pin)
{
	unsigned int val;
	
	val = readl(GPIO_IN_POL(pin));
	return (val & (1 << (pin % 31)));
}

static unsigned int mvGppValueGet(unsigned int pin)
{
	unsigned int val;
	
	val = readl(GPIO_DATA_IN(pin));
	val &= 1 << (pin & 31);
	return val;
}

static int mvUsbBackVoltageUpdate(unsigned char gppNo)
{
    int     vbusChange = 0;
    unsigned int  gppData, regVal, gppInv;
    
    gppInv = mvGppPolarityGet(gppNo);
    gppData = mvGppValueGet(gppNo);

    regVal = mv_udc_read(USB_PHY_PWR_CTRL);

    if( (gppInv == 0) &&
        (gppData != 0) )
    {
        /* VBUS appear */
        regVal |= (7 << 24);
        
        gppInv |= (1 << (gppNo & 31));
	    
        /* DBGMSG("VBUS appear: gpp=%d\n", gppNo); */
        vbusChange = 1;
    }
    else if( (gppInv != 0) &&
             (gppData != 0) )
    {
        /* VBUS disappear */
        regVal &= ~(7 << 24);
        
        gppInv &= ~(1 << (gppNo & 31));

        /* DBGMSG("VBUS disappear: gpp=%d\n", gppNo); */
        vbusChange = 2;
    }
    else
    {
        /* No changes */
        /* DBGMSG("VBUS no changes: gpp=%d\n", gppNo); */
        return vbusChange;
    }
    mv_udc_write(USB_PHY_PWR_CTRL, regVal);
    
    mvGppPolaritySet(gppNo, gppInv);

    return vbusChange;
}

static irqreturn_t mv_usb_vbus_irq (int irq, void *_dev)
{
    struct mv_usb_dev       *mv_dev = _dev;
    int                     vbus_change;

    vbus_change = mvUsbBackVoltageUpdate((int)mv_dev->vbus_gpp_no);
    if(vbus_change == 2)
    {
        if( (mv_dev->gadget.speed != USB_SPEED_UNKNOWN) &&
            (mv_dev->driver != NULL)                    &&
            (mv_dev->driver->disconnect != NULL) )
            mv_dev->driver->disconnect (&mv_dev->gadget);
    }

    return IRQ_HANDLED;
}

static irqreturn_t mv_usb_dev_irq (int irq, void *_dev)
{
    struct mv_usb_dev       *mv_dev = _dev;

    spin_lock (&mv_dev->lock);

    /* handle ARC USB Device interrupts */
    _usb_dci_vusb20_isr(mv_dev->mv_usb_handle);

    spin_unlock (&mv_dev->lock);

    return IRQ_HANDLED;
}

/* when a driver is successfully registered, it will receive
 * control requests including set_configuration(), which enables
 * non-control requests.  then usb traffic follows until a
 * disconnect is reported.  then a host may connect again, or
 * the driver might get unbound.
 */
int usb_gadget_register_driver (struct usb_gadget_driver *driver)
{
    int                 retval;
    struct mv_usb_dev   *mv_dev = NULL;
    uint_8              error;

/*    DBGMSG("ENTER usb_gadget_register_driver: \n");*/

    /* Find USB Gadget device controller */
    mv_dev = the_controller;

    if ( (driver == NULL)
            || (driver->speed != USB_SPEED_HIGH)
            || !driver->bind
            || !driver->unbind
            || !driver->setup)
    {
    		printk("%s: driver: %p, speed:%d\n", __func__, driver, USB_SPEED_HIGH);
        DBGMSG("ERROR: speed=%d, bind=%p,  unbind=%p, setup=%p\n",
                    driver->speed, driver->bind, driver->unbind, driver->setup);
        return -EINVAL;
    }

    if (!mv_dev)
    {
        DBGMSG("ERROR: mv_dev=%p\n", mv_dev);
        return -ENODEV;
    }
    if (mv_dev->driver)
    {
        DBGMSG("ERROR: driver=%p is busy\n", mv_dev->driver);
        return -EBUSY;
    }
/*
    DBGMSG("usb_gadget_register_driver: mv_dev=%p, pDriver=%p\n", 
                mv_dev, driver);
*/
    /* first hook up the driver ... */
    mv_dev->driver = driver;
    mv_dev->gadget.dev.driver = &driver->driver;
    retval = driver->bind (&mv_dev->gadget);
    if (retval) {
        DBGMSG("bind to driver %s --> %d\n",
                driver->driver.name, retval);
        mv_dev->driver = 0;
        mv_dev->gadget.dev.driver = 0;
        return retval;
    }

    /* request_irq */
    if (request_irq (mv_dev->irq, mv_usb_dev_irq, IRQF_DISABLED, 
                     driver_name, mv_dev) != 0) 
    {
        DBGMSG("%s register: request interrupt %d failed\n", 
                driver->driver.name, mv_dev->irq);
        return -EBUSY;
    }

    _usb_device_start(mv_dev->mv_usb_handle);
    error = mv_usb_start_ep0(mv_dev);

    DBGMSG("registered Marvell USB gadget driver %s\n", driver->driver.name);
    return error;
}
EXPORT_SYMBOL (usb_gadget_register_driver);

int usb_gadget_unregister_driver (struct usb_gadget_driver *driver)
{
    int                 i;
    struct mv_usb_ep    *mv_ep;
    struct mv_usb_dev   *mv_dev = NULL;
    unsigned long       flags = 0;

    /* Find USB Gadget device controller */
        if( (the_controller != NULL) && (the_controller->driver == driver) )
        {
            mv_dev = the_controller;
        }
    
    if (!mv_dev)
    {
        DBGMSG("usb_gadget_unregister_driver FAILED: no such device\n");
        return -ENODEV;
    }
    if (!driver || (driver != mv_dev->driver) )
    {
        DBGMSG("usb_gadget_unregister_driver FAILED: no such driver\n");
        return -EINVAL;
    }

    /* Stop and Disable ARC USB device */
    MV_SPIN_LOCK_IRQSAVE(&mv_dev->lock, flags);

    /* Stop Endpoints */
    for (i=0; i<2*_usb_device_get_max_endpoint(mv_dev->mv_usb_handle); i++)
    {
        mv_ep = &mv_dev->ep[i];
        if(mv_ep->is_enabled == 0)
            continue;

        mv_ep->is_enabled = 0;
        mv_usb_ep_cancel_all_req(mv_ep);

        _usb_device_deinit_endpoint(mv_dev->mv_usb_handle, mv_ep->num, 
                                mv_ep->is_in ? ARC_USB_SEND : ARC_USB_RECV);
    }
    _usb_device_stop(mv_dev->mv_usb_handle);

    MV_SPIN_UNLOCK_IRQRESTORE(&mv_dev->lock, flags);

    if (mv_dev->gadget.speed != USB_SPEED_UNKNOWN)
        mv_dev->driver->disconnect (&mv_dev->gadget);

    driver->unbind (&mv_dev->gadget);

    /* free_irq */
    free_irq (mv_dev->irq, mv_dev);

    mv_dev->gadget.dev.driver = 0;
    mv_dev->driver = 0;

    mv_dev->gadget.speed = USB_SPEED_UNKNOWN;

    /* Reinit all endpoints */
    mv_usb_reinit(mv_dev);

    /*device_remove_file(dev->dev, &dev_attr_function); ?????*/
    DBGMSG("unregistered Marvell USB gadget driver %s\n", driver->driver.name);

    return 0;
}
EXPORT_SYMBOL (usb_gadget_unregister_driver);

static int  mv_usb_ep_enable(struct usb_ep *_ep, 
                              const struct usb_endpoint_descriptor *desc)
{
    struct mv_usb_dev* usb_dev;
    struct mv_usb_ep*  usb_ep;
    uint_16             maxSize;
    uint_8              epType; 
    unsigned long       flags = 0;

    usb_ep = container_of (_ep, struct mv_usb_ep, ep);
    if( (_ep == NULL) || (desc == NULL) )
        return -EINVAL;
    
    usb_dev = usb_ep->usb_dev;

    if(usb_ep->is_enabled)
    {
        DBGMSG("mv_usb: %d%s Endpoint (%s) is already in use\n", 
                    usb_ep->num, usb_ep->is_in ? "In" : "Out", usb_ep->ep.name);
        return -EINVAL;
    }
/*
    DBGMSG("USB Enable %s: type=%d, maxPktSize=%d\n",
                _ep->name, desc->bmAttributes & 0x3, desc->wMaxPacketSize);
*/
    if(usb_ep->num == 0)
    {
        DBGMSG("mv_usb: ep0 is reserved\n");
        return -EINVAL;
    }

    if(desc->bDescriptorType != USB_DT_ENDPOINT)
    {
        DBGMSG("mv_usb: wrong descriptor type %d\n", desc->bDescriptorType);
        return -EINVAL;
    }

    MV_SPIN_LOCK_IRQSAVE(&usb_dev->lock, flags);

    usb_dev = usb_ep->usb_dev;
    if( (usb_dev->driver == NULL) || 
        (usb_dev->gadget.speed == USB_SPEED_UNKNOWN) )
    {
        MV_SPIN_UNLOCK_IRQRESTORE(&usb_dev->lock, flags);
        return -ESHUTDOWN;
    }
    /* Max packet size */
    maxSize = le16_to_cpu (desc->wMaxPacketSize);

    /* Endpoint type */
    if( (desc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_CONTROL)
        epType = ARC_USB_CONTROL_ENDPOINT;
    else if( (desc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_ISOC) 
        epType = ARC_USB_ISOCHRONOUS_ENDPOINT;
    else if( (desc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_BULK) 
        epType = ARC_USB_BULK_ENDPOINT;
    else
        epType = ARC_USB_INTERRUPT_ENDPOINT;

    _ep->maxpacket = maxSize & 0x7ff;
    usb_ep->is_enabled = 1;

    _usb_device_init_endpoint(usb_dev->mv_usb_handle, usb_ep->num, maxSize, 
            usb_ep->is_in ? ARC_USB_SEND : ARC_USB_RECV, epType,
            (epType == ARC_USB_BULK_ENDPOINT) ? ARC_USB_DEVICE_DONT_ZERO_TERMINATE : 0);

    MV_SPIN_UNLOCK_IRQRESTORE(&usb_dev->lock, flags);
    return 0;
}

static int  mv_usb_ep_disable (struct usb_ep *_ep)
{
    struct mv_usb_dev*  mv_dev;
    struct mv_usb_ep*   mv_ep;
    unsigned long       flags = 0;
    uint_8              direction;

    mv_ep = container_of (_ep, struct mv_usb_ep, ep);
    if( (_ep == NULL) || (mv_ep->is_enabled == 0) || (mv_ep->num == 0))
        return -EINVAL;

    mv_dev = mv_ep->usb_dev;
/*
    DBGMSG("mv_usb_ep_disable: mv_dev=%p, ep=0x%x (%d-%s), name=%s\n", 
                mv_dev, (unsigned)_ep, mv_ep->num, mv_ep->is_in ? "in" : "out", 
                _ep->name);
*/    
    MV_SPIN_LOCK_IRQSAVE(&mv_dev->lock, flags);

    direction = mv_ep->is_in ? ARC_USB_SEND : ARC_USB_RECV;

    mv_ep->is_enabled = 0;

    /* Cancell all requests */
    mv_usb_ep_cancel_all_req(mv_ep);

    /* Disable endpoint */
    _usb_device_deinit_endpoint(mv_dev->mv_usb_handle, mv_ep->num, direction);

    MV_SPIN_UNLOCK_IRQRESTORE(&mv_dev->lock, flags);
    return 0;
}


static struct usb_request* mv_usb_ep_alloc_request(struct usb_ep *_ep, gfp_t gfp_flags)
{
    struct usb_request* req;

    if (!_ep)
        return NULL;

    req = kmalloc (sizeof *req, gfp_flags);
    if (!req)
        return NULL;

    memset (req, 0, sizeof *req);
    INIT_LIST_HEAD (&req->list);

    return req;
}

static void     mv_usb_ep_free_request(struct usb_ep *_ep, struct usb_request *_req)
{

    if (!_ep || !_req)
    {
        DBGMSG("ep_free_request Error: _ep=%p, _req=%p\n", _ep, _req);
        return;
    }
    if( !list_empty(&_req->list) )
    {
        DBGMSG("%s free_request: _req=0x%x\n",
                    _ep->name, (unsigned)_req);
        list_del_init (&_req->list);
    }
    kfree (_req);
}

static int      mv_usb_ep_queue (struct usb_ep *_ep, struct usb_request *_req, 
                                 gfp_t gfp_flags)
{
    struct mv_usb_dev* usb_dev;
    struct mv_usb_ep*  usb_ep;
    unsigned long       flags = 0;
    uint_8              error;

    usb_ep = container_of (_ep, struct mv_usb_ep, ep);
    /* check parameters */
    if( (_ep == NULL) || (_req == NULL) )
    {
        DBGMSG("ep_queue Failed: _ep=%p, _req=%p\n", _ep, _req);
        return -EINVAL;
    }
    usb_dev = usb_ep->usb_dev;

    if ( (usb_dev->driver == NULL) || (usb_dev->gadget.speed == USB_SPEED_UNKNOWN) )
        return -ESHUTDOWN;

    if(usb_ep->is_enabled == 0)
    {
        DBGMSG("ep_queue Failed - %s is disabled: usb_ep=%p\n", _ep->name, usb_ep);
        return -EINVAL;
    }

    DBGMSG("%s: num=%d-%s, name=%s, _req=%p, buf=%p, length=%d\n", 
                __FUNCTION__, usb_ep->num, usb_ep->is_in ? "in" : "out", 
                _ep->name, _req, _req->buf, _req->length);

    MV_SPIN_LOCK_IRQSAVE(&usb_dev->lock, flags);
                
    _req->status = -EINPROGRESS;
    _req->actual = 0;

    /* Add request to list */
    if( ((usb_ep->num == 0) && (_req->length == 0)) || (usb_ep->is_in) )
    {
        int     send_size, size;
        uint_8  *send_ptr, *buf_ptr;

        send_ptr = buf_ptr = _req->buf;
        send_size = size = _req->length;
        list_add_tail(&_req->list, &usb_ep->req_list);

        error = _usb_device_send_data(usb_dev->mv_usb_handle, usb_ep->num, send_ptr, send_size);
        if(error != USB_OK)
        {
            DBGMSG("ep_queue: Can't SEND data (err=%d): ep_num=%d, pBuf=0x%x, send_size=%d\n",
                    error, usb_ep->num, (unsigned)_req->buf, _req->length);
            list_del_init (&_req->list);
        }

        size -= send_size;
        buf_ptr += send_size;
    }
    else
    {
        error = _usb_device_recv_data(usb_dev->mv_usb_handle, usb_ep->num, _req->buf, _req->length);
        if(error != USB_OK)
        {
            DBGMSG("mv_usb_ep_queue: Can't RCV data (err=%d): ep_num=%d, pBuf=0x%x, size=%d\n",
                        error, usb_ep->num, (unsigned)_req->buf, _req->length);
        }
        else
            list_add_tail(&_req->list, &usb_ep->req_list);
    }

    MV_SPIN_UNLOCK_IRQRESTORE(&usb_dev->lock, flags);

    return (int)error;
}

/* Cancell request */
static int      mv_usb_ep_dequeue (struct usb_ep *_ep, struct usb_request *_req)
{
    struct mv_usb_dev* usb_dev;
    struct usb_request *usb_req;
    struct mv_usb_ep*  usb_ep;
    unsigned long       flags = 0;
    int                 status = 0;

    usb_ep = container_of (_ep, struct mv_usb_ep, ep);
    /* check parameters */
    if( (_ep == NULL) || (_req == NULL) || (usb_ep->is_enabled == 0) )
        return -EINVAL;

    usb_dev = usb_ep->usb_dev;
        
    if ( (usb_dev->driver == NULL) || (usb_dev->gadget.speed == USB_SPEED_UNKNOWN) )
    {
        DBGMSG("mv_usb_ep_dequeue: ep=0x%x, num=%d-%s, name=%s, driver=0x%x, speed=%d\n", 
                (unsigned)_ep, usb_ep->num, usb_ep->is_in ? "in" : "out", 
                _ep->name, (unsigned)usb_dev->driver, usb_dev->gadget.speed);

        return -ESHUTDOWN;
    }

    MV_SPIN_LOCK_IRQSAVE(&usb_dev->lock, flags);

    /* ????? Currently supported only dequeue request from the HEAD of List */
    if( !list_empty(&usb_ep->req_list) )
    {
        usb_req = list_entry (usb_ep->req_list.next, struct usb_request, list);

        if(usb_req == _req)
        {
            /* Cancel transfer */
            _usb_device_cancel_transfer(usb_dev->mv_usb_handle, usb_ep->num, 
                            usb_ep->is_in ? ARC_USB_SEND : ARC_USB_RECV);
            /* Dequeue request and call complete function */
            list_del_init (&_req->list);

            if (_req->status == -EINPROGRESS)
                _req->status = -ECONNRESET;

            /* ????? what about enable interrupts */
            _req->complete (&usb_ep->ep, _req);
        }
        else
        {
            DBGMSG("Cancell request failed: ep=%p, usb_req=%p, req=%p\n", 
                        _ep, usb_req, _req);
            status = EINVAL;
        }
    }
    /*
    else
        DBGMSG("%s: ep=%p, epName=%s, epNum=%d - reqList EMPTY\n", 
                    __FUNCTION__, usb_ep, usb_ep->ep.name, usb_ep->num);
    */
    MV_SPIN_UNLOCK_IRQRESTORE(&usb_dev->lock, flags);

    return status;
}

static int      mv_usb_ep_set_halt (struct usb_ep *_ep, int value)
{
    struct mv_usb_ep*   mv_ep;
    unsigned long       flags = 0;
    int                 retval = 0;

    mv_ep = container_of (_ep, struct mv_usb_ep, ep);
    if (_ep == NULL)
        return -EINVAL;
    if( (mv_ep->usb_dev->driver == NULL) || 
        (mv_ep->usb_dev->gadget.speed == USB_SPEED_UNKNOWN) )
        return -ESHUTDOWN;

/*
    DBGMSG("%s - %s \n", 
                _ep->name, value ? "Stalled" : "Unstalled");
*/
    MV_SPIN_LOCK_IRQSAVE(&mv_ep->usb_dev->lock, flags);
    if (!list_empty (&mv_ep->req_list))
        retval = -EAGAIN;
    else 
    {
        /* set/clear, then synch memory views with the device */
        if (value) 
        {
            if (mv_ep->num == 0)
                mv_ep->usb_dev->protocol_stall = 1;
            else
                _usb_device_stall_endpoint(mv_ep->usb_dev->mv_usb_handle, mv_ep->num,
                mv_ep->is_in ? ARC_USB_SEND : ARC_USB_RECV);
        } 
        else
        {
            _usb_device_unstall_endpoint(mv_ep->usb_dev->mv_usb_handle, mv_ep->num, 
                                    mv_ep->is_in ? ARC_USB_SEND : ARC_USB_RECV);
        }
    }
    MV_SPIN_UNLOCK_IRQRESTORE(&mv_ep->usb_dev->lock, flags);

    return retval;
}


static void     mv_usb_ep_fifo_flush (struct usb_ep *_ep)
{
    DBGMSG("%s: ep=%p, ep_name=%s - NOT supported\n", __FUNCTION__, _ep, _ep->name);
}


static struct usb_ep_ops mv_usb_ep_ops = 
{
    .enable         = mv_usb_ep_enable,
    .disable        = mv_usb_ep_disable,

    .alloc_request  = mv_usb_ep_alloc_request,
    .free_request   = mv_usb_ep_free_request,

    .queue          = mv_usb_ep_queue,
    .dequeue        = mv_usb_ep_dequeue,

    .set_halt       = mv_usb_ep_set_halt,
    .fifo_flush     = mv_usb_ep_fifo_flush,
    /*.fifo_status    =  Not supported */
};

static int mv_usb_get_frame (struct usb_gadget *_gadget)
{
    DBGMSG("Call mv_usb_get_frame - NOT supported\n");
    return 0;
}

static int mv_usb_wakeup(struct usb_gadget *_gadget)
{
    DBGMSG("Call mv_usb_wakeup - NOT supported\n");
    return 0;
}

static int mv_usb_set_selfpowered (struct usb_gadget *_gadget, int value)
{
    DBGMSG("Call mv_usb_set_selfpowered - NOT supported\n");
    return 0;
}

static const struct usb_gadget_ops mv_usb_ops = 
{
    .get_frame       = mv_usb_get_frame,
    .wakeup          = mv_usb_wakeup,
    .set_selfpowered = mv_usb_set_selfpowered,
    .ioctl           = NULL,
};

static void mv_usb_gadget_release (struct device *_dev)
{
    struct mv_usb_dev   *usb_dev = dev_get_drvdata (_dev);

    /*DBGMSG("Call mv_usb_gadget_release \n");*/
    kfree(usb_dev);
}

static int __init mv_usb_gadget_probe(struct platform_device *pdev) 
{
    struct mv_usb_dev       *mv_dev;
    int                     retval, i;
    uint_8                  error;
    struct resource *res;
    void __iomem *regs;
    int irq, err;

    DBGMSG("USB Gadget driver probed\n");

	irq = platform_get_irq(pdev, 0);
	if (irq <= 0) {
		DBGMSG("%s: Found UDC with no IRQ. Check setup!\n", driver_name);
		err = -ENODEV;
		goto err1;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		DBGMSG("%s: Found UDC with no register addr. Check setup!\n", driver_name);
		err = -ENODEV;
		goto err1;
	}

	if (!request_mem_region(res->start, res->end - res->start + 1, driver_name)) {
		DBGMSG("%s: controller already in use\n", driver_name);
		err = -EBUSY;
		goto err1;
	}

	regs = ioremap(res->start, res->end - res->start + 1);
	if (regs == NULL) {
		DBGMSG("%s: error mapping memory\n", driver_name);
		err = -EFAULT;
		goto err2;
	}

    if (the_controller) 
    {
        DBGMSG("mv_dev_load: USB controller is BUSY\n");
        err = -EBUSY;
        goto err3;
    }

    /* alloc, and start init */
    mv_dev = kmalloc(sizeof(struct mv_usb_dev), GFP_ATOMIC);
    if (mv_dev == NULL)
    {
        DBGMSG("mv_dev_load: malloc failed\n");
        err = -ENOMEM;
        goto err3;
    }

    memset (mv_dev, 0, sizeof *mv_dev);
    spin_lock_init (&mv_dev->lock);
    mv_dev->dev = &pdev->dev; 
    mv_dev->gadget.ops = &mv_usb_ops;
    mv_dev->gadget.is_dualspeed = 1;

    /* the "gadget" abstracts/virtualizes the controller */
	dev_set_name(&mv_dev->gadget.dev, "gadget");
    mv_dev->gadget.dev.parent = &pdev->dev;
    mv_dev->gadget.dev.dma_mask = pdev->dev.dma_mask; /* ?????? */
    mv_dev->gadget.dev.release = mv_usb_gadget_release ;
    mv_dev->gadget.name = driver_name;
    mv_dev->mv_usb_handle = NULL;
    
    mv_dev->rsrc_start = res->start;
    mv_dev->rsrc_len = res->end - res->start + 1;
    mv_dev->regs = regs;
    mv_dev->irq = irq;

	platform_set_drvdata(pdev, mv_dev);
    the_controller = mv_dev;
    
    mv_dev->pd = pdev->dev.platform_data;

    if(mv_dev->pd->gpio_usb_vbus)
    {
    	if((retval = gpio_request(mv_dev->pd->gpio_usb_vbus, "GPIO USB VBUS")))
    	{
    		DBGMSG("%s can't get vbus gpio %d, err: %d\n", driver_name, mv_dev->pd->gpio_usb_vbus, retval);
    		err = -EINVAL;
    		goto err3;
    	}
    	gpio_direction_input(mv_dev->pd->gpio_usb_vbus);
    	mv_udc_write(USB_PHY_PWR_CTRL, mv_udc_read(USB_PHY_PWR_CTRL) & ~(7 << 24));

    	mv_dev->vbus_gpp_no = mv_dev->pd->gpio_usb_vbus;
    }
    else
    	mv_dev->vbus_gpp_no = 0;
	
    DBGMSG("USB gadget device: vbus_gpp_no = %d\n", mv_dev->vbus_gpp_no);
    	
    if(mv_dev->pd->gpio_usb_vbus_en)
    {
    	if((retval = gpio_request(mv_dev->pd->gpio_usb_vbus_en, "GPIO USB VBUS ENABLE")))
    	{
    		DBGMSG("%s can't get vbus enable gpio %d, err: %d\n", driver_name, mv_dev->pd->gpio_usb_vbus_en, retval);
    		err = -EINVAL;
    		goto err4;
    	}
    	gpio_direction_output(mv_dev->pd->gpio_usb_vbus_en, 0);
    }
    
    if(mv_dev->vbus_gpp_no)
    {
         if (request_irq (gpio_to_irq(mv_dev->vbus_gpp_no), mv_usb_vbus_irq, IRQF_DISABLED, 
                     driver_name, mv_dev) != 0) 
        {
            DBGMSG("%s probe: request interrupt %d failed\n", 
                    driver_name, gpio_to_irq(mv_dev->vbus_gpp_no));
            err = -EBUSY;
            goto err5;
        }
		}

		if(mv_dev->pd != NULL && mv_dev->pd->dram != NULL)
			mv_udc_conf_mbus_windows(mv_dev->pd->dram);

		mv_udc_setup(1);


    /* Reset ARC USB device ????? */
    /* Reinit ARC USB device ????? */

    /* First of all. */
    _usb_device_set_bsp_funcs(&usbImportFuncs);
    
    /* Enable ARC USB device */
    retval = (int)_usb_device_init(0, &mv_dev->mv_usb_handle);
    if (retval != USB_OK) 
    {
        DBGMSG("\nUSB Initialization failed. Error: %x", retval);
        err = -EINVAL;
        goto err6;
    } /* Endif */

    /* Self Power, Remote wakeup disable */
    _usb_device_set_status(mv_dev->mv_usb_handle, ARC_USB_STATUS_DEVICE, (1 << DEVICE_SELF_POWERED));

    /* Register all ARC Services */  
    error = _usb_device_register_service(mv_dev->mv_usb_handle, 
                                ARC_USB_SERVICE_BUS_RESET, mv_usb_bus_reset_service);
    if (error != USB_OK) 
    {
        DBGMSG("\nUSB BUS_RESET Service Registration failed. Error: 0x%x", error);
        err = -EINVAL;
        goto err6;
    } /* Endif */
   
    error = _usb_device_register_service(mv_dev->mv_usb_handle, 
                        ARC_USB_SERVICE_SPEED_DETECTION, mv_usb_speed_service);
    if (error != USB_OK) 
    {
        DBGMSG("\nUSB SPEED_DETECTION Service Registration failed. Error: 0x%x", 
                        error);
        err = -EINVAL;
        goto err6;
    } /* Endif */
         
    error = _usb_device_register_service(mv_dev->mv_usb_handle, 
                                ARC_USB_SERVICE_SUSPEND, mv_usb_suspend_service);
    if (error != USB_OK) 
    {
        DBGMSG("\nUSB SUSPEND Service Registration failed. Error: 0x%x", error);
        err = -EINVAL;
        goto err6;
    } /* Endif */

    error = _usb_device_register_service(mv_dev->mv_usb_handle, 
                                ARC_USB_SERVICE_SLEEP, mv_usb_suspend_service);
    if (error != USB_OK) 
    {
        DBGMSG("\nUSB SUSPEND Service Registration failed. Error: 0x%x", error);
        err = -EINVAL;
        goto err6;
    } /* Endif */    

    error = _usb_device_register_service(mv_dev->mv_usb_handle, 
                                ARC_USB_SERVICE_RESUME, mv_usb_resume_service);
    if (error != USB_OK) 
    {
        DBGMSG("\nUSB RESUME Service Registration failed. Error: 0x%x", error);
        err = -EINVAL;
        goto err6;
    } /* Endif */

    error = _usb_device_register_service(mv_dev->mv_usb_handle, 0, 
                                            mv_usb_ep0_complete_service);   
    if (error != USB_OK) 
    {
        DBGMSG("\nUSB ep0 TR_COMPLETE Service Registration failed. Error: 0x%x", error);
        err = error;
        goto err6;
    } /* Endif */

    for (i=1; i<_usb_device_get_max_endpoint(mv_dev->mv_usb_handle); i++)
    {
        error = _usb_device_register_service(mv_dev->mv_usb_handle, i, 
                                                    mv_usb_tr_complete_service);   
        if (error != USB_OK) 
        {
            DBGMSG("\nUSB ep0 TR_COMPLETE Service Registration failed. Error: 0x%x", error);
            err = error;
            goto err6;
        } /* Endif */
    }
    mv_dev->gadget.speed = USB_SPEED_UNKNOWN;

    if( mv_usb_reinit (mv_dev) != USB_OK)
    {
        err = -EINVAL;
        goto err6;
    }

    err = device_register (&mv_dev->gadget.dev);
    if(err)
    	goto err6;
    	
    return 0;

err6:
	free_irq(gpio_to_irq(mv_dev->vbus_gpp_no), mv_dev);
err5:
	if(mv_dev->pd->gpio_usb_vbus_en)
		gpio_free(mv_dev->pd->gpio_usb_vbus_en);
err4:
	if(mv_dev->pd->gpio_usb_vbus)
		gpio_free(mv_dev->pd->gpio_usb_vbus);
err3:
	iounmap(regs);
err2:
	release_mem_region(res->start, res->end - res->start + 1);
err1:

    return err; 
}

static int __exit mv_usb_gadget_remove(struct platform_device *pdev)
{
    int                 i;
    struct mv_usb_dev   *mv_dev = platform_get_drvdata(pdev); 

    DBGMSG("mv_usb_gadget_remove: mv_dev=%p, driver=%p\n", 
                mv_dev, mv_dev->driver);

    /* start with the driver above us */
    if (mv_dev->driver) 
    {
        /* should have been done already by driver model core */
        DBGMSG("pci remove, driver '%s' is still registered\n",
                    mv_dev->driver->driver.name);
        usb_gadget_unregister_driver (mv_dev->driver);
    }

    spin_lock (&mv_dev->lock);

    for (i=0; i<_usb_device_get_max_endpoint(mv_dev->mv_usb_handle); i++)
        _usb_device_unregister_service(mv_dev->mv_usb_handle, i);   

    /* Deregister all other services */
    _usb_device_unregister_service(mv_dev->mv_usb_handle, ARC_USB_SERVICE_BUS_RESET);   
    _usb_device_unregister_service(mv_dev->mv_usb_handle, ARC_USB_SERVICE_SPEED_DETECTION);

    _usb_device_unregister_service(mv_dev->mv_usb_handle, ARC_USB_SERVICE_SUSPEND);

    _usb_device_unregister_service(mv_dev->mv_usb_handle, ARC_USB_SERVICE_RESUME);

    _usb_device_shutdown(mv_dev->mv_usb_handle);

    spin_unlock (&mv_dev->lock);
    
    mv_udc_setup(0);
    
    if(mv_dev->vbus_gpp_no)
    {
        free_irq (gpio_to_irq(mv_dev->vbus_gpp_no), mv_dev); 
        gpio_free(mv_dev->vbus_gpp_no);
    }
    if(mv_dev->pd->gpio_usb_vbus_en)
    {
    	gpio_direction_output(mv_dev->pd->gpio_usb_vbus_en, 1);
    	gpio_free(mv_dev->pd->gpio_usb_vbus_en);
    }

	iounmap(mv_dev->regs);
	release_mem_region(mv_dev->rsrc_start, mv_dev->rsrc_len);

    the_controller = 0;
    device_unregister (&mv_dev->gadget.dev);

    kfree(mv_dev);

    platform_set_drvdata(pdev, 0);
    return 0;
}
 
static struct platform_driver mv_usb_gadget_driver = 
{
	.driver = {
		.name       = (char *) driver_name,
		.owner = THIS_MODULE,
	},
	.probe      = mv_usb_gadget_probe,
	.remove     = __exit_p(mv_usb_gadget_remove),
};

MODULE_VERSION (DRIVER_VERSION);
MODULE_DESCRIPTION (DRIVER_DESC);
MODULE_AUTHOR ("Dima Epshtein");
MODULE_LICENSE ("GPL");

static int __init init (void)
{
	DBGMSG("%s: version %s loaded\n", driver_name, DRIVER_VERSION);
	
	the_controller = NULL;
	
	return platform_driver_register(&mv_usb_gadget_driver); 
}
module_init (init);

static void __exit cleanup (void)
{
    DBGMSG("%s: version %s unloaded\n", driver_name, DRIVER_VERSION);
    
    platform_driver_unregister(&mv_usb_gadget_driver);
}
module_exit (cleanup);

