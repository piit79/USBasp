/* Name: usbdrv.c
 * Project: AVR USB driver
 * Author: Christian Starkjohann
 * Creation Date: 2004-12-29
 * Tabsize: 4
 * Copyright: (c) 2005 by OBJECTIVE DEVELOPMENT Software GmbH
 * License: Proprietary, free under certain conditions. See Documentation.
 * This Revision: $Id: usbdrv.c 53 2005-04-12 17:11:29Z cs $
 */

#include <avr/io.h>
#include <avr/pgmspace.h>
#include "usbdrv.h"
#include "oddebug.h"

/*
General Description:
This module implements the C-part of the USB driver. See usbdrv.h for a
documentation of the entire driver.
*/

/* ------------------------------------------------------------------------- */

/* raw USB registers / interface to assembler code: */
/* usbRxBuf MUST be in 1 byte addressable range (because usbInputBuf is only 1 byte) */
static char	usbRxBuf[2][USB_BUFSIZE];/* raw RX buffer: PID, 8 bytes data, 2 bytes CRC */
uchar		usbDeviceId;		/* assigned during enumeration, defaults to 0 */
uchar		usbInputBuf;		/* ptr to raw buffer used for receiving */
uchar		usbAppBuf;			/* ptr to raw buffer passed to app for processing */
volatile char usbRxLen;			/* = 0; number of bytes in usbAppBuf; 0 means free */
uchar		usbCurrentTok;		/* last token received */
uchar		usbRxToken;			/* token for data we received */
uchar		usbMsgLen = 0xff;	/* remaining number of bytes, no msg to send if -1 (see usbMsgPtr) */
volatile char usbTxLen = -1;	/* number of bytes to transmit with next IN token */
uchar		usbTxBuf[USB_BUFSIZE];/* data to transmit with next IN, free if usbTxLen == -1 */
#if USB_CFG_HAVE_INTRIN_ENDPOINT
/* uchar		usbRxEndp;		endpoint which was addressed (1 bit in MSB) [not impl] */
volatile char usbTxLen1 = -1;	/* TX count for endpoint 1 */
uchar		usbTxBuf1[USB_BUFSIZE];/* TX data for endpoint 1 */
#endif
uchar		usbAckBuf[1] = {USBPID_ACK};	/* transmit buffer for ack tokens */
uchar		usbNakBuf[1] = {USBPID_NAK};	/* transmit buffer for nak tokens */

/* USB status registers / not shared with asm code */
uchar			*usbMsgPtr;		/* data to transmit next -- ROM or RAM address */
static uchar	usbMsgFlags;	/* flag values see below */
static uchar	usbNewDeviceId;	/* = 0; device ID which should be set after status phase */
static uchar	usbIsReset;		/* = 0; USB bus is in reset phase */

#define	USB_FLG_TX_PACKET		(1<<0)
/* Leave free 6 bits after TX_PACKET. This way we can increment usbMsgFlags to toggle TX_PACKET */
#define	USB_FLG_MSGPTR_IS_ROM	(1<<6)
#define	USB_FLG_USE_DEFAULT_RW	(1<<7)

/*
optimizing hints:
- do not post/pre inc/dec integer values in operations
- assign value of PRG_RDB() to register variables and don't use side effects in arg
- use narrow scope for variables which should be in X/Y/Z register
- assign char sized expressions to variables to force 8 bit arithmetics
*/

/* ------------------------------------------------------------------------- */

static char	usbDescrDevice[] PROGMEM = {	/* USB device descriptor */
	18,			/* sizeof(usbDescrDevice): length of descriptor in bytes */
	1,			/* descriptor type */
	0x01, 0x01,	/* USB version supported */
	USB_CFG_DEVICE_CLASS,
	USB_CFG_DEVICE_SUBCLASS,
	0,			/* protocol */
	8,			/* max packet size */
	USB_CFG_VENDOR_ID,	/* 2 bytes */
	USB_CFG_DEVICE_ID,	/* 2 bytes */
	USB_CFG_DEVICE_VERSION,	/* 2 bytes */
#if USB_CFG_VENDOR_NAME_LEN
	1,			/* manufacturer string index */
#else
	0,			/* manufacturer string index */
#endif
#if USB_CFG_DEVICE_NAME_LEN
	2,			/* product string index */
#else
	0,			/* product string index */
#endif
	0,			/* serial number string index */
	1,			/* number of configurations */
};

static char	usbDescrConfig[] PROGMEM = {	/* USB configuration descriptor */
	9,			/* sizeof(usbDescrConfig): length of descriptor in bytes */
	2,			/* descriptor type */
	(18 + 7 * USB_CFG_HAVE_INTRIN_ENDPOINT), 0,	/* total length of data returned (including inlined descriptors) */
	1,			/* number of interfaces in this configuration */
	1,			/* index of this configuration */
	0,			/* configuration name string index */
#if USB_CFG_IS_SELF_POWERED
	USBATTR_SELFPOWER,	/* attributes */
#else
	USBATTR_BUSPOWER,	/* attributes */
#endif
	USB_CFG_MAX_BUS_POWER/2,			/* max USB current in 2mA units */
/* interface descriptor follows inline: */
	9,			/* sizeof(usbDescrInterface): length of descriptor in bytes */
	4,			/* descriptor type */
	0,			/* index of this interface */
	0,			/* alternate setting for this interface */
	USB_CFG_HAVE_INTRIN_ENDPOINT,	/* endpoints excl 0: number of endpoint descriptors to follow */
	USB_CFG_INTERFACE_CLASS,
	USB_CFG_INTERFACE_SUBCLASS,
	USB_CFG_INTERFACE_PROTOCOL,
	0,			/* string index for interface */
#if USB_CFG_HAVE_INTRIN_ENDPOINT	/* endpoint descriptor for endpoint 1 */
	7,			/* sizeof(usbDescrEndpoint) */
	5,			/* descriptor type = endpoint */
	0x81,		/* IN endpoint number 1 */
	0x03,		/* attrib: Interrupt endpoint */
	8, 0,		/* maximum packet size */
	USB_CFG_INTR_POLL_INTERVAL,	/* in ms */
#endif
};

static char	usbDescrString0[] PROGMEM = {	/* language descriptor */
	4,			/* sizeof(usbDescrString0): length of descriptor in bytes */
	3,			/* descriptor type */
	0x09, 0x04,	/* language index (0x0409 = US-English) */
};

#if USB_CFG_VENDOR_NAME_LEN
static int	usbDescrString1[] PROGMEM = {
	(2 * USB_CFG_VENDOR_NAME_LEN + 2) | (3<<8),	/* length of descriptor in bytes | descriptor type */
	USB_CFG_VENDOR_NAME
};
#endif
#if USB_CFG_DEVICE_NAME_LEN
static int	usbDescrString2[] PROGMEM = {
	(2 * USB_CFG_DEVICE_NAME_LEN + 2) | (3<<8),	/* length of descriptor in bytes | descriptor type */
	USB_CFG_DEVICE_NAME
};
#endif

/* We don't use prog_int or prog_int16_t for compatibility with various libc
 * versions. Here's an other compatibility hack:
 */
#ifndef PRG_RDB
#define	PRG_RDB(addr)	pgm_read_byte(addr)
#endif

typedef union{
	unsigned	word;
	uchar		*ptr;
	uchar		bytes[2];
}converter_t;
/* We use this union to do type conversions. This is better optimized than
 * type casts in gcc 3.4.3 and much better than using bit shifts to build
 * ints from chars. Byte ordering is not a problem on an 8 bit platform.
 */

/* ------------------------------------------------------------------------- */

#if USB_CFG_HAVE_INTRIN_ENDPOINT
static uchar	usbTxPacketCnt1;

void	usbSetInterrupt(uchar *data, uchar len)
{
uchar		*p, i;
converter_t	crc;

	if(len > 7)
		len = 7;
	i = USBPID_DATA1;
	if(usbTxPacketCnt1 & 1)
		i = USBPID_DATA0;
	if(usbTxLen1 < 0){		/* packet buffer was empty */
		usbTxPacketCnt1++;
	}else{
		usbTxLen1 = -1;		/* avoid sending incomplete interrupt data */
	}
	p = usbTxBuf1;
	*p++ = i;
	for(i=len;i--;)
		*p++ = *data++;
	crc.word = usbCrc16(&usbTxBuf1[1], len);
	usbTxBuf1[len + 1] = crc.bytes[0];
	usbTxBuf1[len + 2] = crc.bytes[1];
	usbTxLen1 = len + 4;	/* len must be given including sync byte */
}
#endif

static void	usbWrite(uchar *data, uchar len)
{
#if USB_CFG_IMPLEMENT_FN_WRITE
	if(!(usbMsgFlags & USB_FLG_USE_DEFAULT_RW)){
		if(usbFunctionWrite(data, len) == 0xff){	/* an error occurred */
			/* usbMsgLen = 0xff; cancel potentially pending ACK [has been done by ASM module when OUT token arrived] */
			usbTxBuf[0] = USBPID_STALL;
			usbTxLen = 2;		/* length including sync byte */
			return;
		}
	}
#endif
	usbMsgLen = 0;		/* send zero-sized block as ACK */
	usbMsgFlags = 0;	/* start with a DATA1 package */
}

static uchar	usbRead(uchar *data, uchar len)
{
#if USB_CFG_IMPLEMENT_FN_READ
	if(usbMsgFlags & USB_FLG_USE_DEFAULT_RW){
#endif
		uchar i = len, *r = usbMsgPtr;
		if(usbMsgFlags & USB_FLG_MSGPTR_IS_ROM){	/* ROM data */
			while(i--){
				char c = PRG_RDB(r);	/* assign to char size variable to enforce byte ops */
				*data++ = c;
				r++;
			}
		}else{					/* RAM data */
			while(i--)
				*data++ = *r++;
		}
		usbMsgPtr = r;
		return len;
#if USB_CFG_IMPLEMENT_FN_READ
	}else{
		if(len != 0)	/* don't bother app with 0 sized reads */
			return usbFunctionRead(data, len);
		return 0;
	}
#endif
}

/* Don't make this function static to avoid inlining.
 * The entire function would become too large and exceed the range of
 * relative jumps.
 */
void	usbProcessRx(uchar *data, uchar len)
{
/* We use if() cascades because the compare is done byte-wise while switch()
 * is int-based. The if() cascades are therefore more efficient.
 */
	DBG2(0x10 + (usbRxToken == (uchar)USBPID_SETUP), data, len);
	if(usbRxToken == (uchar)USBPID_SETUP){
		uchar replyLen = 0, flags = USB_FLG_USE_DEFAULT_RW;
		if(len == 8){	/* Setup size must be always 8 bytes. Ignore otherwise. */
			uchar type = data[0] & (3 << 5);
			if(type == USBRQ_TYPE_STANDARD << 5){
				uchar *replyData = usbTxBuf + 9; /* there is 3 bytes free space at the end of the buffer */
				replyData[0] = 0;
				if(data[1] == 0){/* GET_STATUS */
#if USB_CFG_IS_SELF_POWERED
					uchar recipient = data[0] & 0x1f;	/* assign arith ops to variables to enforce byte size */
					if(recipient == USBRQ_RCPT_DEVICE)
						replyData[0] =  USB_CFG_IS_SELF_POWERED;
#endif
					replyData[1] = 0;
					replyLen = 2;
				}else if(data[1] == 5){		/* SET_ADDRESS */
					usbNewDeviceId = data[2];
				}else if(data[1] == 6){		/* GET_DESCRIPTOR */
					flags = USB_FLG_MSGPTR_IS_ROM | USB_FLG_USE_DEFAULT_RW;
					if(data[3] == 1){		/* descriptor type requested */
						replyLen = sizeof(usbDescrDevice);
						replyData = (uchar *)usbDescrDevice;
					}else if(data[3] == 2){
						replyLen = sizeof(usbDescrConfig);
						replyData = (uchar *)usbDescrConfig;
					}else if(data[3] == 3){	/* string descriptor */
						if(data[2] == 0){	/* descriptor index */
							replyLen = sizeof(usbDescrString0);
							replyData = (uchar *)usbDescrString0;
#if USB_CFG_VENDOR_NAME_LEN
						}else if(data[2] == 1){
							replyLen = sizeof(usbDescrString1);
							replyData = (uchar *)usbDescrString1;
#endif
#if USB_CFG_DEVICE_NAME_LEN
						}else if(data[2] == 2){
							replyLen = sizeof(usbDescrString2);
							replyData = (uchar *)usbDescrString2;
#endif
						}
					}
				}else if(data[1] == 8){		/* GET_CONFIGURATION */
					replyLen = 1;
					replyData[0] = 1;	/* config is always 1, no setConfig required */
				}else if(data[1] == 10){	/* GET_INTERFACE */
					replyLen = 1;
#if USB_CFG_HAVE_INTRIN_ENDPOINT
				}else if(data[1] == 11){	/* SET_INTERFACE */
					usbTxPacketCnt1 = 0;	/* reset data toggling for interrupt socket */
#endif
				}else{
					/* the following requests can be ignored, send default reply */
					/* 1: CLEAR_FEATURE, 3: SET_FEATURE, 7: SET_DESCRIPTOR */
					/* 9: SET_CONFIGURATION, 11: SET_INTERFACE, 12: SYNCH_FRAME */
				}
				usbMsgPtr = replyData;
				if(!data[7] && replyLen > data[6])	/* max length is in data[7]:data[6] */
					replyLen = data[6];
			}else{	/* not a standard request -- must be vendor or class request */
#if USB_CFG_IMPLEMENT_FN_READ || USB_CFG_IMPLEMENT_FN_WRITE
				uchar	len;
				replyLen = data[6];	/* if this is an OUT operation, the next token will reset usbMsgLen */
				if((len = usbFunctionSetup(data)) != 0xff){
					replyLen = len;
				}else{
					flags = 0;	/* we have no valid msg, use read/write functions */
				}
#else
				replyLen = usbFunctionSetup(data);
#endif
			}
		}
		usbMsgLen = replyLen;
		usbMsgFlags = flags;
	}else{	/* out request */
		usbWrite(data, len);
	}
}

/* ------------------------------------------------------------------------- */

static void	usbBuildTxBlock(void)
{
uchar		wantLen, len, txLen, x;
converter_t	crc;

	x = USBPID_DATA1;
	if(usbMsgFlags & USB_FLG_TX_PACKET)
		x = USBPID_DATA0;
	usbMsgFlags++;
	usbTxBuf[0] = x;
	wantLen = usbMsgLen;
	if(wantLen > 8)
		wantLen = 8;
	usbMsgLen -= wantLen;
	len = usbRead(usbTxBuf + 1, wantLen);
	if(len <= 8){	/* valid data packet */
		crc.word = usbCrc16(&usbTxBuf[1], len);
		usbTxBuf[len + 1] = crc.bytes[0];
		usbTxBuf[len + 2] = crc.bytes[1];
		txLen = len + 4;	/* length including sync byte */
		if(len < 8)		/* a partial package identifies end of message */
			usbMsgLen = 0xff;
	}else{
		usbTxBuf[0] = USBPID_STALL;
		txLen = 2;	/* length including sync byte */
		usbMsgLen = 0xff;
	}
	usbTxLen = txLen;
	DBG2(0x20, usbTxBuf, txLen-1);
}

static inline uchar	isNotSE0(void)
{
uchar	rval;
/* We want to do
 *     return (USBIN & USBMASK);
 * here, but the compiler does int-expansion acrobatics.
 * We can avoid this by assigning to a char-sized variable.
 */
	rval = USBIN & USBMASK;
	return rval;
}

/* ------------------------------------------------------------------------- */

void	usbPoll(void)
{
uchar	len;

	if((len = usbRxLen) > 0){
/* We could check CRC16 here -- but ACK has already been sent anyway. If you
 * need data integrity checks with this driver, check the CRC in your app
 * code and report errors back to the host. Since the ACK was already sent,
 * retries must be handled on application level.
 * unsigned crc = usbCrc16((uchar *)(unsigned)(usbAppBuf + 1), usbRxLen - 3);
 */
		len -= 3;	/* remove PID and CRC */
		if(len < 128){
			usbProcessRx((uchar *)(unsigned)(usbAppBuf + 1), len);
		}
		usbRxLen = 0;	/* mark rx buffer as available */
	}
	if(usbTxLen < 0){	/* TX system is idle */
		if(usbMsgLen != 0xff){
			usbBuildTxBlock();
		}else if(usbNewDeviceId){
			usbDeviceId = usbNewDeviceId;
			DBG1(1, &usbNewDeviceId, 1);
			usbNewDeviceId = 0;
		}
	}
	if(isNotSE0()){	/* SE0 state */
		usbIsReset = 0;
	}else{
		/* check whether SE0 lasts for more than 2.5us (3.75 bit times) */
		if(!usbIsReset){
			uchar i;
			for(i=100;i;i--){
				if(isNotSE0())
					goto notUsbReset;
			}
			usbIsReset = 1;
			usbDeviceId = 0;
			usbNewDeviceId = 0;
			DBG1(0xff, 0, 0);
notUsbReset:;
		}
	}
}

/* ------------------------------------------------------------------------- */

void	usbInit(void)
{
	usbInputBuf = (uchar)usbRxBuf[0];
	usbAppBuf = (uchar)usbRxBuf[1];
#if USB_INTR_CFG_SET != 0
	USB_INTR_CFG |= USB_INTR_CFG_SET;
#endif
#if USB_INTR_CFG_CLR != 0
	USB_INTR_CFG &= ~(USB_INTR_CFG_CLR);
#endif
	USB_INTR_ENABLE |= (1 << USB_INTR_ENABLE_BIT);
}

/* ------------------------------------------------------------------------- */
