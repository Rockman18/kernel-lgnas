/*
 * LG NAS NT3 platform support
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; version 2 of the License.
 */
#include <asm/machdep.h>

#include <linux/i2c.h>
#include <linux/of_i2c.h>
#include <linux/gpio.h>

#define mtdcr(rn, val) \
	asm volatile("mtdcr %0,%1" : : "i"(rn), "r"(val))

#define DCRN_SDR0_CONFIG_ADDR	0xe
#define DCRN_SDR0_CONFIG_DATA	0xf

#define SDR0_WRITE(offset, data) ({\
	mtdcr(DCRN_SDR0_CONFIG_ADDR, offset); \
	mtdcr(DCRN_SDR0_CONFIG_DATA, data); })

#define DCRN_SDR0_PFC0	0x4100

static void __gpio_output_enable(void)
{
	SDR0_WRITE(DCRN_SDR0_PFC0, 0xffffffff);
}

static int __i2c_write(struct i2c_client *client, u8 *buf, u8 len)
{
	struct i2c_msg msgs[] = {
		{.addr = client->addr, .flags = 0, .buf = buf, .len = len},
	};
	
	if(i2c_transfer(client->adapter, msgs, 1) != 1)
		return false;
	return true;
}

static int __detach_flash(void)
{
	struct device_node *np = of_find_compatible_node(NULL, NULL, "io-micom");
	struct i2c_client *client = of_find_i2c_device_by_node(np);
	char buf[8] = {0x02, 0x97, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

	__gpio_output_enable();
	return __i2c_write(client, buf, 8);
}

static void lgnas_flash_init(void)
{
	if(!__detach_flash())
		printk("%s: failed\n", __func__);
}

static void lgnas_power_off(void)
{
	gpio_set_value(15, 0);
}

static void __init lgnas_power_off_init(void)
{
	ppc_md.power_off = lgnas_power_off;
	ppc_md.halt = lgnas_power_off;
}

static int __init lgnas_init(void)
{
	lgnas_flash_init();
	lgnas_power_off_init();
	return 0;
}
machine_late_initcall(ppc44x_simple, lgnas_init);
