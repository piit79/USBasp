/* Name: usbdrvasm.S
 * Project: AVR USB driver
 * Author: Christian Starkjohann
 * Creation Date: 2004-12-29
 * Tabsize: 4
 * Copyright: (c) 2005 by OBJECTIVE DEVELOPMENT Software GmbH
 * License: Proprietary, free under certain conditions. See Documentation.
 * This Revision: $Id: usbdrvasm.S 52 2005-04-12 16:57:29Z cs $
 */

/*
General Description:
This module implements the assembler part of the USB driver. See usbdrv.h
for a description of the entire driver.
Since almost all of this code is timing critical, don't change unless you
really know what you are doing! Many parts require not only a maximum number
of CPU cycles, but even an exact number of cycles!
*/

/* configs for io.h */
#define __SFR_OFFSET 0
#define _VECTOR(N) __vector_ ## N	/* io.h does not define this for asm */

#include <avr/io.h>	/* for CPU I/O register definitions and vectors */
#include "usbdrv.h"	/* for common defs */


/* register names */
#define	x1		r16
#define	x2		r17
#define	shift	r18
#define	cnt		r19
#define	x3		r20
#define	x4		r21

#define	nop2	rjmp	.+0	/* jump to next instruction */

.text

.global	SIG_INTERRUPT0
	.type	SIG_INTERRUPT0, @function
SIG_INTERRUPT0:
;Software-receiver engine. Strict timing! Don't change unless you can preserve timing!
;interrupt response time: 4 cycles + insn running = 7 max if interrupts always enabled
;max allowable interrupt latency: 32 cycles -> max 25 cycles interrupt disable
;max stack usage: [ret(2), x1, SREG, x2, cnt, shift, YH, YL, x3, x4] = 11 bytes
usbInterrupt:
;order of registers pushed:
;x1, SREG, x2, cnt, shift, [YH, YL, x3]
	push	x1				;2  push only what is necessary to sync with edge ASAP
	in		x1, SREG		;1
	push	x1				;2
;sync byte (D-) pattern LSb to MSb: 01010100 [1 = idle = J, 0 = K]
;sync up with J to K edge during sync pattern -- use fastest possible loops
;first part has no timeout because it waits for IDLE or SE1 (== disconnected)
#if !USB_CFG_SAMPLE_EXACT
	ldi		x1, 5			;1 setup a timeout for waitForK
#endif
waitForJ:
	sbis	USBIN, USBMINUS	;1 wait for D- == 1
	rjmp	waitForJ		;2
#if USB_CFG_SAMPLE_EXACT
;The following code represents the unrolled loop in the else branch. It
;results in a sampling window of 1/4 bit which meets the spec.
	sbis	USBIN, USBMINUS
	rjmp	foundK
	sbis	USBIN, USBMINUS
	rjmp	foundK
	sbis	USBIN, USBMINUS
	rjmp	foundK
	nop
	nop2
foundK:
#else
waitForK:
	dec		x1				;1
	sbic	USBIN, USBMINUS	;1 wait for D- == 0
	brne	waitForK		;2
#endif
;{2, 6} after falling D- edge, average delay: 4 cycles [we want 4 for center sampling]
;we have 1 bit time for setup purposes, then sample again:
	push	x2				;2
	push	cnt				;2
	push	shift			;2
shortcutEntry:
	ldi		cnt, 1			;1 pre-init bit counter (-1 because no dec follows, -1 because 1 bit already sampled)
	ldi		x2, 1<<USB_CFG_DPLUS_BIT	;1 -> 8   edge sync ended with D- == 0
;now wait until SYNC byte is over. Wait for either 2 bits low (success) or 2 bits high (failure)
waitNoChange:
	in		x1, USBIN		;1 <-- sample, timing: edge + {2, 6} cycles
	eor		x2, x1			;1
	sbrc	x2, 0			;1 | 2
	ldi		cnt, 2			;1 | 0 cnt = numBits - 1 (because dec follows)
	mov		x2, x1			;1
	dec		cnt				;1
	brne	waitNoChange	;2 | 1
	sbrc	x1, USBMINUS	;2
	rjmp	sofError		;0 two consecutive "1" bits -> framing error
;start reading data, but don't check for bitstuffing because these are the
;first bits. Use the cycles for initialization instead. Note that we read and
;store the binary complement of the data stream because eor results in 1 for
;a change and 0 for no change.
	in		x1, USBIN		;1 <-- sample bit 0, timing: edge + {3, 7} cycles
	eor		x2, x1			;1
	ror		x2				;1
	ldi		shift, 0x7f		;1 The last bit of the sync pattern was a "no change"
	ror		shift			;1
	push	YH				;2 -> 7
	in		x2, USBIN		;1 <-- sample bit 1, timing: edge + {2, 6} cycles
	eor		x1, x2			;1
	ror		x1				;1
	ror		shift			;1
	push	YL				;2
	lds		YL, usbInputBuf	;2 -> 8
	in		x1, USBIN		;1 <-- sample bit 2, timing: edge + {2, 6} cycles
	eor		x2, x1			;1
	ror		x2				;1
	ror		shift			;1
	ldi		cnt, USB_BUFSIZE;1
	clr		YH				;1
	push	x3				;2 -> 8
	in		x2, USBIN		;1 <-- sample bit 3, timing: edge + {2, 6} cycles
	eor		x1, x2			;1
	ror		x1				;1
	ror		shift			;1
	ser		x3				;1
	nop						;1
	rjmp	rxbit4			;2 -> 8

shortcutToStart:			;{,43} into next frame: max 5.5 sync bits missed
#if !USB_CFG_SAMPLE_EXACT
	ldi		x1, 5			;2 setup timeout
#endif
waitForJ1:
	sbis	USBIN, USBMINUS	;1 wait for D- == 1
	rjmp	waitForJ1		;2
#if USB_CFG_SAMPLE_EXACT
;The following code represents the unrolled loop in the else branch. It
;results in a sampling window of 1/4 bit which meets the spec.
	sbis	USBIN, USBMINUS
	rjmp	foundK1
	sbis	USBIN, USBMINUS
	rjmp	foundK1
	sbis	USBIN, USBMINUS
	rjmp	foundK1
	nop
	nop2
foundK1:
#else
waitForK1:
	dec		x1				;1
	sbic	USBIN, USBMINUS	;1 wait for D- == 0
	brne	waitForK1		;2
#endif
	pop		YH				;2 correct stack alignment
	nop2					;2 delay for the same time as the pushes in the original code
	rjmp	shortcutEntry	;2

; ################# receiver loop #################
; extra jobs done during bit interval:
; bit 6:	se0 check
; bit 7:	or, store, clear
; bit 0:	recover from delay	[SE0 is unreliable here due to bit dribbling in hubs]
; bit 1:	se0 check
; bit 2:	se0 check
; bit 3:	overflow check
; bit 4:	se0 check
; bit 5:	rjmp

; stuffed* helpers have the functionality of a subroutine, but we can't afford
; the overhead of a call. We therefore need a separate routine for each caller
; which jumps back appropriately.

stuffed5:				;1 for branch taken
	in		x2, USBIN	;1 <-- sample @ +1
	andi	x2, USBMASK	;1
	breq	se0a		;1
	andi	x3, 0xc0	;1 (0xff03 >> 2) & 0xff
	ori		shift, 0xfc	;1
	rjmp	rxbit6		;2

stuffed6:				;1 for branch taken
	in		x1, USBIN	;1 <-- sample @ +1
	andi	x1, USBMASK	;1
	breq	se0a		;1
	andi	x3, 0x81	;1 (0xff03 >> 1) & 0xff
	ori		shift, 0xfc	;1
	rjmp	rxbit7		;2

; This is somewhat special because it has to compensate for the delay in bit 7
stuffed7:				;1 for branch taken
	andi	x1, USBMASK	;1 already sampled by caller
	breq	se0a		;1
	mov		x2, x1		;1 ensure correct NRZI sequence [we can save andi x3 here]
	ori		shift, 0xfc	;1
	in		x1, USBIN	;1 <-- sample bit 0
	rjmp	unstuffed7	;2

stuffed0:				;1 for branch taken
	in		x1, USBIN	;1 <-- sample @ +1
	andi	x1, USBMASK	;1
	breq	se0a		;1
	andi	x3, 0xfe	;1 (0xff03 >> 7) & 0xff
	ori		shift, 0xfc	;1
	rjmp	rxbit1		;2

;-----------------------------
rxLoop:
	brlo	stuffed5	;1
rxbit6:
	in		x1, USBIN	;1 <-- sample bit 6
	andi	x1, USBMASK	;1
	breq	se0a		;1
	eor		x2, x1		;1
	ror		x2			;1
	ror		shift		;1
	cpi		shift, 4	;1
	brlo	stuffed6	;1
rxbit7:
	in		x2, USBIN	;1 <-- sample bit 7
	eor		x1, x2		;1
	ror		x1			;1
	ror		shift		;1
	eor		x3,	shift	;1 x3 is 0 at bit locations we changed, 1 at others
	st		y+, x3		;2 the eor above reconstructed modified bits and inverted rx data
	ser		x3			;1
rxbit0:
	in		x1, USBIN	;1 <-- sample bit 0
	cpi		shift, 4	;1
	brlo	stuffed7	;1
unstuffed7:
	eor		x2, x1		;1
	ror		x2			;1
	ror		shift		;1
	cpi		shift, 4	;1
	brlo	stuffed0	;1
rxbit1:
	in		x2, USBIN	;1 <-- sample bit 1
	andi	x2, USBMASK	;1
se0a:					; enlarge jump range to SE0
	breq	se0			;1 check for SE0 more often close to start of byte
	eor		x1, x2		;1
	ror		x1			;1
	ror		shift		;1
	cpi		shift, 4	;1
	brlo	stuffed1	;1
rxbit2:
	in		x1, USBIN	;1 <-- sample bit 2
	andi	x1, USBMASK	;1
	breq	se0			;1
	eor		x2, x1		;1
	ror		x2			;1
	ror		shift		;1
	cpi		shift, 4	;1
	brlo	stuffed2	;1
rxbit3:
	in		x2, USBIN	;1 <-- sample bit 3
	eor		x1, x2		;1
	ror		x1			;1
	ror		shift		;1
	dec		cnt			;1	check for buffer overflow
	breq	overflow	;1
	cpi		shift, 4	;1
	brlo	stuffed3	;1
rxbit4:
	in		x1, USBIN	;1 <-- sample bit 4
	andi	x1, USBMASK	;1
	breq	se0			;1
	eor		x2, x1		;1
	ror		x2			;1
	ror		shift		;1
	cpi		shift, 4	;1
	brlo	stuffed4	;1
rxbit5:
	in		x2, USBIN	;1 <-- sample bit 5
	eor		x1, x2		;1
	ror		x1			;1
	ror		shift		;1
	cpi		shift, 4	;1
	rjmp	rxLoop		;2
;-----------------------------

stuffed1:				;1 for branch taken
	in		x2, USBIN	;1 <-- sample @ +1
	andi	x2, USBMASK	;1
	breq	se0			;1
	andi	x3, 0xfc	;1 (0xff03 >> 6) & 0xff
	ori		shift, 0xfc	;1
	rjmp	rxbit2		;2

stuffed2:				;1 for branch taken
	in		x1, USBIN	;1 <-- sample @ +1
	andi	x1, USBMASK	;1
	breq	se0			;1
	andi	x3, 0xf8	;1 (0xff03 >> 5) & 0xff
	ori		shift, 0xfc	;1
	rjmp	rxbit3		;2

stuffed3:				;1 for branch taken
	in		x2, USBIN	;1 <-- sample @ +1
	andi	x2, USBMASK	;1
	breq	se0			;1
	andi	x3, 0xf0	;1 (0xff03 >> 4) & 0xff
	ori		shift, 0xfc	;1
	rjmp	rxbit4		;2

stuffed4:				;1 for branch taken
	in		x1, USBIN	;1 <-- sample @ +1
	andi	x1, USBMASK	;1
	breq	se0			;1
	andi	x3, 0xe0	;1 (0xff03 >> 3) & 0xff
	ori		shift, 0xfc	;1
	rjmp	rxbit5		;2

;################ end receiver loop ###############

overflow:					; ignore package if buffer overflow
	rjmp	rxDoReturn		; enlarge jump range

;This is the only non-error exit point for the software receiver loop
;{4, 20} cycles after start of SE0, typically {10, 18} after SE0 start = {-6, 2} from end of SE0
;next sync starts {16,} cycles after SE0 -> worst case start: +4 from next sync start
;we don't check any CRCs here because there is no time left.
se0:							;{-6, 2} from end of SE0 / {,4} into next frame
	mov		cnt, YL				;1 assume buffer in lower 256 bytes of memory
	lds		YL, usbInputBuf		;2 reposition to buffer start
	sub		cnt, YL				;1 length of message
	ldi		x1, 1<<USB_INTR_PENDING_BIT	;1
	cpi		cnt, 3				;1
	out		USB_INTR_PENDING, x1;1 clear pending intr and check flag later. SE0 must be over. {,10} into next frame
	brlo	rxDoReturn			;1 ensure valid packet size, ignore others
	ld		x1, y				;2 PID
	ldd		x2, y+1				;2 ADDR + 1 bit endpoint number
	mov		x3, x2				;1 store for endpoint number
	andi	x2, 0x7f			;1 mask endpoint number bit
	lds		shift, usbDeviceId	;2
	cpi		x1, USBPID_SETUP	;1
	breq	isSetupOrOut		;2 -> 19 = {13, 21} from SE0 end
	cpi		x1, USBPID_OUT		;1
	breq	isSetupOrOut		;2 -> 22 = {16, 24} from SE0 end / {,24} into next frame
	cpi		x1, USBPID_IN		;1
	breq	handleIn			;1
	cpi		x1, USBPID_DATA0	;1
	breq	isData				;1
	cpi		x1, USBPID_DATA1	;1
	brne	rxDoReturn			;1 ignore all other PIDs
isData:
	lds		x2, usbCurrentTok	;2
	tst		x2					;1
	breq	rxDoReturn			;1 for other device or spontaneous data -- ignore
	lds		x1, usbRxLen		;2
	cpi		x1, 0				;1
	brne	sendNakAndReti		;1 no buffer space available / {30, 38} from SE0 end
	sts		usbRxLen, cnt		;2 store received data, swap buffers
	sts		usbRxToken, x2		;2
	lds		x1, usbAppBuf		;2
	sts		usbAppBuf, YL		;2
	sts		usbInputBuf, x1		;2 buffers now swapped
	rjmp	sendAckAndReti		;2 -> {42, 50} from SE0 end

handleIn:						; {18, 26} from SE0 end
	cp		x2, shift			;1 shift contains our device ID
	brne	rxDoReturn			;1 other device
#if USB_CFG_HAVE_INTRIN_ENDPOINT
	sbrc	x3, 7				;2
	rjmp	handleIn1			;0
#endif
	lds		cnt, usbTxLen		;2
	cpi		cnt, -1				;1
	breq	sendNakAndReti		;1 -> {27, 35} from SE0 end
	ldi		x1, -1				;1
	sts		usbTxLen, x1		;2 buffer is now free
	ldi		YL, lo8(usbTxBuf)	;1
	ldi		YH, hi8(usbTxBuf)	;1
	rjmp	usbSendAndReti		;2 -> {34, 43} from SE0 end

; Comment about when to set usbTxLen to -1:
; We should set it back to -1 when we receive the ACK from the host. This would
; be simple to implement: One static variable which stores whether the last
; tx was for endpoint 0 or 1 and a compare in the receiver to distinguish the
; ACK. However, we set it back to -1 immediately when we send the package,
; assuming that no error occurs and the host sends an ACK. We save one byte
; RAM this way and avoid potential problems with endless retries. The rest of
; the driver assumes error-free transfers anyway.

otherOutOrSetup:
	clr		x1
	sts		usbCurrentTok, x1
rxDoReturn:
	pop		x3					;2
	pop		YL					;2
	pop		YH					;2
	rjmp	sofError			;2

isSetupOrOut:					; we must be fast here -- a data package may follow / {,24} into next frame
	cp		x2, shift			;1 shift contains our device ID
	brne	otherOutOrSetup		;1 other device -- ignore
	sts		usbCurrentTok, x1	;2
#if 0	/* we implement only one rx endpoint */
	sts		usbRxEndp, x3		;2 only stored if we may have to distinguish endpoints
#endif
;A transmission can still have data in the output buffer while we receive a
;SETUP package with an IN phase. To avoid that the old data is sent as a reply,
;we abort transmission. ### This mechanism assumes that NO OUT OR SETUP package
;is ever sent to endpoint 1. We would abort transmission for endpoint 0
;in this case.
	ldi		x1, -1				;1
	sts		usbMsgLen, x1		;2
	sts		usbTxLen, x1		;2 abort transmission
	pop		x3					;2
	pop		YL					;2
	in		x1, USB_INTR_PENDING;1
	sbrc	x1, USB_INTR_PENDING_BIT;1 check whether data is already arriving {,41} into next frame
	rjmp	shortcutToStart		;2 save the pops and pushes -- a new interrupt is aready pending
;If the jump above was not taken, we can be at {,2} into the next frame here
	pop		YH					;2
sofError:						; error in start of frame -- ignore frame
	ldi		x1, 1<<USB_INTR_PENDING_BIT;1 many int0 events occurred during our processing -- clear pending flag
	out		USB_INTR_PENDING, x1;1
	pop		shift				;2
	pop		cnt					;2
	pop		x2					;2
	pop		x1					;2
	out		SREG, x1			;1
	pop		x1					;2
	reti						;4 -> {,21} into next frame -> up to 3 sync bits missed


sendNakAndReti:					; 21 cycles until SOP
	ldi		YL, lo8(usbNakBuf)	;1
	ldi		YH, hi8(usbNakBuf)	;1
	rjmp	usbSendToken		;2

sendAckAndReti:					; 19 cycles until SOP
	ldi		YL, lo8(usbAckBuf)	;1
	ldi		YH, hi8(usbAckBuf)	;1
usbSendToken:
	ldi		cnt, 2				;1
;;;;rjmp	usbSendAndReti		fallthrough

; USB spec says:
; idle = J
; J = (D+ = 0), (D- = 1) or USBOUT = 0x01
; K = (D+ = 1), (D- = 0) or USBOUT = 0x02
; Spec allows 7.5 bit times from EOP to SOP for replies (= 60 cycles)

;usbSend:
;pointer to data in 'Y'
;number of bytes in 'cnt'
;uses: x1...x4, shift, cnt, Y
usbSendAndReti:				; SOP starts 16 cycles after call
	push	x4				;2
	in		x1, USBOUT		;1
	cbr		x1,	USBMASK		;1 mask out data bits
	ori		x1, USBIDLE		;1 idle
	out		USBOUT, x1		;1 prepare idle state
	ldi		x4, USBMASK		;1 exor mask
	in		x2, USBDDR		;1
	ori		x2, USBMASK		;1 set both pins to output
	out		USBDDR, x2		;1 <-- acquire bus now
; need not init x2 (bitstuff history) because sync starts with 0
	ldi		shift, 0x80		;1 sync byte is first byte sent
	rjmp	txLoop			;2 -> 13 + 3 = 16 cycles until SOP

#if USB_CFG_HAVE_INTRIN_ENDPOINT	/* placed here due to relative jump range */
handleIn1:
	lds		cnt, usbTxLen1
	cpi		cnt, -1
	breq	sendNakAndReti
	ldi		x1, -1
	sts		usbTxLen1, x1
	ldi		YL, lo8(usbTxBuf1)
	ldi		YH, hi8(usbTxBuf1)
	rjmp	usbSendAndReti
#endif

bitstuff0:					;1 (for branch taken)
	eor		x1, x4			;1
	ldi		x2, 0			;1
	out		USBOUT, x1		;1 <-- out
	rjmp	didStuff0		;2 branch back 2 cycles earlier
bitstuff1:					;1 (for branch taken)
	eor		x1, x4			;1
	ldi		x2, 0			;1
	sec						;1 set carry so that brsh will not jump
	out		USBOUT, x1		;1 <-- out
	rjmp	didStuff1		;2 jump back 1 cycle earler
bitstuff2:					;1 (for branch taken)
	eor		x1, x4			;1
	ldi		x2, 0			;1
	rjmp	didStuff2		;2 jump back 3 cycles earlier and do out
bitstuff3:					;1 (for branch taken)
	eor		x1, x4			;1
	ldi		x2, 0			;1
	rjmp	didStuff3		;2 jump back earlier

txLoop:
	sbrs	shift, 0		;1
	eor		x1, x4			;1
	out		USBOUT, x1		;1 <-- out
	ror		shift			;1
	ror		x2				;1
didStuff0:
	cpi		x2, 0xfc		;1
	brsh	bitstuff0		;1
	sbrs	shift, 0		;1
	eor		x1, x4			;1
	ror		shift			;1
	out		USBOUT, x1		;1 <-- out
	ror		x2				;1
	cpi		x2, 0xfc		;1
didStuff1:
	brsh	bitstuff1		;1
	sbrs	shift, 0		;1
	eor		x1, x4			;1
	ror		shift			;1
	ror		x2				;1
didStuff2:
	out		USBOUT, x1		;1 <-- out
	cpi		x2, 0xfc		;1
	brsh	bitstuff2		;1
	sbrs	shift, 0		;1
	eor		x1, x4			;1
	ror		shift			;1
	ror		x2				;1
didStuff3:
	cpi		x2, 0xfc		;1
	out		USBOUT, x1		;1 <-- out
	brsh	bitstuff3		;1
	nop2					;2
	ld		x3, y+			;2
	sbrs	shift, 0		;1
	eor		x1, x4			;1
	out		USBOUT, x1		;1 <-- out
	ror		shift			;1
	ror		x2				;1
didStuff4:
	cpi		x2, 0xfc		;1
	brsh	bitstuff4		;1
	sbrs	shift, 0		;1
	eor		x1, x4			;1
	ror		shift			;1
	out		USBOUT, x1		;1 <-- out
	ror		x2				;1
	cpi		x2, 0xfc		;1
didStuff5:
	brsh	bitstuff5		;1
	sbrs	shift, 0		;1
	eor		x1, x4			;1
	ror		shift			;1
	ror		x2				;1
didStuff6:
	out		USBOUT, x1		;1 <-- out
	cpi		x2, 0xfc		;1
	brsh	bitstuff6		;1
	sbrs	shift, 0		;1
	eor		x1, x4			;1
	ror		shift			;1
	ror		x2				;1
didStuff7:
	cpi		x2, 0xfc		;1
	out		USBOUT, x1		;1 <-- out
	brsh	bitstuff7		;1
	mov		shift, x3		;1
	dec		cnt				;1
	brne	txLoop			;2 | 1
	cbr		x1, USBMASK		;1 prepare SE0
	pop		x4				;2
	out		USBOUT, x1		;1 <-- out SE0
	ldi		cnt, 4			;1 two bits = 16 cycles
se0Delay:
	dec		cnt				;1
	brne	se0Delay		;2 | 1
	ori		x1, USBIDLE		;1
	in		x2, USBDDR		;1
	cbr		x2, USBMASK		;1 set both pins to input
	out		USBOUT, x1		;1 <-- out J (idle)
	cbr		x1, USBMASK		;1 configure no pullup on both pins
	pop		x3				;2
	pop		YL				;2
	out		USBDDR, x2		;1 <-- release bus now
	out		USBOUT, x1		;1 set pullup state
	pop		YH				;2
	rjmp	sofError		;2 [we want to jump to rxDoReturn, but this saves cycles]

bitstuff4:					;1 (for branch taken)
	eor		x1, x4			;1
	ldi		x2, 0			;1
	out		USBOUT, x1		;1 <-- out
	rjmp	didStuff4		;2 jump back 2 cycles earlier
bitstuff5:					;1 (for branch taken)
	eor		x1, x4			;1
	ldi		x2, 0			;1
	sec						;1 set carry so that brsh is not taken
	out		USBOUT, x1		;1 <-- out
	rjmp	didStuff5		;2 jump back 1 cycle earlier
bitstuff6:					;1 (for branch taken)
	eor		x1, x4			;1
	ldi		x2, 0			;1
	rjmp	didStuff6		;2 jump back 3 cycles earlier and do out there
bitstuff7:					;1 (for branch taken)
	eor		x1, x4			;1
	ldi		x2, 0			;1
	rjmp	didStuff7		;2 jump back 4 cycles earlier

; ######################## utility functions ########################

; extern unsigned usbCrc16(unsigned char *data, unsigned char len);
; data: r24/25
; len: r22
; temp variables:
;   r18: data byte
;   r19: bit counter
;   r20/21: polynomial
;   r23: scratch
;   r24/25: crc-sum
;   r26/27=X: ptr
.global	usbCrc16
usbCrc16:
	mov		XL, r24
	mov		XH, r25
	ldi		r24, 0xff
	ldi		r25, 0xff
	ldi		r20, lo8(0xa001)
	ldi		r21, hi8(0xa001)
crcByteLoop:
	subi	r22, 1
	brcs	crcReady
	ld		r18, x+
	ldi		r19, 8
crcBitLoop:
	mov		r23, r18
	eor		r23, r24
	lsr		r25
	ror		r24
	lsr		r18
	sbrs	r23, 0
	rjmp	crcNoXor
	eor		r24, r20
	eor		r25, r21
crcNoXor:
	dec		r19
	brne	crcBitLoop
	rjmp	crcByteLoop
crcReady:
	com		r24
	com		r25
	ret
