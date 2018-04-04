/*
 *  Xenos RingBuffer character driver.
 *
 *  Copyright (C) 2018 Justin Moore
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/ioctl.h>
#include <linux/circ_buf.h>
#include <linux/log2.h>
#include <linux/interrupt.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/types.h>

#include <asm/cacheflush.h>

#define DRV_NAME	"xenos_rb"
#define DRV_VERSION	"0.1"

#define XENOS_VECTOR 0x58

#define IOC_MAGIC 0x58524230
#define IOCTL_RESET _IO(IOC_MAGIC, 0)

static int xenosrb_size = 0x10000;
module_param(xenosrb_size, int, 0444);

static struct {
    void __iomem *graphics_base;

    struct circ_buf primary_rb;
    int rb_size;

    spinlock_t gfx_lock;
    spinlock_t write_lock;
} xenosrb_info;

static void xenosrb_reset_ring(void)
{
    int rb_cntl;

    rb_cntl = in_be32(xenosrb_info.graphics_base + 0x0704);
    rb_cntl |= 0x08000000; /* RB_NO_UPDATE = 1 */

    /* RB_RPTR_WR_ENA = 1 */
    out_be32(xenosrb_info.graphics_base + 0x0704, rb_cntl | 0x80000000);

    out_be32(xenosrb_info.graphics_base + 0x071C, 0x00000000); /* CP_RB_RPTR_WR */
    out_be32(xenosrb_info.graphics_base + 0x0714, 0x00000000); /* CP_RB_WPTR */
    udelay(1000);

    /* RB_RPTR_WR_ENA = 0 */
    out_be32(xenosrb_info.graphics_base + 0x0704, rb_cntl);
}

static irqreturn_t xenos_interrupt(int irq, void* dev)
{
	return IRQ_HANDLED;
}

static ssize_t xenosrb_read(struct file *file, char __user *buf, size_t count,
			    loff_t *ppos)
{
	return -EINVAL;
}

static ssize_t xenosrb_write(struct file *file, const char __user *buf,
			     size_t count, loff_t *ppos)
{
    struct circ_buf *primary_rb = &xenosrb_info.primary_rb;
    size_t space;
	size_t trail_space;
	int head_ptr;
	unsigned long flags;

    if (*ppos || (count & 0x3) != 0x0)
        return -EINVAL;

	spin_lock(&xenosrb_info.write_lock);
    space = CIRC_SPACE(primary_rb->head, primary_rb->tail, xenosrb_info.rb_size);
    if (space < count) {
	    /* Update the head pointer and check again */
	    primary_rb->tail = in_be32(xenosrb_info.graphics_base + 0x0710);

	    space = CIRC_SPACE(primary_rb->head, primary_rb->tail,
			       xenosrb_info.rb_size);

	    if (space < count) {
		    spin_unlock(&xenosrb_info.write_lock);
		    return -EBUSY;
	    }
    }

    trail_space = CIRC_SPACE_TO_END(primary_rb->head, primary_rb->tail,
				   xenosrb_info.rb_size);

    /* Copy bytes to end of ringbuffer */
    if (copy_from_user(primary_rb->buf + primary_rb->head, buf,
		       min(trail_space, count))) {
	    spin_unlock(&xenosrb_info.write_lock);
	    return -EFAULT;
	}

	head_ptr = primary_rb->head + min(trail_space, count);

	if (trail_space < count) {
		/* Wrap around, copy bytes to beginning of ring buffer. */
		if (copy_from_user(primary_rb->buf, buf + trail_space,
				   count - trail_space)) {
            spin_unlock(&xenosrb_info.write_lock);
			return -EFAULT;
		}

		head_ptr = count - trail_space;
	}

	/* Update our tail pointer. */
	primary_rb->head = head_ptr & (xenosrb_info.rb_size - 1);
	spin_unlock(&xenosrb_info.write_lock);

    /* Flush the CPU cache. */
    flush_dcache_range((uintptr_t)primary_rb->buf, (uintptr_t)primary_rb->buf + xenosrb_info.rb_size);

    spin_lock_irqsave(&xenosrb_info.gfx_lock, flags);
    out_be32(xenosrb_info.graphics_base + 0x0714, head_ptr); /* CP_RB_WPTR */
    spin_unlock_irqrestore(&xenosrb_info.gfx_lock, flags);

	return count;
}

static long xenosrb_ioctl(struct file *file, unsigned int cmd,
			  unsigned long arg)
{
    unsigned long flags;

    switch (cmd) {
        case IOCTL_RESET:
            spin_lock_irqsave(&xenosrb_info.gfx_lock, flags);

            xenosrb_reset_ring();

            /* FIXME: This most definitely will cause a data race between xenosrb_read */
            xenosrb_info.primary_rb.head = 0;
            xenosrb_info.primary_rb.tail = 0;

            spin_unlock_irqrestore(&xenosrb_info.gfx_lock, flags);
            return 0;
    }

    return -EINVAL;
}

static const struct file_operations xenos_fops = {
	.read = xenosrb_read,
	.write = xenosrb_write,
	.open = nonseekable_open,
    .unlocked_ioctl = xenosrb_ioctl,
};

static struct miscdevice xenosrb_dev = {
	.minor =  MISC_DYNAMIC_MINOR,
	"xenosrb",
	&xenos_fops
};

static int __init xenosrb_init(void)
{
    unsigned long flags;
	int rc = 0;

    xenosrb_info.graphics_base = ioremap(0x200EC800000ULL, 0x10000);
    if (!xenosrb_info.graphics_base) {
        printk(KERN_ERR "%s: failed to ioremap gfx regs\n", __func__);
        rc = -EIO;
        return rc;
    }

    rc = misc_register(&xenosrb_dev);
    if (rc) {
        goto err_release_reg;
    }

    xenosrb_info.rb_size = 1 << ((order_base_2(xenosrb_size) & 0xFF) | 0x2);
    xenosrb_info.primary_rb.buf = kzalloc(xenosrb_info.rb_size, GFP_KERNEL);
    if (!xenosrb_info.primary_rb.buf) {
	    rc = -ENOMEM;
	    goto err_unregister_dev;
    }

    spin_lock_init(&xenosrb_info.gfx_lock);
    spin_lock_init(&xenosrb_info.write_lock);

    /* Setup the ringbuffer with interrupts disabled */
    spin_lock_irqsave(&xenosrb_info.gfx_lock, flags);

    xenosrb_info.primary_rb.head = 0;
    xenosrb_info.primary_rb.tail = 0;
    xenosrb_reset_ring();

    /* CP_RB_CNTL */
    out_be32(xenosrb_info.graphics_base + 0x0704, 0x0802 | (order_base_2(xenosrb_info.rb_size) & 0xFF));

    /* CP_RB_BASE */
    out_be32(xenosrb_info.graphics_base + 0x0700, virt_to_phys(xenosrb_info.primary_rb.buf));

    /* CP_RB_WPTR_DELAY */
    out_be32(xenosrb_info.graphics_base + 0x0718, 0x0010);

    /* scratch setup */
    out_be32(xenosrb_info.graphics_base + 0x0774, 0x00000000); /* SCRATCH_ADDR */
    out_be32(xenosrb_info.graphics_base + 0x0770, 0x00000000); /* SCRATCH_UMSK */

    spin_unlock_irqrestore(&xenosrb_info.gfx_lock, flags);

    rc = request_irq(XENOS_VECTOR, xenos_interrupt, 0, "xenos", NULL);
	if (rc) {
		printk(KERN_ERR "%s: failed to request IRQ 0x%.2X\n", __func__,
		       XENOS_VECTOR);
	}

	printk("XenosRB Character Driver Initialized, ring size = %d\n", xenosrb_info.rb_size);
	return 0;

	xenosrb_reset_ring();
	kfree(xenosrb_info.primary_rb.buf);
err_unregister_dev:
    misc_deregister(&xenosrb_dev);
err_release_reg:
	iounmap(xenosrb_info.graphics_base);

	return rc;
}

static void __exit xenosrb_exit(void)
{
    unsigned long flags;

    free_irq(XENOS_VECTOR, NULL);
    misc_deregister(&xenosrb_dev);

    spin_lock_irqsave(&xenosrb_info.gfx_lock, flags);
    xenosrb_reset_ring();

    /* CP_RB_CNTL */
    out_be32(xenosrb_info.graphics_base + 0x0704, 0x00000000);

    /* CP_RB_BASE */
    out_be32(xenosrb_info.graphics_base + 0x0700, 0x00000000);

    out_be32(xenosrb_info.graphics_base + 0x0774, 0x00000000); /* SCRATCH_ADDR */
    out_be32(xenosrb_info.graphics_base + 0x0770, 0x00000000); /* SCRATCH_UMSK */

    spin_unlock_irqrestore(&xenosrb_info.gfx_lock, flags);

    kfree(xenosrb_info.primary_rb.buf);
    iounmap(xenosrb_info.graphics_base);
}

module_init(xenosrb_init);
module_exit(xenosrb_exit);

MODULE_AUTHOR("Justin Moore <arkolbed@gmail.com>");
MODULE_DESCRIPTION("Ring Buffer Driver for Xenos GPU");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);

