/*
 * pm.c
 *
 * Power Management functions for Marvell Kirkwood System On Chip
 *
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/suspend.h>
#include <linux/io.h>
#include <mach/kirkwood.h>
#include <mach/bridge-regs.h>

#if defined(CONFIG_MACH_NT1) || defined(CONFIG_MACH_NT11)
#define N_PORTS	1
#define N_PMPS	1
#elif defined(CONFIG_MACH_NC2)
#define N_PORTS	2
#define N_PMPS	2
#elif defined(CONFIG_MACH_NC21)
#define N_PORTS	2
#define N_PMPS	1
#else
#define N_PORTS	1
#define N_PMPS	0
#endif

extern void (*ata_power_set)(unsigned int port_no,
	int pmp, unsigned char cmnd, int force, int power);
static void __restore_hdpower(void)
{
	int port, pmp;
	
	for(port = 0; port < N_PORTS; port++)
		for(pmp = 0; pmp < N_PMPS; pmp++)
			if(ata_power_set)
				ata_power_set(port, pmp, 0xff, 1, 1);
}

static void __enable_clock(void)
{
#ifdef CONFIG_CRYPTO_DEV_MV_CESA
	writel(0x00c7c1c9, BRIDGE_VIRT_BASE | 0x11c);
#else
	writel(0x00c5c1c9, BRIDGE_VIRT_BASE | 0x11c);
#endif
}

static void __disable_clock(void)
{
	writel(0x00c40081, BRIDGE_VIRT_BASE | 0x11c);
}

static void __enable_mem(void)
{
#ifdef CONFIG_CRYPTO_DEV_MV_CESA
	writel(0x00002202, BRIDGE_VIRT_BASE | 0x118);
#else
	writel(0x00002302, BRIDGE_VIRT_BASE | 0x118);
#endif
}

static void __disable_mem(void)
{
	writel(0x00002bf7, BRIDGE_VIRT_BASE | 0x118);
}

void kirkwood_idle(void)
{
	printk("%s: entering idle mode...\n", __func__);
	
	/* set the DRAM to Self Refresh mode */
	writel(0x7, DDR_OPERATION_BASE);
	cpu_do_idle();

	__restore_hdpower();

	printk("%s: exiting idle mode...\n", __func__);
}

void kirkwood_deepidle(void)
{
	printk("%s: entering deep idle mode...\n", __func__);
	
	/* set the DRAM to Self Refresh mode */
	writel(0x7, DDR_OPERATION_BASE);

	__disable_mem();
	__disable_clock();
	
	cpu_do_idle();

	__enable_clock();
	__enable_mem();
	__restore_hdpower();

	printk("%s: exiting deep idle mode...\n", __func__);
}

static int kirkwood_pm_valid(suspend_state_t state)
{
	return ((state == PM_SUSPEND_MEM) || (state == PM_SUSPEND_STANDBY));
}

static int kirkwood_pm_enter(suspend_state_t state)
{
	switch(state)
	{
	case PM_SUSPEND_STANDBY:
		kirkwood_idle();
		break;
	case PM_SUSPEND_MEM:
		kirkwood_deepidle();
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static struct platform_suspend_ops kirkwood_pm_ops =
{
	.valid = kirkwood_pm_valid,
	.enter = kirkwood_pm_enter,
};

static int kirkwood_pm_register(void)
{
	printk("%s: Power Management for Marvell Kirkwood.\n", __func__);
	
	suspend_set_ops(&kirkwood_pm_ops);
	return 0;
}

__initcall(kirkwood_pm_register);
