/* Name: oddebug.c
 * Project: AVR library
 * Author: Christian Starkjohann
 * Creation Date: 2005-01-16
 * Tabsize: 4
 * Copyright: (c) 2005 by OBJECTIVE DEVELOPMENT Software GmbH
 * License: Proprietary, free under certain conditions. See Documentation.
 * This Revision: $Id: oddebug.c 52 2005-04-12 16:57:29Z cs $
 */

#include <avr/io.h>
#include "oddebug.h"

#if	DEBUG_LEVEL > 0

static void	uartPutc(char c)
{
	while(!(ODDBG_USR & (1 << UDRE)));	/* wait for data register empty */
	ODDBG_UDR = c;
}

static uchar	hexAscii(uchar h)
{
	h &= 0xf;
	if(h < 10){
		h += '0';
	}else{
		h += 'a' - (uchar)10;
	}
	return h;
}

static void	printHex(uchar c)
{
	uartPutc(hexAscii(c >> 4));
	uartPutc(hexAscii(c));
}

void	odDebug(uchar prefix, uchar *data, uchar len)
{
	printHex(prefix);
	uartPutc(':');
	while(len--){
		uartPutc(' ');
		printHex(*data++);
	}
	uartPutc('\r');
	uartPutc('\n');
}

#endif
