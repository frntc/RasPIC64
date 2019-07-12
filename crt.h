/*
 crt.h

 RasPIC64 - A framework for interfacing the C64 and a Raspberry Pi 3B/3B+
 // Copyright (c) 2019 Carsten Dachsbacher <frenetic@dachsbacher.de>
 
 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/
#ifndef _crt_h
#define _crt_h

//#define CONSOLE_DEBUG

#include <circle/types.h>
#include <circle/util.h>
#include <circle/logger.h>
#include <fatfs/ff.h>
#include "gpio_defs.h"

// which type of cartridge are we emulating?
#define BS_NONE			0x00
#define BS_EASYFLASH	0x01
#define BS_MAGICDESK	0x02

static const char CRT_HEADER_SIG[] = "C64 CARTRIDGE   ";
static const char CHIP_HEADER_SIG[] = "CHIP";

typedef struct  {
	u8  signature[16];
	u32 length;
    u16 version;
    u16 type;
    u8  exrom;
    u8  game;
	u8  reserved[ 6 ];
    u8  name[32 + 1];
} CRT_HEADER;

typedef struct  {
	u8  signature[4];
	u32 total_length;
    u16 type;
	u16 bank;
	u16 adr;
	u16 rom_length;
	u8  data[ 8192 ];
} CHIP_HEADER;

void readCRTFile( CLogger *logger, CRT_HEADER *crtHeader, char *DRIVE, char *FILENAME, u8 *ef_low_raw, u8 *ef_high_raw, u32 *bankswitchType, u32 *ROM_LH );

#endif