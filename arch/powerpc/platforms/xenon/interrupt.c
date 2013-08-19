/*
 * Xenon interrupt controller,
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License v2
 * as published by the Free Software Foundation.
 */

#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/percpu.h>
#include <linux/types.h>
#include <linux/ioport.h>

#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/prom.h>
#include <asm/ptrace.h>
#include <asm/machdep.h>

#include "interrupt.h"

static void *iic_base,
	*bridge_base, // ea000000
	*biu,         // e1000000
	*graphics;    // ec800000
static struct irq_host *host;

#define XENON_NR_IRQS 128

#define PRIO_IPI_4       0x08
#define PRIO_IPI_3       0x10
#define PRIO_SMM         0x14
#define PRIO_SFCX        0x18
#define PRIO_SATA_HDD    0x20
#define PRIO_SATA_CDROM  0x24
#define PRIO_OHCI_0      0x2c
#define PRIO_EHCI_0      0x30
#define PRIO_OHCI_1      0x34
#define PRIO_EHCI_1      0x38
#define PRIO_XMA         0x40
#define PRIO_AUDIO       0x44
#define PRIO_ENET        0x4C
#define PRIO_XPS         0x54
#define PRIO_GRAPHICS    0x58
#define PRIO_PROFILER    0x60
#define PRIO_BIU         0x64
#define PRIO_IOC         0x68
#define PRIO_FSB         0x6c
#define PRIO_IPI_2       0x70
#define PRIO_CLOCK       0x74
#define PRIO_IPI_1       0x78

/* bridge (PCI) IRQ -> CPU IRQ */
static int xenon_pci_irq_map[] = {
		PRIO_CLOCK, PRIO_SATA_CDROM, PRIO_SATA_HDD, PRIO_SMM,
		PRIO_OHCI_0, PRIO_EHCI_0, PRIO_OHCI_1, PRIO_EHCI_1,
		-1, -1, PRIO_ENET, PRIO_XMA,
		PRIO_AUDIO, PRIO_SFCX, -1, -1};

static void disconnect_pci_irq(int prio)
{
	int i;

	for (i=0; i<0x10; ++i)
		if (xenon_pci_irq_map[i] == prio)
			writel(0, bridge_base + 0x10 + i * 4);
}

	/* connects an PCI IRQ to CPU #0 */
static void connect_pci_irq(int prio)
{
	int i;

	for (i=0; i<0x10; ++i)
		if (xenon_pci_irq_map[i] == prio)
			writel(0x0800180 | (xenon_pci_irq_map[i]/4), bridge_base + 0x10 + i * 4);
}

static void iic_mask(unsigned int irq)
{
	disconnect_pci_irq(irq);
}

static void iic_unmask(unsigned int irq)
{
	int i;
	connect_pci_irq(irq);
	for (i=0; i<6; ++i)
		__raw_writeq(0, iic_base + i * 0x1000 + 0x68);
}

void xenon_init_irq_on_cpu(int cpu)
{
		/* init that cpu's interrupt controller */
	__raw_writeq(0x7C, iic_base + cpu * 0x1000 + 0x70);
	__raw_writeq(0, iic_base + cpu * 0x1000 + 0x8);      /* irql */
	__raw_writeq(1<<cpu, iic_base + cpu * 0x1000);       /* "who am i" */

		/* ack all outstanding interrupts */
	while (__raw_readq(iic_base + cpu * 0x1000 + 0x50) != 0x7C);
	__raw_writeq(0, iic_base + cpu * 0x1000 + 0x68);
}

static void iic_eoi(unsigned int irq)
{
	int cpu = hard_smp_processor_id();
	void *my_iic_base = iic_base + cpu * 0x1000;
	__raw_writeq(0, my_iic_base + 0x68);
	mb();
	__raw_readq(my_iic_base + 0x8);
}

static struct irq_chip xenon_pic = {
	.typename = " XENON-PIC ",
	.mask = iic_mask,
	.unmask = iic_unmask,
	.eoi = iic_eoi,
};

/* Get an IRQ number from the pending state register of the IIC */
static unsigned int iic_get_irq(void)
{
	int cpu = hard_smp_processor_id();
	void *my_iic_base;
	int index;

	my_iic_base = iic_base + cpu * 0x1000;

	index = __raw_readq(my_iic_base + 0x50) & 0x7F; /* read destructive pending interrupt */

	__raw_writeq(0x7c, my_iic_base + 0x08); /* current task priority */
	mb();
	__raw_readq(my_iic_base + 0x8);

		/* HACK: we will handle some (otherwise unhandled) interrupts here
		   to prevent them flooding. */
	switch (index)
	{
	case PRIO_GRAPHICS:
		writel(0, graphics + 0xed0);
		writel(0, graphics + 0x6540);
		break;
	case PRIO_IOC:
	{
		writel(0, biu + 0x4002c);
		break;
	}
	case PRIO_CLOCK:
	{
		writel(0, bridge_base + 0x106C);
		break;
	}
	default:
		break;
	}

		/* HACK: we need to ACK unhandled interrupts here */
	if (!irq_desc[index].action)
	{
		printk(KERN_WARNING "IRQ %02x unhandled, doing local EOI\n", index);
		__raw_writeq(0, my_iic_base + 0x60);
		iic_eoi(index);
		return NO_IRQ;
	}

	if (index == 0x7C)
		return NO_IRQ;
	else
		return index;
}

static int xenon_irq_host_map(struct irq_host *h, unsigned int virq,
				irq_hw_number_t hw)
{
	set_irq_chip_and_handler(virq, &xenon_pic, handle_percpu_irq);
	return 0;
}

static int xenon_irq_host_match(struct irq_host *h, struct device_node *node)
{
	return h->host_data != NULL && node == h->host_data;
}

static struct irq_host_ops xenon_irq_host_ops = {
	.map = xenon_irq_host_map,
	.match = xenon_irq_host_match,
};

void __init xenon_iic_init_IRQ(void)
{
	int i;
	struct device_node *dn;

	printk("XENON init IRQ\n");

			/* search for our interrupt controller inside the device tree */
	for (dn = NULL;
	     (dn = of_find_node_by_name(dn,"interrupt-controller")) != NULL;) {
		if (!device_is_compatible(dn,
				     "xenon"))
			continue;

		irq_set_virq_count(0x80);
		iic_base = ioremap_nocache(0x20000050000, 0x10000);

		host = irq_alloc_host(IRQ_HOST_MAP_NOMAP, 0, &xenon_irq_host_ops, 0);
		host->host_data = of_node_get(dn);
		BUG_ON(host == NULL);
		irq_set_default_host(host);
	}

	ppc_md.get_irq = iic_get_irq;

	bridge_base = ioremap_nocache(0xea000000, 0x10000);
	biu = ioremap_nocache(0xe1000000, 0x2000000);
	graphics = ioremap_nocache(0xec800000, 0x10000);

		/* initialize interrupts */
	writel(0, bridge_base);
	writel(0x40000000, bridge_base + 4);

	writel(0x40000000, biu + 0x40074);
	writel(0xea000050, biu + 0x40078);

	writel(0, bridge_base + 0xc);
	writel(0x3, bridge_base);

	/* disconnect all PCI IRQs until they are requested */
	for (i=0; i<0x10; ++i)
		writel(0, bridge_base + 0x10 + i * 4);

	xenon_init_irq_on_cpu(0);
}

#ifdef CONFIG_SMP

static int ipi_to_prio(int ipi)
{
	switch (ipi)
	{
	case PPC_MSG_CALL_FUNCTION:
		return PRIO_IPI_1;
		break;
	case PPC_MSG_RESCHEDULE:
		return PRIO_IPI_2;
		break;
	//case PPC_MSG_MIGRATE_TASK:
	//	return PRIO_IPI_3;
	//	break;
	case PPC_MSG_DEBUGGER_BREAK:
		return PRIO_IPI_4;
		break;
	default:
		BUG();
	}
	return 0;
}

void xenon_cause_IPI(int target, int msg)
{
	int ipi_prio;

	ipi_prio = ipi_to_prio(msg);

	__raw_writeq( (0x10000<<target) | ipi_prio, iic_base + 0x10 + hard_smp_processor_id() * 0x1000);
}

static irqreturn_t xenon_ipi_action(int irq, void *dev_id)
{
	int ipi = (int)(long)dev_id;
	smp_message_recv(ipi);
	return IRQ_HANDLED;
}

static void xenon_request_ipi(int ipi, const char *name)
{
	int prio = ipi_to_prio(ipi), virq;

	virq = irq_create_mapping(host, prio);
	if (virq == NO_IRQ)
	{
		printk(KERN_ERR
				"xenon_request_ipi: failed to map IPI%d (%s)\n", prio, name);
		return;
	}

	if (request_irq(prio, xenon_ipi_action, IRQF_DISABLED,
			name, (void *)(long)ipi))
		printk(KERN_ERR "request irq for ipi failed!\n");
}

void xenon_request_IPIs(void)
{
	xenon_request_ipi(PPC_MSG_CALL_FUNCTION, "IPI-call");
	xenon_request_ipi(PPC_MSG_RESCHEDULE, "IPI-resched");
#ifdef CONFIG_DEBUGGER
	xenon_request_ipi(PPC_MSG_DEBUGGER_BREAK, "IPI-debug");
#endif /* CONFIG_DEBUGGER */
}

#endif
