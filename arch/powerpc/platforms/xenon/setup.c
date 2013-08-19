/*
 *  linux/arch/powerpc/platforms/xenon/xenon_setup.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 *  Adapted from 'alpha' version by Gary Thomas
 *  Modified by Cort Dougan (cort@cs.nmt.edu)
 *  Modified by PPC64 Team, IBM Corp
 *  Modified by Cell Team, IBM Deutschland Entwicklung GmbH
 *  Modified by anonymous xbox360 hacker
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#define DEBUG

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/slab.h>
#include <linux/user.h>
#include <linux/reboot.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/seq_file.h>
#include <linux/root_dev.h>
#include <linux/console.h>
#include <linux/mutex.h>
#include <linux/memory_hotplug.h>

#include <asm/mmu.h>
#include <asm/processor.h>
#include <asm/io.h>
#include <asm/kexec.h>
#include <asm/pgtable.h>
#include <asm/prom.h>
#include <asm/rtas.h>
#include <asm/pci-bridge.h>
#include <asm/iommu.h>
#include <asm/dma.h>
#include <asm/machdep.h>
#include <asm/time.h>
#include <asm/nvram.h>
#include <asm/cputable.h>
#include <asm/ppc-pci.h>
#include <asm/irq.h>
#include <asm/spu.h>
#include <asm/spu_priv1.h>
#include <asm/udbg.h>
#include <asm/cacheflush.h>
#include "interrupt.h"


#ifdef DEBUG
#define DBG(fmt...) udbg_printf(fmt)
#else
#define DBG(fmt...)
#endif

void __init xenon_pci_init(void);
#ifdef CONFIG_SMP
extern void smp_init_xenon(void);
#endif


static void xenon_show_cpuinfo(struct seq_file *m)
{
	struct device_node *root;
	const char *model = "";

	root = of_find_node_by_path("/");
	if (root)
		model = get_property(root, "model", NULL);
	seq_printf(m, "machine\t\t: CHRP %s\n", model);
	of_node_put(root);
}

static void xenon_progress(char *s, unsigned short hex)
{
	printk("*** %04x : %s\n", hex, s ? s : "");
}

static void __init xenon_pcibios_fixup(void)
{
	struct pci_dev *dev = NULL;

	for_each_pci_dev(dev)
		pci_read_irq_line(dev);
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

	if (ROOT_DEV == 0) {
		printk("No ramdisk, default root is /dev/hda2\n");
		ROOT_DEV = Root_HDA2;
	}

	xenon_pci_init();
	/* Find and initialize PCI host bridges */
	init_pci_config_tokens();
#ifdef CONFIG_DUMMY_CONSOLE
	conswitchp = &dummy_con;
#endif
}

void __init xenon_hpte_init(unsigned long htab_size);

static int __init xenon_probe(void)
{
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
	.check_legacy_ioport	= xenon_check_legacy_ioport,
	.progress		= xenon_progress,
	.init_IRQ       	= xenon_init_irq,
	.pcibios_fixup		= xenon_pcibios_fixup,
#ifdef CONFIG_KEXEC
	.machine_kexec		= default_machine_kexec,
	.machine_kexec_prepare	= default_machine_kexec_prepare,
	.machine_crash_shutdown	= default_machine_crash_shutdown,
#endif
};
