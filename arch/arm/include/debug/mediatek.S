/*
 * Low-level debug macro for Mediatek SoCs
 *
 * Copyright (c) 2014 MundoReader S.L.
 * Author: Matthias Brugger <matthias.bgg@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/serial_reg.h>

	        .macro  addruart, rp, rv, tmp
	        ldr	\rp, =CONFIG_DEBUG_UART_PHYS
	        ldr	\rv, =CONFIG_DEBUG_UART_VIRT
	        .endm

	        .macro  senduart,rd,rx
	        strb    \rd, [\rx, #0x0]
	        .endm

	        .macro  waituart,rd,rx
1001:   	ldr \rd, [\rx, #0x14]
	        tst \rd, #0x40
	        beq 1001b
	        .endm

		.macro  busyuart,rd,rx
1002:   	ldr \rd, [\rx, #0x14]
	        tst \rd, #0x40
	        beq 1002b
	        .endm
