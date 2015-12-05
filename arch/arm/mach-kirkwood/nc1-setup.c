/*
 * arch/arm/mach-kirkwood/nc1-setup.c
 *
 * LG AMS Setup
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/proc_fs.h>
#include <linux/mtd/partitions.h>
#include <linux/ata_platform.h>
#include <linux/mv643xx_eth.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <asm/setup.h>
#include <asm/io.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <mach/kirkwood.h>
#include "common.h"
#include "mpp.h"

static struct proc_dir_entry *resource_dump;
static u32 resource_dump_request, resource_dump_result;

static int __ishex(char ch)
{
	if(((ch>='0') && (ch <='9')) ||
		((ch>='a') && (ch <='f')) ||
		((ch >='A') && (ch <='F')))
		return 1;
	return 0;
}

static int __hex_value(char ch)
{
	if((ch >= '0') && (ch <= '9')) return ch-'0';
	if((ch >= 'a') && (ch <= 'f')) return ch-'a'+10;
	if((ch >= 'A') && (ch <= 'F')) return ch-'A'+10;
	return 0;
}

static int __atoh(char *s, int len)
{
	int i=0;
	while(__ishex(*s) && len--) {
		i = i*0x10 + __hex_value(*s);
		s++;
	}
	return i;
}

static int __resource_dump_write(struct file *file, const char *buffer,
	unsigned long count, void *data)
{
	/* Reading / Writing from system controller internal registers */
	if(!strncmp(buffer, "register", 8)) {
		if(buffer[10] == 'r') {
			resource_dump_request = __atoh((char *)((unsigned int)buffer + 12), 8);
			resource_dump_result = readl(KIRKWOOD_REGS_VIRT_BASE | resource_dump_request);
		}
		if(buffer[10] == 'w') {
			resource_dump_request = __atoh((char *)((unsigned int)buffer + 12), 8);
			resource_dump_result = __atoh((char *)((unsigned int)buffer + 12 + 8 + 1), 8);
			writel(resource_dump_result, KIRKWOOD_REGS_VIRT_BASE | resource_dump_request);
		}
	}
	/* Reading / Writing from 32bit address - mostly usable for memory */
	if(!strncmp(buffer, "memory  ", 8)) {
		if(buffer[10] == 'r') {
			resource_dump_request = __atoh((char *)((unsigned int)buffer + 12), 8);
			resource_dump_result = *(unsigned int *)resource_dump_request;
		}
		if(buffer[10] == 'w') {
			resource_dump_request = __atoh((char *)((unsigned int)buffer + 12), 8);
			resource_dump_result = __atoh((char *)((unsigned int)buffer + 12 + 8 + 1), 8);
			*(unsigned int *)resource_dump_request = resource_dump_result;
		}
	}
	return count;
}

static int __resource_dump_read(char *buffer, char **buffer_location, off_t offset,
	int buffer_length, int *zero, void *ptr)
{
	if(offset > 0)
		return 0;
	return sprintf(buffer, "%08x\n", resource_dump_result);
}

static int __init __start_resource_dump(void)
{
	resource_dump = create_proc_entry("resource_dump", 0666, NULL);
	resource_dump->read_proc = __resource_dump_read;
	resource_dump->write_proc = __resource_dump_write;
	resource_dump->nlink = 1;
	return 0;
}

static void __init lgnas_proc_init(void)
{
	__start_resource_dump();
}

static struct i2c_board_info lgnas_i2c_board_info[] =
{
	{
		I2C_BOARD_INFO("rtc-rs5c372", 0x32),
		.type = "r2025sd",
	},
	{
		I2C_BOARD_INFO("iomicom", 0x58),
		.type = "io-micom",
	},
};

static void __init lgnas_i2c_init(void)
{
	kirkwood_i2c_init();
	i2c_register_board_info(0, lgnas_i2c_board_info,
		ARRAY_SIZE(lgnas_i2c_board_info));
}

static void __set_direction(unsigned pin, int input)
{
	u32 u;

	u = readl(GPIO_IO_CONF(pin));
	if (input)
		u |= 1 << (pin & 31);
	else
		u &= ~(1 << (pin & 31));
	writel(u, GPIO_IO_CONF(pin));
}

static void __set_level(unsigned pin, int high)
{
	u32 u;

	u = readl(GPIO_OUT(pin));
	if (high)
		u |= 1 << (pin & 31);
	else
		u &= ~(1 << (pin & 31));
	writel(u, GPIO_OUT(pin));
}

void lgnas_power_off(void)
{
	__set_level(7, 0);
	__set_direction(7, 0);
}

static void __init lgnas_power_off_init(void)
{
	pm_power_off = lgnas_power_off;
}

void lgnas_hdpower_set(unsigned int port_no,
	int pmp, unsigned char cmnd, int force, int power)
{
}

static struct mtd_partition lgnas_nand_parts[] =
{
	{
		.name = "u-boot",
		.offset = 0,
		.size = 0x100000
	},
	{
		.name = "kernel",
		.offset = MTDPART_OFS_NXTBLK,
		.size = 0x400000
	},
	{
		.name = "rootfs",
		.offset = MTDPART_OFS_NXTBLK,
		.size = 0xb00000
	},
	{
		.name = "data",
		.offset = MTDPART_OFS_NXTBLK,
		.size = MTDPART_SIZ_FULL
	},
};

static struct mv643xx_eth_platform_data lgnas_ge00_data =
{
	.phy_addr	= MV643XX_ETH_PHY_ADDR(8),
};

static struct mv_sata_platform_data lgnas_sata_data =
{
	.n_ports = 2,
};

static unsigned int lgnas_mpp_config[] __initdata =
{
	MPP0_NF_IO2,
	MPP1_NF_IO3,
	MPP2_NF_IO4,
	MPP3_NF_IO5,
	MPP4_NF_IO6,
	MPP5_NF_IO7,
	MPP15_GPIO,
	MPP16_GPIO,
	MPP17_GPIO,
	MPP18_NF_IO0,
	MPP19_NF_IO1,
	MPP20_GPIO,
	MPP22_GPIO,
	MPP28_GPIO,
	MPP29_GPIO,
	MPP30_GPIO,
	MPP31_GPIO,
	MPP32_GPIO,
	MPP33_GPO,
	MPP34_GPIO,
	MPP35_GPIO,
	0
};

static void __init lgnas_init(void)
{
	kirkwood_init();
	kirkwood_mpp_conf(lgnas_mpp_config);

	kirkwood_uart0_init();
	kirkwood_nand_init(ARRAY_AND_SIZE(lgnas_nand_parts), 25);
	lgnas_i2c_init();
	kirkwood_ehci_init();
	kirkwood_ge00_init(&lgnas_ge00_data);
	kirkwood_sata_init(&lgnas_sata_data);

	lgnas_proc_init();
	lgnas_power_off_init();
}

#define LGAMS_MACHINE_NAME	"LG Advanced Multimedia Storage NC1"

MACHINE_START(LGAMS, LGAMS_MACHINE_NAME)
	.phys_io = KIRKWOOD_REGS_PHYS_BASE,
	.io_pg_offst = ((KIRKWOOD_REGS_VIRT_BASE) >> 18) & 0xfffc,
	.boot_params = 0x00000100,
	.init_machine	= lgnas_init,
	.map_io = kirkwood_map_io,
	.init_irq	= kirkwood_init_irq,
	.timer = &kirkwood_timer,
MACHINE_END
