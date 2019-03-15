/*
   __________               __________.___     __________    _____      _____		
   \______   \_____    _____\______   \   |    \______   \  /  _  \    /     \  
    |       _/\__  \  /  ___/|     ___/   |     |       _/ /  /_\  \  /  \ /  \ 
    |    |   \ / __ \_\___ \ |    |   |   |     |    |   \/    |    \/    Y    \
    |____|_  /(____  /____  >|____|   |___|     |____|_  /\____|__  /\____|__  /
           \/      \/     \/                           \/         \/         \/ 

 kernel_ram.cpp

 RasPIC64 - A framework for interfacing the C64 and a Raspberry Pi 3B/3B+
          - RasPI RAM: example how to implement a GeoRAM/NeoRAM compatible memory expansion
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

// define this if you use a RaspberryPi 3B+
#define TIMINGS_RPI3B_PLUS

#include "kernel_georam.h"

// size of GeoRAM/NeoRAM in Kilobytes
const int geoSizeKB = 2048;

// GeoRAM registers
// $dffe : selection of 256 Byte-window in 16 Kb-block
// $dfff : selection of 16 Kb-nlock
u8  geoReg[ 2 ] AA;	

// GeoRAM memory pool and actual pointer
u8  geoRAM_Pool[ geoSizeKB * 1024 + 64 ] AA;
u8 *geoRAM AA;

// u8* to current window
#define GEORAM_WINDOW (&geoRAM[ ( geoReg[ 1 ] * 16384 ) + ( geoReg[ 0 ] * 256 ) ])

// GeoRAM helper routines
void geoRAM_Init()
{
	geoReg[ 0 ] = geoReg[ 1 ] = 0;
	geoRAM = (u8*)( ( (u32)&geoRAM_Pool + 64 ) & ~63 );
}

__attribute__( ( always_inline ) ) inline u8 geoRAM_IO1_Read( u32 A )
{
	return GEORAM_WINDOW[ A ];
}

__attribute__( ( always_inline ) ) inline void geoRAM_IO1_Write( u32 A, u8 D )
{
	GEORAM_WINDOW[ A ] = D;
}

__attribute__( ( always_inline ) ) inline u8 geoRAM_IO2_Read( u32 A )
{
    if ( A < 2 )
		return geoReg[ A & 1 ];
	return 0;
}

__attribute__( ( always_inline ) ) inline void geoRAM_IO2_Write( u32 A, u8 D )
{
	if ( ( A & 1 ) == 1 )
		geoReg[ 1 ] = D & ( ( geoSizeKB / 16 ) - 1 ); else
		geoReg[ 0 ] = D & 63;
}


CLogger	*logger;

boolean CKernel::Initialize( void )
{
	boolean bOK = TRUE;

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

	// initialize latch and software I2C buffer
	#ifdef USE_LATCH_OUTPUT
	initLatch();

	setLatch( LATCH_EXROM );
	setLatch( LATCH_GAME );
	outputLatch();

	#ifdef USE_OLED
	// I know this is a gimmick, but I couldn't resist ;-)
	splashScreen( raspi_ram_splash );
	#endif

	#endif

	// GeoRAM initialization
	geoRAM_Init();

	return bOK;
}

void CKernel::Run( void )
{
	// setup FIQ
	m_InputPin.ConnectInterrupt( this->FIQHandler, this );
	m_InputPin.EnableInterrupt( GPIOInterruptOnRisingEdge );

	// wait forever
	while ( true )
	{
		asm volatile ("wfi");
	}

	// and we'll never reach this...
	m_InputPin.DisableInterrupt();
}


void CKernel::FIQHandler (void *pParam)
{
	u32 g2;

	BEGIN_CYCLE_COUNTER

	// preload cache
	CACHE_PRELOAD( GEORAM_WINDOW[ 0 ] );
	CACHE_PRELOAD( GEORAM_WINDOW[ 64 ] );
	CACHE_PRELOAD( GEORAM_WINDOW[ 128 ] );
	CACHE_PRELOAD( GEORAM_WINDOW[ 192 ] );

	// wait until 25ns after FIQ start
	// PLS100-based PLAs (+ similar ones, and presumably modern replacements) are fast enough so that this delay is fine with 45 cycles. 
	// For an EEPROM-PLA I needed to go higher (85 worked well in this particular case) -- for GeoRAM timing is relatively relaxed
	#ifndef TIMINGS_RPI3B_PLUS
	WAIT_UP_TO_CYCLE( 45+40 );
	#else
	WAIT_UP_TO_CYCLE( 91 );
	#endif

	// get A0-A7, IO1, IO2, ...
	g2 = read32( ARM_GPIO_GPLEV0 );

	// block wrong executions
	if ( !( g2 & bPHI ) ) return;

	// no access to GeoRAM => exit
	if ( ( g2 & bIO1 ) && ( g2 & bIO2 ) ) return;

	// ... and figure out whether it's a read-from-periphery / write-to-bus cycle
	if ( g2 & bRW )
	{
		u32 A = ( g2 >> A0 ) & 255;
		u8 byte;

		// GeoRAM read from memory page
		if ( !( g2 & bIO1 ) ) 
		{
			byte = geoRAM_IO1_Read( A );
		} else
		// GeoRAM read register
		//if ( !( g2 & bIO2 ) ) // always true here
		{
			byte = geoRAM_IO2_Read( A );
		}

		u32 D = encodeGPIO( byte );

		write32( ARM_GPIO_GPSET0, D );
		write32( ARM_GPIO_GPCLR0, (D_FLAG & ( ~D )) | (1 << GPIO_OE) );

		// TODO: tweak this timing!
		#ifndef TIMINGS_RPI3B_PLUS
		WAIT_UP_TO_CYCLE( 900 );
		#else
		WAIT_UP_TO_CYCLE( 1025 ); // 407: 1050
		#endif

		// disable 74LV245
		write32( ARM_GPIO_GPSET0, 1 << GPIO_OE ); 
	} else
	// if ( !( g2 & bRW ) ) // always true here
	{
		// read-from-bus (= write to periphery) cycle

		// set bank 2 GPIOs to input (D0-D7)
		SET_BANK2_INPUT 

		// enable 74LVC245
		write32( ARM_GPIO_GPCLR0, (1 << GPIO_OE) );

		u32 A = ( g2 >> A0 ) & 255;

		#ifndef TIMINGS_RPI3B_PLUS
		WAIT_UP_TO_CYCLE( 500 );
		#else
		WAIT_UP_TO_CYCLE( 570 ); // 407: 580
		#endif

		g2 = read32( ARM_GPIO_GPLEV0 );

		// disable 74LV245
		write32( ARM_GPIO_GPSET0, 1 << GPIO_OE ); 

		SET_BANK2_OUTPUT 

		// decode data
		u8 D = ( g2 >> D0 ) & 255;

		// GeoRAM write to memory page
		if ( !( g2 & bIO1 ) ) 
		{
			geoRAM_IO1_Write( A, D );
		} else
		// GeoRAM write register
		//if ( !( g2 & bIO2 ) ) // always true here
		{
			geoRAM_IO2_Write( A, D );
		}	
	}
}

int main( void )
{
	CKernel kernel;
	if ( kernel.Initialize() )
		kernel.Run();

	halt();
	return EXIT_HALT;
}
