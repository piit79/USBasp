/*
  isp.h - part of USBasp

  Autor..........: Thomas Fischl <tfischl@gmx.de> 
  Description....: Provides functions for communication/programming
                   over ISP interface
  Licence........: Free under certain conditions. See Documentation.
  Creation Date..: 2005-02-23
  Last change....: 2005-04-20
*/

#ifndef __isp_h_included__
#define	__isp_h_included__

#ifndef uchar
#define	uchar	unsigned char
#endif

#define	ISP_OUT   PORTB
#define ISP_IN    PINB
#define ISP_DDR   DDRB
#define ISP_RST   PB2
#define ISP_MOSI  PB3
#define ISP_MISO  PB4
#define ISP_SCK   PB5

#define ISP_DELAY 1
#define ISP_SCK_SLOW 0
#define ISP_SCK_FAST 1

/* Prepare connection to target device */
void ispConnect();

/* Close connection to target device */
void ispDisconnect();

/* read an write a byte from isp using software (slow) */
uchar ispTransmit_sw(uchar send_byte);

/* read an write a byte from isp using hardware (fast) */
uchar ispTransmit_hw(uchar send_byte);

/* enter programming mode */
uchar ispEnterProgrammingMode();

/* read byte from eeprom at given address */
uchar ispReadEEPROM(unsigned int address);

/* write byte to flash at given address */
uchar ispWriteFlash(unsigned int address, uchar data, uchar pollmode);

uchar ispFlushPage(unsigned int address, uchar pollvalue);

/* read byte from flash at given address */
uchar ispReadFlash(unsigned int address);

/* write byte to eeprom at given address */
uchar ispWriteEEPROM(unsigned int address, uchar data);

/* pointer to sw or hw transmit function */
uchar (*ispTransmit)(uchar);

/* set SCK speed. call before ispConnect! */
void ispSetSCKOption(uchar sckoption);

#endif /* __isp_h_included__ */
