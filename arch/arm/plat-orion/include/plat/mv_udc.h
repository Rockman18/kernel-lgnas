#ifndef __PLAT_MV_UDC_H
#define __PLAT_MV_UDC_H

#include <linux/mbus.h>

struct mv_udc_platform_data {
	struct mbus_dram_target_info *dram;
	unsigned char gpio_usb_vbus_en;
	unsigned char gpio_usb_vbus;
};

#endif
