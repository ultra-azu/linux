// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2012 ARM Ltd.
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 *
 * Adapted for ARM and earlycon:
 * Copyright (C) 2014 Linaro Ltd.
 * Author: Rob Herring <robh@kernel.org>
 */
#include <linux/kernel.h>
#include <linux/console.h>
#include <linux/io.h>
#include <linux/init.h>
#include <linux/serial_core.h>

char* ramoops_mem_base;
unsigned int ramoops_mem_len;
static unsigned int ramoops_offset;

static void ramoops_putc(struct uart_port *port, int c)
{
	if (!ramoops_mem_base || !ramoops_mem_len)
		return;
	if (ramoops_offset >= ramoops_mem_len)
		ramoops_offset = 0;

	ramoops_mem_base[ramoops_offset++] = (char) c;
}

static void ramoops_write(struct console *con, const char *s, unsigned n)
{
	struct earlycon_device *dev = con->data;
	uart_console_write(&dev->port, s, n, ramoops_putc);
}

static int
__init ramoops_setup(struct earlycon_device *device, const char *opt)
{
	device->con->write = ramoops_write;
	return 0;
}
EARLYCON_DECLARE(ramoops, ramoops_setup);
