/*
 * arch/arm/mach-kirkwood/nc2-setup.c
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
#include <linux/spi/flash.h>
#include <linux/spi/spi.h>
#include <linux/spi/orion_spi.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/delay.h>
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

static struct i2c_board_info lgnas_i2c_board_info[] =
{
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

static void __set_blink(unsigned pin, int blink)
{
	u32 u = readl(GPIO_BLINK_EN(pin));
	if(blink)
		u |= 1 << (pin & 31);
	else
		u &= ~(1 << (pin & 31));
	writel(u, GPIO_BLINK_EN(pin));
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

extern void (*gpio_set_blink)(unsigned pin, int blink);

static void __init lgnas_gpio_blink_init(void)
{
	gpio_set_blink = __set_blink;
}

#define SMI_REG	(KIRKWOOD_REGS_VIRT_BASE | 0x72004)
static int smi_is_done(void)
{
	return !(readl(SMI_REG) & 0x10000000);
}
static void smi_wait_ready(void)
{
	int i;
	for(i = 0; !smi_is_done(); i++)
	{
		if(i == 10)
			return;
		mdelay(10);
	}
}
static void smi_write(int reg, u16 val)
{
	smi_wait_ready();
	writel((reg << 21) | (8 << 16) | (val & 0xffff), SMI_REG);
	smi_wait_ready();
}

static void __link_led_brightness_set(unsigned value)
{
	smi_write(0x16, 0x0003);
	if(value)
		smi_write(0x10, 0x1019);
	else
		smi_write(0x10, 0x1018);
	smi_write(0x16, 0x0000);
}

extern void (*link_led_brightness_set)(unsigned value);

static void lgnas_link_led_init(void)
{
	link_led_brightness_set = __link_led_brightness_set;
}

static int __hpo;

static int __hpo_proc_read(char *page, char **start, off_t off,
	int count, int *eof, void *data)
{
	return sprintf(page, "%s\n", __hpo? "off" : "on");
}

static int __hpo_proc_write(struct file *file, const char *buffer,
	unsigned long count, void *data)
{
	if(!strncmp(buffer, "on", 2))
		__hpo = 0;
	if(!strncmp(buffer, "off", 3))
		__hpo = 1;
	return count;
}

static void __init __hpo_proc_start(void)
{
	struct proc_dir_entry *hpo = create_proc_entry("hpo", 0644, NULL);
	if(hpo) {
		hpo->read_proc = __hpo_proc_read;
		hpo->write_proc = __hpo_proc_write;
	}
}

static void __init lgnas_proc_init(void)
{
	__start_resource_dump();
	__hpo_proc_start();
}

extern int enter_cpuidle;

static void __enable_sata_pm(unsigned int port_no)
{
	void __iomem *port_mmio = (void __iomem *)
		(KIRKWOOD_REGS_VIRT_BASE | (0x82000 + 0x2000 * port_no));
	u32 scontrol, m2, ifcfg;

	ifcfg = readl(port_mmio + 0x50);
	ifcfg &= ~(1 << 9);
	writel(ifcfg, port_mmio + 0x50);

	m2 = readl(port_mmio + 0x330);
	m2 |= 0xf;
	writel(m2, port_mmio + 0x330);
	
	udelay(200);

	scontrol = readl(port_mmio + 0x308);
	scontrol = (scontrol & ~0xff00) | 0x3300;
	writel(scontrol, port_mmio + 0x308);
}

static void __disable_sata_pm(unsigned int port_no)
{
	void __iomem *port_mmio = (void __iomem *)
		(KIRKWOOD_REGS_VIRT_BASE | (0x82000 + 0x2000 * port_no));
	u32 scontrol, m2, ifcfg;

	scontrol = readl(port_mmio + 0x308);
	scontrol = (scontrol & ~0xff00) | 0x2000;
	writel(scontrol, port_mmio + 0x308);
	
	m2 = readl(port_mmio + 0x330);
	m2 &= ~0xf;
	writel(m2, port_mmio + 0x330);
	
	udelay(200);
	
	ifcfg = readl(port_mmio + 0x50);
	ifcfg |= (1 << 9);
	writel(ifcfg, port_mmio + 0x50);
}

void lgnas_hdpower_set(unsigned int port_no,
	int pmp, unsigned char cmnd, int force, int power)
{
	static u32 save_reg[2];
	static unsigned char scsi_cmnd[2];
	unsigned int deadline = 60;
	int synchronous;
	u8 stat[2];
	int level[2];

	level[0] = gpio_get_value(15);
	level[1] = gpio_get_value(16);
	
	if(__hpo && level[0] && level[1])
		return;
	
	if(power)
	{
		if(!level[0] || !level[1])
		{
			pr_info("%s: [port(%d) pmp(%d)] triggering hdd power on...\n",
				__func__, port_no, pmp);
			
			enter_cpuidle = 0;
			
			synchronous = ((scsi_cmnd[0] != 0x1b) && (scsi_cmnd[1] != 0x1b) &&
				!level[0] && !level[1]) | force;

			if(synchronous)
			{
				__enable_sata_pm(0);
				gpio_set_value(15, 1);
				__enable_sata_pm(1);
				gpio_set_value(16, 1);
				
				/* edma interrupt error mask register */
				writel(save_reg[0], KIRKWOOD_REGS_VIRT_BASE | 0x8200c);
				writel(save_reg[1], KIRKWOOD_REGS_VIRT_BASE | 0x8400c);
				
				do
				{
					mdelay(1000);
					/* device status register */
					stat[0] = ioread8(KIRKWOOD_REGS_VIRT_BASE | 0x8211c);
					stat[1] = ioread8(KIRKWOOD_REGS_VIRT_BASE | 0x8411c);
					deadline--;
				} while(((stat[0] != 0xff) && (stat[1] != 0xff)) &&
								((stat[0] & (1<<7)) || (stat[1] & (1<<7))) &&
								(deadline > 0));
				
				/* serror register */
				save_reg[0] = readl(KIRKWOOD_REGS_VIRT_BASE | 0x82304);
				writel(save_reg[0], KIRKWOOD_REGS_VIRT_BASE | 0x82304);
				/* edma interrupt error cause register */
				writel(0x0, KIRKWOOD_REGS_VIRT_BASE | 0x82008);
				writel(~((1<<13) | (1<<14) | (1<<16) | (0x1f<<21)),
					KIRKWOOD_REGS_VIRT_BASE | 0x8200c);

				save_reg[1] = readl(KIRKWOOD_REGS_VIRT_BASE | 0x84304);
				writel(save_reg[1], KIRKWOOD_REGS_VIRT_BASE | 0x84304);
				writel(0x0, KIRKWOOD_REGS_VIRT_BASE | 0x84008);
				writel(~((1<<13) | (1<<14) | (1<<16) | (0x1f<<21)),
					KIRKWOOD_REGS_VIRT_BASE | 0x8400c);
			}
			else if(!level[port_no])
			{
				void __iomem *port_mmio = (void __iomem *)
					(KIRKWOOD_REGS_VIRT_BASE | (0x82000 + 0x2000 * port_no));
				unsigned pin = 15 + port_no;
				u8 stat;

				__enable_sata_pm(port_no);
				gpio_set_value(pin, 1);
				
				writel(save_reg[port_no], port_mmio + 0xc);
				
				save_reg[port_no] = readl(port_mmio + 0x344);
				
				do
				{
					mdelay(1000);
					writel((save_reg[port_no] & ~0xf) | pmp, port_mmio + 0x344);
					stat = ioread8(port_mmio + 0x11c);
					deadline--;
				} while(stat != 0xff && (stat & (1<<7)) && (deadline > 0));
				
				writel(save_reg[port_no], port_mmio + 0x344);
				
				save_reg[port_no] = readl(port_mmio + 0x304);
				writel(save_reg[port_no], port_mmio + 0x304);
				writel(0x0, port_mmio + 0x8);
				writel(~((1<<13) | (1<<14) | (1<<16) | (0x1f<<21)), port_mmio + 0xc);
			}
		}
	}
	else
	{
		if(level[0] || level[1])
		{
			pr_info("%s: [port(%d) pmp(%d)] triggering hdd power off...\n",
				__func__, port_no, pmp);

			switch(port_no)
			{
			case 0:
				save_reg[0] = readl(KIRKWOOD_REGS_VIRT_BASE | 0x8200c);
				writel(0x0, KIRKWOOD_REGS_VIRT_BASE | 0x8200c);
				__disable_sata_pm(0);
				gpio_set_value(15, 0);
				break;
			case 1:
				save_reg[1] = readl(KIRKWOOD_REGS_VIRT_BASE | 0x8400c);
				writel(0x0, KIRKWOOD_REGS_VIRT_BASE | 0x8400c);
				__disable_sata_pm(1);
				gpio_set_value(16, 0);
				break;
			}
			scsi_cmnd[port_no] = cmnd;
			enter_cpuidle = 1;
		}
	}
}

extern void (*ata_power_set)(unsigned int port_no,
	int pmp, unsigned char cmnd, int force, int power);
static void __init lgnas_power_func_init(void)
{
	ata_power_set = lgnas_hdpower_set;
}

static struct mtd_partition lgnas_partitions[] = {
	{
		.name = "u-boot",
		.offset = 0,
		.size = MTDPART_SIZ_FULL
	},
};

static const struct flash_platform_data lgnas_flash =
{
	.type	= "mx25l4005a",
	.name	= "spi_flash",
	.parts = lgnas_partitions,
	.nr_parts	= ARRAY_SIZE(lgnas_partitions),
};

static struct spi_board_info __initdata lgnas_spi_slave_info[] =
{
	{
		.modalias	= "m25p80",
		.platform_data = &lgnas_flash,
		.irq = -1,
		.max_speed_hz	= 20000000,
		.bus_num = 0,
		.chip_select = 0,
	},
};

static void __init lgnas_spi_init(void)
{
	spi_register_board_info(lgnas_spi_slave_info,
		ARRAY_SIZE(lgnas_spi_slave_info));
	kirkwood_spi_init();
}

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
	MPP0_SPI_SCn,
	MPP1_SPI_MOSI,
	MPP2_SPI_SCK,
	MPP3_SPI_MISO,
	MPP15_GPIO,
	MPP16_GPIO,
	MPP17_GPIO,
	MPP28_GPIO,
	MPP29_GPIO,
	MPP30_GPIO,
	MPP31_GPIO,
	MPP32_GPIO,
	MPP34_GPIO,
	MPP35_GPIO,
	MPP47_GPIO,
	0
};

static void __init lgnas_init(void)
{
	kirkwood_init();
	kirkwood_mpp_conf(lgnas_mpp_config);

	kirkwood_uart0_init();
	lgnas_spi_init();
	lgnas_i2c_init();
	kirkwood_ehci_init();
	kirkwood_ge00_init(&lgnas_ge00_data);
	kirkwood_sata_init(&lgnas_sata_data);

	lgnas_proc_init();
	lgnas_power_func_init();
	lgnas_power_off_init();
	lgnas_gpio_blink_init();
	lgnas_link_led_init();
}

#define LGAMS_MACHINE_NAME	"LG Advanced Multimedia Storage NC2"

MACHINE_START(LGAMS, LGAMS_MACHINE_NAME)
	.phys_io = KIRKWOOD_REGS_PHYS_BASE,
	.io_pg_offst = ((KIRKWOOD_REGS_VIRT_BASE) >> 18) & 0xfffc,
	.boot_params = 0x00000100,
	.init_machine	= lgnas_init,
	.map_io = kirkwood_map_io,
	.init_irq	= kirkwood_init_irq,
	.timer = &kirkwood_timer,
MACHINE_END
