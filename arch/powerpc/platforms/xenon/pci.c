/*
 * Xenon PCI support
 * Maintained by: Felix Domke <tmbinc@elitedvb.net>
 * Minor modification by: wolie <wolie@telia.com> 
 * based on:
 * Copyright (C) 2004 Benjamin Herrenschmuidt (benh@kernel.crashing.org),
 *		      IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

// #define DEBUG

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/bootmem.h>

#include <asm/sections.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/pci-bridge.h>
#include <asm/machdep.h>
#include <asm/iommu.h>
#include <asm/ppc-pci.h>

#ifdef DEBUG
#define DBG(x...) printk(x)
#else
#define DBG(x...)
#endif

#define OFFSET(devfn) ((devfn+256)<<12)

static int xenon_pci_read_config(struct pci_bus *bus, unsigned int devfn,
			      int offset, int len, u32 *val)
{
	struct pci_controller *hose;
	void* addr;

	hose = pci_bus_to_host(bus);
	if (hose == NULL)
		return PCIBIOS_DEVICE_NOT_FOUND;

	DBG("xenon_pci_read_config,slot %d, func %d\n", PCI_SLOT(devfn), PCI_FUNC(devfn));

	if (PCI_SLOT(devfn) >= 32)
		return PCIBIOS_DEVICE_NOT_FOUND;
	if (PCI_SLOT(devfn) == 3)
		return PCIBIOS_DEVICE_NOT_FOUND;
	if (PCI_SLOT(devfn) == 6)
		return PCIBIOS_DEVICE_NOT_FOUND;
	if (PCI_SLOT(devfn) >= 0xB)
		return PCIBIOS_DEVICE_NOT_FOUND;
	if (PCI_FUNC(devfn) >= 2)
		return PCIBIOS_DEVICE_NOT_FOUND;
	DBG("xenon_pci_read_config, %p, devfn=%d, offset=%d, len=%d\n", bus, devfn, offset, len);

	addr = ((void*)hose->cfg_addr) + OFFSET(devfn) + offset;

	/*
	 * Note: the caller has already checked that offset is
	 * suitably aligned and that len is 1, 2 or 4.
	 */
	switch (len) {
	case 1:
		*val = in_8((u8 *)addr);
		break;
	case 2:
		*val = in_le16((u16 *)addr);
		break;
	default:
		*val = in_le32((u32 *)addr);
		break;
	}
	DBG("->%08x\n", (int)*val);
	return PCIBIOS_SUCCESSFUL;
}

static int xenon_pci_write_config(struct pci_bus *bus, unsigned int devfn,
			       int offset, int len, u32 val)
{
	struct pci_controller *hose;
	void *addr;

	hose = pci_bus_to_host(bus);
	if (hose == NULL)
		return PCIBIOS_DEVICE_NOT_FOUND;

	if (PCI_SLOT(devfn) >= 32)
		return PCIBIOS_DEVICE_NOT_FOUND;

	if (PCI_SLOT(devfn) == 3)
		return PCIBIOS_DEVICE_NOT_FOUND;
	DBG("xenon_pci_write_config, %p, devfn=%d, offset=%x, len=%d, val=%08x\n", bus, devfn, offset, len, val);

	addr = ((void*)hose->cfg_addr) + OFFSET(devfn) + offset;
	if (len == 4)
		DBG("was: %08x\n", readl(addr));
	if (len == 2)
		DBG("was: %04x\n", readw(addr));
	if (len == 1)
		DBG("was: %02x\n", readb(addr));
	/*
	 * Note: the caller has already checked that offset is
	 * suitably aligned and that len is 1, 2 or 4.
	 */
	switch (len) {
	case 1:
		writeb(val, addr);
		break;
	case 2:
		writew(val, addr);
		break;
	default:
		writel(val, addr);
		break;
	}
	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops xenon_pci_ops =
{
	xenon_pci_read_config,
	xenon_pci_write_config
};

void __init xenon_pci_init(void)
{
	struct pci_controller *hose;
	struct device_node *np, *root;
	struct device_node *dev = NULL;

	root = of_find_node_by_path("/");
	if (root == NULL) {
		printk(KERN_CRIT "xenon_pci_init: can't find root of device tree\n");
		return;
	}
	for (np = NULL; (np = of_get_next_child(root, np)) != NULL;) {
		if (np->name == NULL)
			continue;
		if (strcmp(np->name, "pci") == 0) {
			of_node_get(np);
			dev = np;
		}
	}
	of_node_put(root);

	if (!dev)
	{
		printk("couldn't find PCI node!\n");
		return;
	}

	hose = pcibios_alloc_controller(dev);
	if (hose == NULL)
	{
		printk("pcibios_alloc_controller failed!\n");
		return;
	}

	hose->first_busno = 0;
	hose->last_busno = 0;

	hose->ops = &xenon_pci_ops;
	hose->cfg_addr = ioremap(0xd0000000, 0x1000000);

	pci_process_bridge_OF_ranges(hose, dev, 1);

	/* Setup the linkage between OF nodes and PHBs */
	pci_devs_phb_init();

	/* Tell pci.c to not change any resource allocations.  */
	pci_probe_only = 1;

	of_node_put(dev);
	DBG("PCI initialized\n");

	pci_io_base = 0;

	ppc_md.pci_dma_dev_setup = NULL;
	ppc_md.pci_dma_bus_setup = NULL;
	set_pci_dma_ops(&dma_direct_ops);
}

