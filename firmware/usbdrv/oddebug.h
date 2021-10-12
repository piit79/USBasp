/* Name: oddebug.h
 * Project: AVR library
 * Author: Christian Starkjohann
 * Creation Date: 2005-01-16
 * Tabsize: 4
 * Copyright: (c) 2005 by OBJECTIVE DEVELOPMENT Software GmbH
 * License: Proprietary, free under certain conditions. See Documentation.
 * This Revision: $Id: oddebug.h 52 2005-04-12 16:57:29Z cs $
 */

#ifndef __oddebug_h_included__
#define __oddebug_h_included__

#include <avr/io.h>

/*
General Description:
This module implements a function for debug logs on the serial line of the
AVR microcontroller. Debugging can be configured with the define
'DEBUG_LEVEL'. If this macro is not defined or defined to 0, all debugging
calls are no-ops. If it is 1, DBG1 logs will appear, but not DBG2. If it is
2, DBG1 and DBG2 logs will be printed.

A debug log consists of a label ('prefix') to indicate which debug log created
the output and a memory block to dump in hex ('data' and 'len').
*/


#ifndef F_CPU
#	define	F_CPU	12000000	/* 12 MHz */
#endif

#ifndef uchar
#	define	uchar	unsigned char
#endif

#if DEBUG_LEVEL > 0 && !defined TXEN	/* no UART in device */
#	warning "Debugging disabled because device has no UART"
#	undef	DEBUG_LEVEL
#endif

#ifndef DEBUG_LEVEL
#	define	DEBUG_LEVEL	0
#endif

/* ------------------------------------------------------------------------- */

#if DEBUG_LEVEL > 0
#	define	DBG1(prefix, data, len)	odDebug(prefix, data, len)
#else
#	define	DBG1(prefix, data, len)
#endif

#if DEBUG_LEVEL > 1
#	define	DBG2(prefix, data, len)	odDebug(prefix, data, len)
#else
#	define	DBG2(prefix, data, len)
#endif

/* ------------------------------------------------------------------------- */

#if	DEBUG_LEVEL > 0
extern void	odDebug(uchar prefix, uchar *data, uchar len);

/* Try to find our control registers; ATMEL likes to rename these */

#if defined UBRR
#	define	ODDBG_UBRR	UBRR
#elif defined UBRRL
#	define	ODDBG_UBRR	UBRRL
#elif defined UBRR1
#	define	ODDBG_UBRR	UBRR1
#elif defined UBRR1L
#	define	ODDBG_UBRR	UBRR1L
#endif

#if defined UCR
#	define	ODDBG_UCR	UCR
#elif defined UCSRB
#	define	ODDBG_UCR	UCSRB
#elif defined UCSR1B
#	define	ODDBG_UCR	UCSR1B
#endif

#if defined	USR
#	define	ODDBG_USR	USR
#elif defined UCSRA
#	define	ODDBG_USR	UCSRA
#elif defined UCSR1A
#	define	ODDBG_USR	UCSR1A
#endif

#if defined UDR
#	define	ODDBG_UDR	UDR
#elif defined UDR1
#	define	ODDBG_UDR	UDR1
#endif

static inline void	odDebugInit(void)
{
	ODDBG_UCR |= (1<<TXEN);
	ODDBG_UBRR = F_CPU / (19200 * 16L) - 1;
}
#else
#	define odDebugInit()
#endif

/* ------------------------------------------------------------------------- */

#endif /* __oddebug_h_included__ */
