/*
__________               __________.___      ___________.__                .__     
\______   \_____    _____\______   \   |     \_   _____/|  | _____    _____|  |__  
 |       _/\__  \  /  ___/|     ___/   |      |    __)  |  | \__  \  /  ___/  |  \ 
 |    |   \ / __ \_\___ \ |    |   |   |      |     \   |  |__/ __ \_\___ \|   Y  \
 |____|_  /(____  /____  >|____|   |___|      \___  /   |____(____  /____  >___|  /
        \/      \/     \/                         \/              \/     \/     \/ 

 kernel_flash.cpp

 RasPIC64 - A framework for interfacing the C64 and a Raspberry Pi 3B/3B+
          - RasPI Flash: example how to implement an generic/magicdesk/easyflash-compatible cartridge
 Copyright (c) 2019 Carsten Dachsbacher <frenetic@dachsbacher.de>
 
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

// this is not yet stable!
//#define USE_LATCH_FOR_GAMEEXROM

// define this if you use a RaspberryPi 3B+
#define TIMINGS_RPI3B_PLUS

#include "kernel_ef.h"
#include "crt.h"

// we will read this .CRT file
static const char DRIVE[] = "SD:";
static const char FILENAME[] = "SD:test.crt";

// temporary buffers to read cartridge data from SD
CRT_HEADER header;

static u8 ef_low_raw[ 1024 * 1024 ];
static u8 ef_high_raw[ 1024 * 1024 ];

static u32 bankswitchType = BS_NONE;

// are we reacting to ROML and/or ROMH?
static u32 ROM_LH = 0;

//
// easyflash (note: generic and magic desk carts are "mapped" to easyflash carts)
//
#define EASYFLASH_BANKS			64
#define EASYFLASH_BANK_MASK		( EASYFLASH_BANKS - 1 )

// easyflash states
static u32 ef_jumper;
static u8  ef_reg0, ef_reg2;
static u8  ef_mode, ef_modeOld;

// ... flash
u8 flash_cacheoptimized_pool[ 1024 * 1024 * 2 + 1024 ] AA;
u8 *flash_cacheoptimized AA;

// ... and extra RAM
static u8  ef_rampool[ 256 + 64 ] AA;
static u8  *ef_ram AA;

// counts the #cycles when the C64-reset line is pulled down (to detect a reset)
u32 resetCounter;

// table with EF memory configurations adapted from Vice
#define M_EXROM	2
#define M_GAME	1
static const u8 ef_memconfig[] = {
    (M_EXROM|M_GAME), (M_EXROM|M_GAME), (M_GAME), (M_GAME),
    (M_EXROM), (M_EXROM|M_GAME), 0, (M_GAME),
    (M_EXROM), (M_EXROM|M_GAME), 0, (M_GAME), 
    (M_EXROM), (M_EXROM|M_GAME), 0, (M_GAME),
};

static u32 updateGAMEEXROM = 0;

__attribute__( ( always_inline ) ) inline void setGAMEEXROM()
{
	ef_mode = ef_memconfig[ ( ef_jumper << 3 ) | ( ef_reg2 & 7 ) ];

#ifdef USE_LATCH_FOR_GAMEEXROM
	if ( ( ef_mode & 2 ) != ( ef_modeOld & 2 ) )
	{
		if ( ( ( ef_mode & 2 ) == 2 ) )
			setLatch( LATCH_EXROM ); else
			clrLatch( LATCH_EXROM ); 
	}
	if ( ( ef_mode & 1 ) != ( ef_modeOld & 1 ) )
	{
		if ( !( ( ef_mode & 1 ) == 1 ) )
			setLatch( LATCH_GAME ); else
			clrLatch( LATCH_GAME ); 
	}

	updateGAMEEXROM = 2;

#else

	u32 set = 0, clr = 0;

	if ( ( ef_mode & 2 ) != ( ef_modeOld & 2 ) )
	{
		if ( ef_mode & 2 )
			set |= bEXROM; else
			clr |= bEXROM;
	}
	if ( ( ef_mode & 1 ) != ( ef_modeOld & 1 ) )
	{
		if ( ef_mode & 1 )
			clr |= bGAME; else
			set |= bGAME;
	}

	if ( set ) write32( ARM_GPIO_GPSET0, set );
	if ( clr ) write32( ARM_GPIO_GPCLR0, clr ); 
#endif

	ef_modeOld = ef_mode;
}

__attribute__( ( always_inline ) ) inline u8 easyflash_IO1_Read( u32 addr )
{
	return ( addr & 2 ) ? ef_reg2 : ef_reg0;
}

__attribute__( ( always_inline ) ) inline void easyflash_IO1_Write( u32 addr, u8 value )
{
	switch ( addr & 2 ) {
	case 0:
		ef_reg0 = (u8)( value & EASYFLASH_BANK_MASK );
		break;
	default:
		ef_reg2 = value & 0x87;
		ef_mode = ef_memconfig[ ( ef_jumper << 3 ) | ( ef_reg2 & 7 ) ];
	}
}

__attribute__( ( always_inline ) ) inline u8 easyflash_IO2_Read( u32 addr )
{
	return ef_ram[ addr & 0xff ];
}

__attribute__( ( always_inline ) ) inline  void easyflash_IO2_Write( u32 addr, u8 value )
{
	ef_ram[ addr & 0xff ] = value;
}

void initEF()
{
	ef_jumper = 0;
	ef_reg0   = 0;
	ef_reg2   = 0;

	if ( bankswitchType == BS_NONE )
	{
		ef_reg2 = 4;
		if ( !header.exrom ) ef_reg2 |= 2;
		if ( !header.game )  ef_reg2 |= 1;
	}

	resetCounter = 0;

	ef_mode = ef_memconfig[ ( ef_jumper << 3 ) | ( ef_reg2 & 7 ) ];
	ef_modeOld = ~ef_mode;
	for ( u32 i = 0; i < 256; i++ )
		ef_ram[ i ] = 0xff;
}

CLogger	*logger;

boolean CKernel::Initialize( void )
{
	boolean bOK = TRUE;

	m_CPUThrottle.SetSpeed( CPUSpeedMaximum );

#ifdef USE_HDMI_VIDEO
	if ( bOK ) bOK = m_Screen.Initialize();

	if ( bOK )
	{
		CDevice *pTarget = m_DeviceNameService.GetDevice( m_Options.GetLogDevice(), FALSE );
		if ( pTarget == 0 )
			pTarget = &m_Screen;

		bOK = m_Logger.Initialize( pTarget );
		logger = &m_Logger;
	}
#endif

	if ( bOK ) bOK = m_Interrupt.Initialize();
	if ( bOK ) bOK = m_Timer.Initialize();

	// initialize ARM cycle counters (for accurate timing)
	initCycleCounter();

	// initialize GPIOs
	gpioInit();
	SET_BANK2_OUTPUT 

	// read .CRT
	//readCRTFile();
	m_EMMC.Initialize();
	readCRTFile( logger, &header, (char*)DRIVE, (char*)FILENAME, (u8*)&ef_low_raw[0], (u8*)&ef_high_raw[0], &bankswitchType, &ROM_LH );

	// get aligned memory for easyflash ram
	ef_ram = (u8*)( ( (u32)&ef_rampool + 64 ) & ~63 );

	// convert flash roms into a cache-friendly format
	flash_cacheoptimized = (u8 *)( ( (u32)&flash_cacheoptimized_pool + 64 ) & ~63 );

	// we always copy the max flash size
	u32 nBlocks = 64;

	for ( u32 b = 0; b < nBlocks; b++ )
		for ( u32 i = 0; i < 8192; i++ )
		{
			u32 realAdr = ( ( i & 255 ) << 5 ) | ( ( i >> 8 ) & 31 );
			flash_cacheoptimized[ ( b * 8192 + realAdr ) * 2 + 0 ] = ef_low_raw[ b * 8192 + i ];
		}

	for ( u32 b = 0; b < nBlocks; b++ )
		for ( u32 i = 0; i < 8192; i++ )
		{
			u32 realAdr = ( ( i & 255 ) << 5 ) | ( ( i >> 8 ) & 31 );
			flash_cacheoptimized[ ( b * 8192 + realAdr ) * 2 + 1 ] = ef_high_raw[ b * 8192 + i ];
		}

	// initialize latch and software I2C buffer
	#ifdef USE_LATCH_OUTPUT
	initLatch();

	#ifdef USE_OLED
	// I know this is a gimmick, but I couldn't resist ;-)
	splashScreen( raspi_flash_splash );
	#endif

	#endif

	initEF();
	setGAMEEXROM();
	outputLatch();

	return bOK;
}


__attribute__( ( always_inline ) ) inline void warmCache4FlashBanks()
{
	u8 *ptr = (u8*)&flash_cacheoptimized[ ef_reg0 * 8192 * 2 ];
	for ( register u32 i = 0; i < 8192 * 2 / 64; i++ )
	{
		CACHE_PRELOAD( ptr );
		ptr += 64;
	}
}

void CKernel::Run( void )
{
	// setup FIQ
	m_InputPin.ConnectInterrupt( this->FIQHandler, this );
	m_InputPin.EnableInterrupt( GPIOInterruptOnRisingEdge );

	CACHE_PRELOAD( &ef_ram[ 0 ] );
	CACHE_PRELOAD( &ef_ram[ 64 ] );
	CACHE_PRELOAD( &ef_ram[ 128 ] );
	CACHE_PRELOAD( &ef_ram[ 192 ] );
	resetCounter = 0xffff;

	// wait forever
	while ( true )
	{
		if ( resetCounter > 3 )
		{
			resetCounter = 0;

			initEF();
			warmCache4FlashBanks();
			setGAMEEXROM();
		}
		asm volatile ("wfi");
	}

	// and we'll never reach this...
	m_InputPin.DisableInterrupt();
}

void CKernel::FIQHandler (void *pParam)
{
	register u32 g2 AA;
	register u32 g3 AA;
	register u32 D AA;

	// not really necessary, but doesn't hurt either
	CACHE_PRELOADI( &&cachesetup ); 
	CACHE_PRELOADI( &&flashrom ); 
	CACHE_PRELOADI( &&efram ); 
	
	BEGIN_CYCLE_COUNTER

	// here's a really nasty trick:
	// we switch the multiplexers to A8..12, but because of some external delay we read the signals before they switch :-P (relaxes timing)
	write32( ARM_GPIO_GPSET0, 1 << DIR_CTRL_257 ); 

	// so let's get A0-A7 immediately -- caution: IO1, IO2, ... etc. may not yet be valid (PLA delay, although the FIQ often takes longer to fire up)
	g2 = read32( ARM_GPIO_GPLEV0 );

	if ( !( g2 & bRESET ) ) resetCounter ++;

	// block wrong executions
	if ( !( g2 & bPHI ) ) 
	{
		write32( ARM_GPIO_GPCLR0, 1 << DIR_CTRL_257 ); 
		return;
	}

cachesetup:

	// here would be the logical time to switch multiplexer to A8..12
	// but the nasty trick (see above) relaxes the timing a lot!
	// write32( ARM_GPIO_GPSET0, 1 << DIR_CTRL_257 ); 

	// we got the A0..A7 part of the address which we will access
	register u32 A = ( g2 >> A0 ) & 255;
	register u32 addr = A << 5;
	
	CACHE_PRELOAD( &flash_cacheoptimized[ ( ( ef_reg0 * 8192 + addr ) * 2 ) ] );
	CACHE_PRELOAD( &ef_ram[ A & ~63 ] );
	CACHE_PRELOADW( &ef_ram[ A & ~63 ] );

	#ifndef TIMINGS_RPI3B_PLUS
	WAIT_UP_TO_CYCLE( 170 );
	#else
	WAIT_UP_TO_CYCLE( 200 ); 
	#endif

	g3 = read32( ARM_GPIO_GPLEV0 );

#if 1
	// access to flash roms
	flashrom:
	//if ( ( g2 & bPHI ) && ( g3 & bRW ) && ( !( g3 & bROML ) || !( g3 & bROMH ) ) )
	if ( ( g2 & bPHI ) && ( g3 & bRW ) && ( ~g3 & ROM_LH ) )
	{
		// make our address complete
		addr |= ( g3 >> A8 ) & 31;

		// read cartridge rom
		if ( !( g3 & bROMH ) )
			D = flash_cacheoptimized[ (ef_reg0 * 8192 + addr)*2+1 ] << D0; else
			D = flash_cacheoptimized[ (ef_reg0 * 8192 + addr)*2+0 ] << D0;
		
		// and put it onto the c64-bus, enable the 74LVC245, and switch the multiplexer back to A0..A7
		write32( ARM_GPIO_GPSET0, D );
		write32( ARM_GPIO_GPCLR0, (D_FLAG & ( ~D )) | (1 << GPIO_OE) | (1 << DIR_CTRL_257) );

		// TODO: timings for RPi 3B+ are different!
		#ifndef TIMINGS_RPI3B_PLUS
		WAIT_UP_TO_CYCLE( 620 );
		#else
		WAIT_UP_TO_CYCLE( 725 ); 
		#endif

		// disable 74LVC245 
		write32( ARM_GPIO_GPSET0, (1 << GPIO_OE) );
		return;
	}
#endif
	efram:
	if ( ( g2 & bPHI ) && (!( g3 & bIO1 ) || !( g3 & bIO2 )) )
	{
		if ( ( bankswitchType == BS_EASYFLASH ) && ( g3 & bRW ) ) // read from periphery / write-to-bus cycle?
		{
			D = (u32)( ef_ram[ A ] ) << D0;

			if ( !( g3 & bIO1 ) ) 
				D = easyflash_IO1_Read( A ) << D0; 

			write32( ARM_GPIO_GPSET0, D );
			write32( ARM_GPIO_GPCLR0, (D_FLAG & ( ~D )) | (1 << GPIO_OE) | (1 << DIR_CTRL_257) );

			// TODO: tweak this timing!
			#ifndef TIMINGS_RPI3B_PLUS
			WAIT_UP_TO_CYCLE( 620 );
			#else
			WAIT_UP_TO_CYCLE( 725 ); 
			#endif

			// disable 74LV245
			write32( ARM_GPIO_GPSET0, 1 << GPIO_OE ); 
			return;
		} else
		{	// read-from-bus (= write to periphery) cycle

			// set bank 2 GPIOs to input (D0-D7)
			SET_BANK2_INPUT 

			// enable 74LVC245
			write32( ARM_GPIO_GPCLR0, (1 << GPIO_OE) | ( 1 << DIR_CTRL_257 ) ); 

			#ifndef TIMINGS_RPI3B_PLUS
			WAIT_UP_TO_CYCLE( 490 );
			#else
			WAIT_UP_TO_CYCLE( 570 ); 
			#endif

			g3 = read32( ARM_GPIO_GPLEV0 );

			// disable 74LV245
			write32( ARM_GPIO_GPSET0, 1 << GPIO_OE ); 

			SET_BANK2_OUTPUT 

			// decode data
			u8 D = ( g3 >> D0 ) & 255;

			// write to easyflash register
			if ( !( g3 & bIO1 ) ) 
			{
				if ( bankswitchType == BS_EASYFLASH )
				{
					// easyflash register in IO1
					easyflash_IO1_Write( A, D );

					if ( ( A & 2 ) == 0 )
					{
						warmCache4FlashBanks();
					} else 
						updateGAMEEXROM = 1;
				} else
				{
					ef_reg0 = (u8)( D & 127 );
					warmCache4FlashBanks();

					if ( !(D & 128) )
						ef_reg2 |= 2; else
						ef_reg2 |= 0;
					
					updateGAMEEXROM = 1;
				}

			} else
				// write to easyflash RAM
				easyflash_IO2_Write( A, D );
		}
	}

	write32( ARM_GPIO_GPCLR0, 1 << DIR_CTRL_257 ); 

	if ( updateGAMEEXROM )
	{
		setGAMEEXROM();
		updateGAMEEXROM = 0;
		#ifdef USE_LATCH_FOR_GAMEEXROM
		outputLatch();
		#endif
	}

	return;

}

int main( void )
{
	CKernel kernel;
	if ( kernel.Initialize() )
		kernel.Run();

	halt();
	return EXIT_HALT;
}

