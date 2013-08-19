/*
 * SMP support for Xenon machines.
 *
 * Based on CBE's smp.c.
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

// #define DEBUG

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/cache.h>
#include <linux/err.h>
#include <linux/sysdev.h>
#include <linux/cpu.h>

#include <asm/ptrace.h>
#include <asm/atomic.h>
#include <asm/irq.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/smp.h>
#include <asm/paca.h>
#include <asm/time.h>
#include <asm/machdep.h>
#include <asm/cputable.h>
#include <asm/firmware.h>
#include <asm/system.h>
#include <asm/rtas.h>

#ifdef DEBUG
#define DBG(fmt...) printk(fmt)
#else
#define DBG(fmt...)
#endif

/*
 * The primary thread of each non-boot processor is recorded here before
 * smp init.
 */
static cpumask_t of_spin_map;

void smp_init_xenon(void);

extern void xenon_request_IPIs(void);
extern void xenon_init_irq_on_cpu(int cpu);

static int __init smp_xenon_probe(void)
{
	xenon_request_IPIs();

	return cpus_weight(cpu_possible_map);
}

static void __devinit smp_xenon_setup_cpu(int cpu)
{
	if (cpu != boot_cpuid)
		xenon_init_irq_on_cpu(cpu);
}

static void __devinit smp_xenon_kick_cpu(int nr)
{
	BUG_ON(nr < 0 || nr >= NR_CPUS);

	DBG("smp_xenon_kick_cpu %d\n", nr);

	/*
	 * The processor is currently spinning, waiting for the
	 * cpu_start field to become non-zero After we set cpu_start,
	 * the processor will continue on to secondary_start
	 */
	paca[nr].cpu_start = 1;
}

static int smp_xenon_cpu_bootable(unsigned int nr)
{
	/* Special case - we inhibit secondary thread startup
	 * during boot if the user requests it.  Odd-numbered
	 * cpus are assumed to be secondary threads.
	 */
	if (system_state < SYSTEM_RUNNING &&
	    cpu_has_feature(CPU_FTR_SMT) &&
	    !smt_enabled_at_boot && nr % 2 != 0)
		return 0;

		/* FIXME: secondary threads behave instable. */
	if (nr & 1)
		return 0;

	return 1;
}

extern void xenon_cause_IPI(int target, int msg);

static void smp_xenon_message_pass(int target, int msg)
{
	unsigned int i;

	if (target < NR_CPUS) {
		xenon_cause_IPI(target, msg);
	} else {
		for_each_online_cpu(i) {
			if (target == MSG_ALL_BUT_SELF
			    && i == smp_processor_id())
				continue;
			xenon_cause_IPI(i, msg);
		}
	}
}

static struct smp_ops_t xenon_smp_ops = {
	.message_pass	= smp_xenon_message_pass,
	.probe		= smp_xenon_probe,
	.kick_cpu	= smp_xenon_kick_cpu,
	.setup_cpu	= smp_xenon_setup_cpu,
	.cpu_bootable	= smp_xenon_cpu_bootable,
};

/* This is called very early */
void __init smp_init_xenon(void)
{
	int i;

	DBG(" -> smp_init_xenon()\n");

	smp_ops = &xenon_smp_ops;

	/* Mark threads which are still spinning in hold loops. */
	if (cpu_has_feature(CPU_FTR_SMT)) {
		for_each_present_cpu(i) {
			if (i % 2 == 0)
				/*
				 * Even-numbered logical cpus correspond to
				 * primary threads.
				 */
				cpu_set(i, of_spin_map);
		}
	} else {
		of_spin_map = cpu_present_map;
	}

	cpu_clear(boot_cpuid, of_spin_map);

	DBG(" <- smp_init_xenon()\n");
}
