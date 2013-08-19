#include <asm/udbg.h>
#include <asm/io.h>

/* NOTE: Isn't tested and disabled in KConfig until fixed */

extern u8 real_readb(volatile u8 __iomem  *addr);
extern void real_writeb(u8 data, volatile u8 __iomem *addr);

static void udbg_xenon_real_putc(char c)
{
	if (c == '\n')
		udbg_xenon_real_putc('\r');
	while (!(real_readb((void*)0x200ea001018ULL)&0x02));
	real_writeb(c, (void*)0x200ea001014ULL);
}

int udbg_init_xenon(void)
{
	udbg_putc = udbg_xenon_real_putc;

	return 0;
}
