/* Name: usbdrv.h
 * Project: AVR USB driver
 * Author: Christian Starkjohann
 * Creation Date: 2004-12-29
 * Tabsize: 4
 * Copyright: (c) 2005 by OBJECTIVE DEVELOPMENT Software GmbH
 * License: Proprietary, free under certain conditions. See Documentation.
 * This Revision: $Id: usbdrv.h 52 2005-04-12 16:57:29Z cs $
 */

#ifndef __usbdrv_h_included__
#define	__usbdrv_h_included__
#include "usbconfig.h"

/*
Hardware Prerequisites:
=======================
USB lines D+ and D- MUST be wired to the same I/O port. Line D- MUST be wired
to bit number 0. D+ must also be connected to INT0. D- requires a pullup of
1.5k to +3.5V (and the device must be powered at 3.5V) to identify as
low-speed USB device. A pullup of 1M SHOULD be connected from D+ to +3.5V to
prevent interference when no USB master is connected. We use D+ as interrupt
source and not D- because it does not trigger on keep-alive and RESET states.

Please adapt the values in usbconfig.h according to your hardware!

The device MUST be clocked at 12 MHz. This is more than the 10 MHz allowed by
an AT90S2313 powered at 4.5V. However, if the supply voltage to maximum clock
relation is interpolated linearly, an ATtiny2313 meets the requirement by
specification. In practice, the AT90S2313 can be overclocked and works well.


Limitations:
============
Compiling:
The bss segment of the driver must be in the first 256 bytes of the address
space because byte wide variables are used as pointers for efficiency reasons.
This is not a problem on devices with 128 byte RAM since the entire RAM
meets this condition. For larger devices please link usbdrv first.

Robustness with respect to communication errors:
The driver assumes error-free communication. It DOES check for errors in
the PID, but does NOT check bit stuffing errors, SE0 in middle of a byte,
token CRC (5 bit) and data CRC (16 bit). CRC checks can not be performed due
to timing constraints: We must start sending a reply within 7 bit times. 
Bit stuffing and misplaced SE0 would have to be checked in real-time, but CPU
performance does not permit that. The driver does not check Data0/Data1
toggling, but application software can implement the check.

Sampling jitter:
The driver guarantees a sampling window of 1/2 bit. The USB spec requires
that the receiver has at most 1/4 bit sampling window. The 1/2 bit window
should still work reliably enough because we work at low speed. If you want
to meet the spec, define the macro "USB_CFG_SAMPLE_EXACT" to 1 in usbconfig.h.
This will unroll a loop which results in bigger code size.

Input characteristics:
Since no differential receiver circuit is used, electrical interference
robustness may suffer. The driver samples only one of the data lines with
an ordinary I/O pin's input characteristics. However, since this is only a
low speed USB implementation and the specification allows for 8 times the
bit rate over the same hardware, we should be on the safe side. Even the spec
requires detection of asymmetric states at high bit rate for SE0 detection.

Number of endpoints:
The driver supports up to two endpoints: One control endpoint (endpoint 0) and
one interrupt-in endpoint (endpoint 1) where the device can send interrupt
data to the host. Endpoint 1 is only compiled in if
USB_CFG_HAVE_INTRIN_ENDPOINT is defined to 1 in usbconfig.h.

Maximum data payload:
Data payload of control in and out transfers may be up to 255 bytes. In order
to accept payload data of out transfers, you need to implement
'usbFunctionWrite()'.

USB Suspend Mode supply current:
The USB standard limits power consumption to 500uA when the bus is in suspend
mode. This is not a problem for self-powered devices since they don't need
bus power anyway. Bus-powered devices can achieve this only by putting the
CPU in sleep mode. The driver does not implement suspend handling by itself.
However, the application may implement activity monitoring and wakeup from
sleep. The host sends regular SE0 states on the bus to keep it active. These
SE0 states can be detected by wiring the INT1 pin to D+. It is not necessary
to enable the interrupt, checking the interrupt pending flag should suffice.
Before entering sleep mode, the application should enable INT1 for a wakeup
on the next bus activity.

Operation without an USB master:
The driver behaves neutral without connection to an USB master if D- reads
as 1. To avoid spurious interrupts, we recommend a high impedance (e.g. 1M)
pullup resistor on D+. If D- becomes statically 0, the driver may block in
the interrupt routine.

Interrupt latency:
The application must ensure that the USB interrupt is not disabled for more
than 20 cycles.

Maximum interrupt duration / CPU cycle consumption:
The driver handles all USB communication during the interrupt service
routine. The routine will not return before an entire USB message is received
and the reply is sent. This may be up to ca. 1200 cycles = 100us if the host
conforms to the standard. The driver will consume CPU cycles for all USB
messages, even if they address an other (low-speed) device on the same bus.

*/

/* ------------------------------------------------------------------------- */
/* --------------------------- Module Interface ---------------------------- */
/* ------------------------------------------------------------------------- */

#ifndef __ASSEMBLER__

#ifndef uchar
#define	uchar	unsigned char
#endif

#if USB_CFG_HAVE_INTRIN_ENDPOINT
void	usbSetInterrupt(uchar *data, uchar len);
/* This function sets the message which will be sent during the next interrupt
 * IN transfer. The message is copied to an internal buffer and must not exceed
 * a length of 7 bytes. The message may be 0 bytes long just to indicate the
 * interrupt status to the host.
 * If you need to transfer more bytes, use a control read after the interrupt.
 */
#endif /* USB_CFG_HAVE_INTRIN_ENDPOINT */

extern void		usbInit(void);
/* This function must be called before interrupts are enabled and the main
 * loop is entered.
 */
extern void		usbPoll(void);
/* This function must be called at regular intervals from the main loop.
 * Maximum delay between calls is somewhat less than 50ms (USB timeout between
 * packages of a message).
 */
extern uchar	*usbMsgPtr;
/* This variable may be used to pass transmit data to the driver from the
 * implementation of usbFunctionWrite(). It is also used internally by the
 * driver for standard control requests.
 */ 
extern uchar	usbFunctionSetup(uchar data[8]);
/* This function is called for all setup requests which are not of type
 * "Standard" (in practice: class and vendor requests). The 8 bytes setup
 * data is passed in 'data'. Data for control-out transfers is passed to the
 * application in separate calls to usbFunctionWrite() (unless you have turned
 * this option off). You should store the setup context in global/static
 * variables to have it available in usbFunctionWrite(). Data for control-in
 * transfers can be provided in two ways: (1) immediately as a result of
 * usbFunctionSetup() or (2) on demand of the driver in calls to the separate
 * function usbFunctionRead() (if enabled). For (1) write the data to a static
 * buffer, set the global variable 'usbMsgPtr' to this buffer and return the
 * data length (may be 0). To implement (2), simply return 0xff (== -1) in
 * usbFunctionSetup(). The driver will call usbFunctionRead() when data is
 * needed. You may use 'usbMsgPtr' to save your own status in this case.
 * The data passed in 'data' has the following content (see USB 1.1 spec):
 *	struct usbControlData{
 *		uchar		requestType;	//[0]
 *		uchar		request;		//[1]
 *		unsigned	value;			//[2], [3]
 *		unsigned	index;			//[4], [5]
 *		unsigned	length;			//[6], [7]
 *	};
 */
#if USB_CFG_IMPLEMENT_FN_WRITE
extern uchar	usbFunctionWrite(uchar *data, uchar len);
/* This function is called by the driver to provide a control transfer's
 * payload data (control-out). It is called in chunks of up to 8 bytes. The
 * total count provided in the current control transfer can be obtained from
 * the 'length' property in the setup data. If an error occurred during
 * processing, return 0xff (== -1). The driver will answer the entire transfer
 * with a STALL token in this case. Otherwise return any number which is not
 * 0xff. NOTE: Only the return value of the LAST usbFunctionWrite() call
 * (the one immediately before the status phase) is used.
 */
#endif /* USB_CFG_IMPLEMENT_FN_WRITE */
#if USB_CFG_IMPLEMENT_FN_READ
extern uchar usbFunctionRead(uchar *data, uchar len);
/* This function is called by the driver to ask the application for a control
 * transfer's payload data (control-in). You should supply up to 'len' bytes of
 * data in this chunk. 'len' will be 8 bytes for all but the last chunk. If
 * you return less than 8 bytes, the control transfer ends. If you return an
 * invalid value (e.g. -1), the driver sends a STALL token.
 */
#endif /* USB_CFG_IMPLEMENT_FN_READ */
extern unsigned	usbCrc16(uchar *data, uchar len);
/* This function calculates the binary complement of the data CRC used in
 * USB data packets. The value is used to build raw transmit packets.
 * You may want to use this function for data checksums.
 */

#endif	/* __ASSEMBLER__ */

/* ------------------------------------------------------------------------- */
/* ------------------------- Constant definitions -------------------------- */
/* ------------------------------------------------------------------------- */

/* I/O definitions for assembler module */
#define	USBOUT		USB_CFG_IOPORT			/* output port for USB bits */
#ifdef __ASSEMBLER__
#define	USBIN		(USB_CFG_IOPORT - 2)	/* input port for USB bits */
#define	USBDDR		(USB_CFG_IOPORT - 1)	/* data direction for USB bits */
#else
#define	USBIN		(*(&USB_CFG_IOPORT - 2))	/* input port for USB bits */
#define	USBDDR		(*(&USB_CFG_IOPORT - 1))	/* data direction for USB bits */
#endif
#if USB_CFG_DMINUS_BIT != 0
#	error "USB_CFG_DMINUS_BIT MUST be 0!"
#endif
#define	USBMINUS	0		/* D- MUST be on bit 0 */
#define	USBIDLE		0x01	/* value representing J state */
#define	USBMASK		((1<<USB_CFG_DPLUS_BIT) | 1)	/* mask for USB I/O bits */

#define	USB_BUFSIZE		11	/* PID, 8 bytes data, 2 bytes CRC */

/* Try to find registers and bits responsible for ext interrupt 0 */

#if defined EICRA
#	define	USB_INTR_CFG	EICRA
#else
#	define	USB_INTR_CFG	MCUCR
#endif
#define	USB_INTR_CFG_SET	((1 << ISC00) | (1 << ISC01))	/* cfg for rising edge */
#define	USB_INTR_CFG_CLR	0	/* no bits to clear */

#if defined GIMSK
#	define	USB_INTR_ENABLE		GIMSK
#elif defined EIMSK
#	define	USB_INTR_ENABLE		EIMSK
#else
#	define	USB_INTR_ENABLE		GICR
#endif
#define	USB_INTR_ENABLE_BIT		INT0

#if defined EIFR
#	define	USB_INTR_PENDING	EIFR
#else
#	define	USB_INTR_PENDING	GIFR
#endif
#define	USB_INTR_PENDING_BIT	INTF0

/*
The defines above don't work for the following chips
at90c8534: no ISC0?, no PORTB, can't find a data sheet
at86rf401: no PORTB, no MCUCR etc
atmega103: no ISC0? (maybe omission in header, can't find data sheet)
atmega603: not defined in avr-libc
at43usb320, at43usb355, at76c711: have USB anyway
at94k: is different...

at90s1200, attiny11, attiny12, attiny15, attiny28: these have no RAM
*/

/* ------------------------------------------------------------------------- */
/* ---------------------- USB Specification Constants ---------------------- */
/* ------------------------------------------------------------------------- */

/* USB Token values */
#define	USBPID_SETUP	0x2d
#define	USBPID_OUT		0xe1
#define	USBPID_IN		0x69
#define	USBPID_DATA0	0xc3
#define	USBPID_DATA1	0x4b

#define	USBPID_ACK		0xd2
#define	USBPID_NAK		0x5a
#define	USBPID_STALL	0x1e

/* USB descriptor constants */
#define	USBATTR_BUSPOWER	0x80
#define	USBATTR_SELFPOWER	0x40
#define	USBATTR_REMOTEWAKE	0x20

/* USB setup recipient values */
#define	USBRQ_RCPT_DEVICE		0
#define	USBRQ_RCPT_INTERFACE	1
#define	USBRQ_RCPT_ENDPOINT		2

/* USB request type values */
#define	USBRQ_TYPE_STANDARD		0
#define	USBRQ_TYPE_CLASS		1
#define	USBRQ_TYPE_VENDOR		2


/* ------------------------------------------------------------------------- */

#endif /* __usbdrv_h_included__ */
