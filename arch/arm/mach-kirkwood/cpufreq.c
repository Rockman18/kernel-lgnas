/*
 * arch/arm/mach-kirkwood/cpufreq.c
 *
 * Clock scaling for Kirkwood SoC
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/cpufreq.h>
#include <linux/io.h>
#include <mach/kirkwood.h>
#include <mach/bridge-regs.h>
#include "common.h"

#define CPU_CLOCK_TBL {0, 400000000, 0, 0, \
											600000000, 0, 800000000, 1000000000, \
											0, 1200000000, 0, 0, \
											1500000000, 1600000000, 1800000000, 2000000000}
#define DDR_CLOCK_RATIO_TBL	{{1, 1}, {0, 0}, {2, 1}, {5, 2}, \
														 {3, 1}, {0, 0}, {4, 1}, {9, 2}, \
														 {5, 1}, {6, 1}, {0, 0}, {0, 0}, \
														 {0, 0}, {0, 0}, {0, 0}, {0, 0}}
#define MSAR_CPUCLOCK_EXTRACT(x)	(((x & 0x2) >> 1) | ((x & 0x400000) >> 21) | \
																	((x & 0x18) >> 1))
#define MSAR_DDRCLOCK_RATIO_OFFS	5
#define MSAR_DDRCLOCK_RATIO_MASK	(0xf << MSAR_DDRCLOCK_RATIO_OFFS)
#define PMC_POWERSAVE_EN (1 << 11)

enum kw_cpufreq_range {
	KW_CPUFREQ_LOW = 0,
	KW_CPUFREQ_HIGH = 1
};

static struct cpufreq_frequency_table kw_freqs[] = {
	{KW_CPUFREQ_LOW, 0},
	{KW_CPUFREQ_HIGH, 0},
	{0, CPUFREQ_TABLE_END}
};

static u32 __get_cpu_clock(void)
{
	u32 cpu_clock_rate, reg;
	u32 cpu_clock[] = CPU_CLOCK_TBL;

	reg = readl(SAMPLE_AT_RESET);
	cpu_clock_rate = MSAR_CPUCLOCK_EXTRACT(reg);
	cpu_clock_rate = cpu_clock[cpu_clock_rate];
	
	return cpu_clock_rate;
}

static u32 __get_sys_clock(void)
{
	u32 sys_clock_rate, reg, cpu_clock_rate, ddr_ratio_index;
	u32 cpu_clock[] = CPU_CLOCK_TBL;
	u32 ddr_ratio[][2] = DDR_CLOCK_RATIO_TBL;
	
	reg = readl(SAMPLE_AT_RESET);
	cpu_clock_rate = MSAR_CPUCLOCK_EXTRACT(reg);
	cpu_clock_rate = cpu_clock[cpu_clock_rate];
	
	ddr_ratio_index = reg & MSAR_DDRCLOCK_RATIO_MASK;
	ddr_ratio_index = ddr_ratio_index >> MSAR_DDRCLOCK_RATIO_OFFS;
	if(ddr_ratio[ddr_ratio_index][0] != 0)
		sys_clock_rate = ((cpu_clock_rate * ddr_ratio[ddr_ratio_index][1]) /
			ddr_ratio[ddr_ratio_index][0]);
	else
		sys_clock_rate = 0;
	
	return sys_clock_rate;
}

static void __power_save_on(void)
{
	unsigned long old, temp;
	/* disable int */
	__asm__ __volatile__("mrs %0, cpsr\n"
											 "orr %1, %0, #0xc0\n"
											 "msr cpsr_c, %1"
											 : "=r" (old), "=r" (temp)
											 :
											 : "memory");
	/* set soc in power save */
	writel(readl(CLOCK_GATING_CTRL) | PMC_POWERSAVE_EN, CLOCK_GATING_CTRL);
	/* wait for int */
	__asm__ __volatile__("mcr p15, 0, r0, c7, c0, 4");
	/* enable int */
	__asm__ __volatile__("msr cpsr_c, %0"
											 :
											 : "r" (old)
											 : "memory");
}

static void __power_save_off(void)
{
	unsigned long old, temp;
	/* disable int */
	__asm__ __volatile__("mrs %0, cpsr\n"
											 "orr %1, %0, #0xc0\n"
											 "msr cpsr_c, %1"
											 : "=r" (old), "=r" (temp)
											 :
											 : "memory");
	/* set soc in power save */
	writel(readl(CLOCK_GATING_CTRL) & ~PMC_POWERSAVE_EN, CLOCK_GATING_CTRL);
	/* wait for int */
	__asm__ __volatile__("mcr p15, 0, r0, c7, c0, 4");
	/* enable int */
	__asm__ __volatile__("msr cpsr_c, %0"
											 :
											 : "r" (old)
											 : "memory");
}

/*
 * Power management function: set or unset powersave mode
 * FIXME: a better place ?
 */
static inline void kw_set_powersave(u8 on)
{
	printk(KERN_DEBUG "cpufreq: Setting PowerSaveState to %s\n",
		on ? "on" : "off");

	if(on)
		__power_save_on();
	else
		__power_save_off();
}

static int kw_cpufreq_verify(struct cpufreq_policy *policy)
{
	if(unlikely(!cpu_online(policy->cpu)))
		return -ENODEV;

	return cpufreq_frequency_table_verify(policy, kw_freqs);
}

/*
 * Get the current frequency for a given cpu.
 */
static unsigned int kw_cpufreq_get(unsigned int cpu)
{
	unsigned int freq;
	u32 reg;

	if(unlikely(!cpu_online(cpu)))
		return -ENODEV;

	/* To get the current frequency, we have to check if
	 * the powersave mode is set. */
	reg = readl(CLOCK_GATING_CTRL);

	if(reg & PMC_POWERSAVE_EN)
		freq = kw_freqs[KW_CPUFREQ_LOW].frequency;
	else
		freq = kw_freqs[KW_CPUFREQ_HIGH].frequency;

	return freq;
}

/*
 * Set the frequency for a given cpu.
 */
static int kw_cpufreq_target(struct cpufreq_policy *policy,
		unsigned int target_freq, unsigned int relation)
{
	unsigned int index;
	struct cpufreq_freqs freqs;

	if(unlikely(!cpu_online(policy->cpu)))
		return -ENODEV;

	/* Lookup the next frequency */
	if(unlikely(cpufreq_frequency_table_target(policy,
		kw_freqs, target_freq, relation, &index)))
		return -EINVAL;

	freqs.old = policy->cur;
	freqs.new = kw_freqs[index].frequency;
	freqs.cpu = policy->cpu;

	printk(KERN_DEBUG "cpufreq: Setting CPU Frequency to %u KHz\n", freqs.new);

	cpufreq_notify_transition(&freqs, CPUFREQ_PRECHANGE);

	/* Interruptions will be disabled in the low level power mode functions. */
	if(index == KW_CPUFREQ_LOW)
		kw_set_powersave(1);
	else if(index == KW_CPUFREQ_HIGH)
		kw_set_powersave(0);
	else
		return -EINVAL;

	cpufreq_notify_transition(&freqs, CPUFREQ_POSTCHANGE);

	return 0;
}

static int kw_cpufreq_cpu_init(struct cpufreq_policy *policy)
{
	u32 dev, rev;

	if(unlikely(!cpu_online(policy->cpu)))
		return -ENODEV;

	kirkwood_pcie_id(&dev, &rev);
	
	if((dev == MV88F6281_DEV_ID) ||
		(dev == MV88F6192_DEV_ID) ||
		(dev == MV88F6282_DEV_ID)) {
		kw_freqs[KW_CPUFREQ_HIGH].frequency = __get_cpu_clock() / 1000;
		kw_freqs[KW_CPUFREQ_LOW].frequency = __get_sys_clock() / 1000;
	} else {
		return -ENODEV;
	}

	printk(KERN_DEBUG
		"cpufreq: High frequency: %uKHz - Low frequency: %uKHz\n",
		kw_freqs[KW_CPUFREQ_HIGH].frequency,
		kw_freqs[KW_CPUFREQ_LOW].frequency);

	policy->cpuinfo.transition_latency = 1000000;
	policy->cur = kw_cpufreq_get(0);
	policy->governor = CPUFREQ_DEFAULT_GOVERNOR;

	cpufreq_frequency_table_get_attr(kw_freqs, policy->cpu);

	return cpufreq_frequency_table_cpuinfo(policy, kw_freqs);
}


static int kw_cpufreq_cpu_exit(struct cpufreq_policy *policy)
{
	cpufreq_frequency_table_put_attr(policy->cpu);
	return 0;
}

static struct freq_attr *kw_freq_attr[] = {
	&cpufreq_freq_attr_scaling_available_freqs,
	NULL,
};

static struct cpufreq_driver kw_freq_driver = {
	.owner = THIS_MODULE,
	.name = "kw_cpufreq",
	.init = kw_cpufreq_cpu_init,
	.verify = kw_cpufreq_verify,
	.exit = kw_cpufreq_cpu_exit,
	.target = kw_cpufreq_target,
	.get = kw_cpufreq_get,
	.attr = kw_freq_attr,
};

static int __init kw_cpufreq_init(void)
{
	printk(KERN_INFO "cpufreq: Init kirkwood cpufreq driver\n");

	return cpufreq_register_driver(&kw_freq_driver);
}

static void __exit kw_cpufreq_exit(void)
{
	cpufreq_unregister_driver(&kw_freq_driver);
}

MODULE_AUTHOR("Marvell Semiconductors ltd.");
MODULE_DESCRIPTION("CPU frequency scaling for Kirkwood SoC");
MODULE_LICENSE("GPL");
module_init(kw_cpufreq_init);
module_exit(kw_cpufreq_exit);
