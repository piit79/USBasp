#ifndef __usbasp_h__
#define __usbasp_h__

#include "avrpart.h"


#define	USBDEV_VENDOR	0x03eb	/* ATMEL */
#define	USBDEV_PRODUCT	0xc7B4 	/* USBasp */

#define USBASP_FUNC_CONNECT    1
#define USBASP_FUNC_DISCONNECT 2
#define USBASP_FUNC_TRANSMIT   3
#define USBASP_FUNC_READFLASH  4
#define USBASP_FUNC_ENABLEPROG 5
#define USBASP_FUNC_WRITEFLASH 6
#define USBASP_FUNC_READEEPROM 7
#define USBASP_FUNC_WRITEEEPROM 8

#define USBASP_BLOCKFLAG_FIRST    1
#define USBASP_BLOCKFLAG_LAST     2

#define USBASP_READBLOCKSIZE   200
#define USBASP_WRITEBLOCKSIZE  200


void usbasp_initpgm (PROGRAMMER * pgm);

#endif /* __usbasp_h__ */
