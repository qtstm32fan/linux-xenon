/*
 * polling mode stateless debugging stuff, originally for NS16550 Serial Ports
 *
 * c 2001 PPC 64 Team, IBM Corp
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <stdarg.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/console.h>
#include <linux/init.h>
#include <asm/processor.h>
#include <asm/udbg.h>

void (*udbg_putc)(char c);
int (*udbg_getc)(void);
int (*udbg_getc_poll)(void);

/*
 * Early debugging facilities. You can enable _one_ of these via .config,
 * if you do so your kernel _will not boot_ on anything else. Be careful.
 */

extern u8 real_readb(volatile u8 __iomem  *addr);
extern void real_writeb(u8 data, volatile u8 __iomem *addr);

void udbg_xenon_real_putc(char c)
{
	if (c == '\n')
		udbg_xenon_real_putc('\r');
	while (!(real_readb((void*)0x80000200ea001018ULL)&0x02));
	real_writeb(c, (void*)0x80000200ea001014ULL);
}

void __init udbg_early_init(void)
{
#if defined(CONFIG_PPC_EARLY_DEBUG_LPAR)
	/* For LPAR machines that have an HVC console on vterm 0 */
	udbg_init_debug_lpar();
#elif defined(CONFIG_PPC_EARLY_DEBUG_G5)
	/* For use on Apple G5 machines */
	udbg_init_pmac_realmode();
#elif defined(CONFIG_PPC_EARLY_DEBUG_RTAS_PANEL)
	/* RTAS panel debug */
	udbg_init_rtas_panel();
#elif defined(CONFIG_PPC_EARLY_DEBUG_RTAS_CONSOLE)
	/* RTAS console debug */
	udbg_init_rtas_console();
#elif defined(CONFIG_PPC_EARLY_DEBUG_MAPLE)
	/* Maple real mode debug */
	udbg_init_maple_realmode();
#elif defined(CONFIG_PPC_EARLY_DEBUG_ISERIES)
	/* For iSeries - hit Ctrl-x Ctrl-x to see the output */
	udbg_init_iseries();
#elif defined(CONFIG_PPC_EARLY_DEBUG_BEAT)
	udbg_init_debug_beat();
#elif defined(CONFIG_PPC_EARLY_DEBUG_PAS_REALMODE)
	udbg_init_pas_realmode();
#elif defined(CONFIG_BOOTX_TEXT)
	udbg_init_btext();
#endif
	udbg_putc = udbg_xenon_real_putc;
}

/* udbg library, used by xmon et al */
void udbg_puts(const char *s)
{
	if (udbg_putc) {
		char c;

		if (s && *s != '\0') {
			while ((c = *s++) != '\0')
				udbg_putc(c);
		}
	}
#if 0
	else {
		printk("%s", s);
	}
#endif
}

int udbg_write(const char *s, int n)
{
	int remain = n;
	char c;

	if (!udbg_putc)
		return 0;

	if (s && *s != '\0') {
		while (((c = *s++) != '\0') && (remain-- > 0)) {
			udbg_putc(c);
		}
	}

	return n - remain;
}

int udbg_read(char *buf, int buflen)
{
	char *p = buf;
	int i, c;

	if (!udbg_getc)
		return 0;

	for (i = 0; i < buflen; ++i) {
		do {
			c = udbg_getc();
			if (c == -1 && i == 0)
				return -1;

		} while (c == 0x11 || c == 0x13);
		if (c == 0 || c == -1)
			break;
		*p++ = c;
	}

	return i;
}

#define UDBG_BUFSIZE 256
void udbg_printf(const char *fmt, ...)
{
	char buf[UDBG_BUFSIZE];
	va_list args;

	va_start(args, fmt);
	vsnprintf(buf, UDBG_BUFSIZE, fmt, args);
	udbg_puts(buf);
	va_end(args);
}

void __init udbg_progress(char *s, unsigned short hex)
{
	udbg_puts(s);
	udbg_puts("\n");
}

/*
 * Early boot console based on udbg
 */
static void udbg_console_write(struct console *con, const char *s,
		unsigned int n)
{
	udbg_write(s, n);
}

static struct console udbg_console = {
	.name	= "udbg",
	.write	= udbg_console_write,
	.flags	= CON_PRINTBUFFER | CON_ENABLED,
	.index	= -1,
};

static int early_console_initialized;

void __init disable_early_printk(void)
{
	if (!early_console_initialized)
		return;
	if (strstr(boot_command_line, "udbg-immortal")) {
		printk(KERN_INFO "early console immortal !\n");
		return;
	}
	return;
	unregister_console(&udbg_console);
	early_console_initialized = 0;
}

/* called by setup_system */
void register_early_udbg_console(void)
{
	if (early_console_initialized)
		return;
	early_console_initialized = 1;
	register_console(&udbg_console);
}

#if 0   /* if you want to use this as a regular output console */
console_initcall(register_udbg_console);
#endif
