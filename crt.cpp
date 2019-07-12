// crt.cpp part of...
//
// RasPIC64 - A framework for interfacing the C64 and a Raspberry Pi 3B/3B+
// Copyright (c) 2019 Carsten Dachsbacher <frenetic@dachsbacher.de>
// 
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
#include "crt.h"

u32 swapBytesU32( u8 *buf )
{
	return buf[ 3 ] | ( buf[ 2 ] << 8 ) | ( buf[ 1 ] << 16 ) | ( buf[ 0 ] << 24 );
}

u16 swapBytesU16( u8 *buf )
{
	return buf[ 1 ] | ( buf[ 0 ] << 8 );
}

// .CRT reading
void readCRTFile( CLogger *logger, CRT_HEADER *crtHeader, char *DRIVE, char *FILENAME, u8 *ef_low_raw, u8 *ef_high_raw, u32 *bankswitchType, u32 *ROM_LH )
{
	CRT_HEADER header;

	FATFS m_FileSystem;

	// mount file system
	if ( f_mount( &m_FileSystem, DRIVE, 1 ) != FR_OK )
		logger->Write( "RaspiFlash", LogPanic, "Cannot mount drive: %s", DRIVE );

	// get filesize
	FILINFO info;
	u32 result = f_stat( FILENAME, &info );
	u32 filesize = (u32)info.fsize;

	// open file
	FIL file;
	result = f_open( &file, FILENAME, FA_READ | FA_OPEN_EXISTING );
	if ( result != FR_OK )
		logger->Write( "RaspiFlash", LogPanic, "Cannot open file: %s", FILENAME );

	if ( filesize > 1025 * 1024 )
		filesize = 1025 * 1024;

	// read data in one big chunk
	u32 nBytesRead;
	u8 rawCRT[ 1025 * 1024 ];
	result = f_read( &file, rawCRT, filesize, &nBytesRead );

	if ( result != FR_OK )
		logger->Write( "RaspiFlash", LogError, "Read error" );

	if ( f_close( &file ) != FR_OK )
		logger->Write( "RaspiFlash", LogPanic, "Cannot close file" );

	// unmount file system
	if ( f_mount( 0, DRIVE, 0 ) != FR_OK )
		logger->Write( "RaspiFlash", LogPanic, "Cannot unmount drive: %s", DRIVE );


	// now "parse" the file which we already have in memory
	u8 *crt = rawCRT;
	u8 *crtEnd = crt + filesize;

	#define readCRT( dst, bytes ) memcpy( (dst), crt, bytes ); crt += bytes;

	readCRT( &header.signature, 16 );

	if ( memcmp( CRT_HEADER_SIG, header.signature, 16 ) )
	{
		logger->Write( "RaspiFlash", LogPanic, "no CRT file." );
	}

	readCRT( &header.length, 4 );
	readCRT( &header.version, 2 );
	readCRT( &header.type, 2 );
	readCRT( &header.exrom, 1 );
	readCRT( &header.game, 1 );
	readCRT( &header.reserved, 6 );
	readCRT( &header.name, 32 );
	header.name[ 32 ] = 0;

	header.length = swapBytesU32( (u8*)&header.length );
	header.version = swapBytesU16( (u8*)&header.version );
	header.type = swapBytesU16( (u8*)&header.type );

	switch ( header.type ) {
	case 32:
		*bankswitchType = BS_EASYFLASH;
		*ROM_LH = bROML | bROMH;
		break;
	case 19:
		*bankswitchType = BS_MAGICDESK;
		*ROM_LH = bROML;
	case 0:
	default:
		*bankswitchType = BS_NONE;
		*ROM_LH = 0;
		break;
	}

	#ifdef CONSOLE_DEBUG
	logger->Write( "RaspiFlash", LogNotice, "length=%d", header.length );
	logger->Write( "RaspiFlash", LogNotice, "version=%d", header.version );
	logger->Write( "RaspiFlash", LogNotice, "type=%d", header.type );
	logger->Write( "RaspiFlash", LogNotice, "exrom=%d", header.exrom );
	logger->Write( "RaspiFlash", LogNotice, "game=%d", header.game );
	logger->Write( "RaspiFlash", LogNotice, "name=%s", header.name );
	#endif

	while ( crt < crtEnd )
	{
		CHIP_HEADER chip;

		memset( &chip, 0, sizeof( CHIP_HEADER ) );

		readCRT( &chip.signature, 4 );

		if ( memcmp( CHIP_HEADER_SIG, chip.signature, 4 ) )
		{
			logger->Write( "RaspiFlash", LogPanic, "no valid CHIP section." );
		}

		readCRT( &chip.total_length, 4 );
		readCRT( &chip.type, 2 );
		readCRT( &chip.bank, 2 );
		readCRT( &chip.adr, 2 );
		readCRT( &chip.rom_length, 2 );

		chip.total_length = swapBytesU32( (u8*)&chip.total_length );
		chip.type = swapBytesU16( (u8*)&chip.type );
		chip.bank = swapBytesU16( (u8*)&chip.bank );
		chip.adr = swapBytesU16( (u8*)&chip.adr );
		chip.rom_length = swapBytesU16( (u8*)&chip.rom_length );

		#ifdef CONSOLE_DEBUG
		logger->Write( "RaspiFlash", LogNotice, "total length=%d", chip.total_length );
		logger->Write( "RaspiFlash", LogNotice, "type=%d", chip.type );
		logger->Write( "RaspiFlash", LogNotice, "bank=%d", chip.bank );
		logger->Write( "RaspiFlash", LogNotice, "adr=$%x", chip.adr );
		logger->Write( "RaspiFlash", LogNotice, "rom length=%d", chip.rom_length );
		#endif

		if ( chip.adr == 0x8000 )
		{
			*ROM_LH |= bROML;
			memcpy( &ef_low_raw[ chip.bank * 8192 ], crt, 8192 );
			crt += 8192;

			if ( chip.rom_length > 8192 )
			{
				*ROM_LH |= bROMH;
				memcpy( &ef_high_raw[ chip.bank * 8192 ], crt, 8192 );
				crt += chip.rom_length - 8192;
			}
		} else
		{
			*ROM_LH |= bROMH;
			memcpy( &ef_high_raw[ chip.bank * 8192 ], crt, 8192 );
			crt += 8192;
		}
	}

	memcpy( crtHeader, &header, sizeof( CRT_HEADER ) );
}