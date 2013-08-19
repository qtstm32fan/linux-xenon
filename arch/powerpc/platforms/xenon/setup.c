/*
 *  linux/arch/powerpc/platforms/xenon/xenon_setup.c
 *
 *  Maintained by: Felix Domke <tmbinc@elitedvb.net>
 *  Minor modification by: wolie <wolie@telia.com>  
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
// #define DEBUG

#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/root_dev.h>
#include <linux/console.h>
#include <linux/kexec.h>

#include <asm/mmu-hash64.h>

#include <asm/mmu.h>
#include <asm/prom.h>
#include <asm/pci-bridge.h>
#include <asm/ppc-pci.h>
#include "interrupt.h"
#include "pci.h"
#include "smp.h"

static void xenon_show_cpuinfo(struct seq_file *m)
{
	struct device_node *root;
	const char *model = "";

	root = of_find_node_by_path("/");
	if (root)
		model = of_get_property(root, "model", NULL);
	seq_printf(m, "machine\t\t: %s\n", model);
	of_node_put(root);
}

static void __init xenon_init_irq(void)
{
	xenon_iic_init_IRQ();
}

static void __init xenon_setup_arch(void)
{
#ifdef CONFIG_SMP
	smp_init_xenon();
#endif
		/* init to some ~sane value until calibrate_delay() runs */
	loops_per_jiffy = 50000000;

	if (ROOT_DEV == 0)
		ROOT_DEV = Root_SDA1;

	xenon_pci_init();
#ifdef CONFIG_DUMMY_CONSOLE
	conswitchp = &dummy_con;
#endif
}

static int __init xenon_probe(void)
{
	unsigned long root = of_get_flat_dt_root();

  if (!of_flat_dt_is_compatible(root, "XENON"))
  	return 0;

	hpte_init_native();

	return 1;
}

static int xenon_check_legacy_ioport(unsigned int baseport)
{
	return -ENODEV;
}

define_machine(xenon) {
	.name			= "Xenon",
	.probe			= xenon_probe,
	.setup_arch		= xenon_setup_arch,
	.show_cpuinfo		= xenon_show_cpuinfo,
	.calibrate_decr		= generic_calibrate_decr,
	.init_IRQ       	= xenon_init_irq,
#if defined(CONFIG_KEXEC)
	.machine_kexec		= default_machine_kexec,
	.machine_kexec_prepare	= default_machine_kexec_prepare,
	.machine_crash_shutdown	= default_machine_crash_shutdown,
#endif
}; 

