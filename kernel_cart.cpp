/*
__________               __________.___     _________                __   
\______   \_____    _____\______   \   |    \_   ___ \_____ ________/  |_ 
 |       _/\__  \  /  ___/|     ___/   |    /    \  \/\__  \\_  __ \   __\
 |    |   \ / __ \_\___ \ |    |   |   |    \     \____/ __ \|  | \/|  |  
 |____|_  /(____  /____  >|____|   |___|     \______  (____  /__|   |__|  
        \/      \/     \/                           \/     \/             

 kernel_cart.cpp

 RasPIC64 - A framework for interfacing the C64 and a Raspberry Pi 3B/3B+
          - RasPI Cart: example how to implement a CBM80 cartridge
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

//#define USE_LATCH_FOR_GAMEEXROM

// define this if you use a RaspberryPi 3B+
#define TIMINGS_RPI3B_PLUS

#include "kernel_cart.h"

// setting EXROM and GAME (low = 0, high = 1)
#define SET_EXROM	0
#define SET_GAME	1

// cartridge memory window bROML or bROMH
#define ROM_LH		bROML

// simply defines "const unsigned char cart[8192]", a binary dump of a 8k cartridge
#include "Cartridges/cart_d020.h" //  ROML, EXROM closed
//#include "Cartridges/cart_1541.h" //  ROML, EXROM closed
//geht noch nicht, settings überprüfen:
//#include "Cartridges/cart_deadtest.h" // ROMH, GAME closed 
//#include "Cartridges/cart_diag41.h" // ROML, EXROM und GAME closed
//#include "Cartridges/cart_64erdisc.h" //  ROML, EXROM closed

// note:
// the above includes are binary dumps of CBM80 cartridges
// before use the data is reorganized such that A8...A12 become the lower bits of the address (and A0..7 become bits 6 to 12)
// I decided to have this reshuffling in this code to make it independent from the dumped bins.
// (with different wiring of the RPi this could be circumvented)
unsigned char cart_cacheoptimized_pool[ 16384 ];
unsigned char *cart_cacheoptimized;

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

	// initialize latch and software I2C buffer
	#ifdef USE_LATCH_OUTPUT
	initLatch();

	#ifdef USE_OLED
	// I know this is a gimmick, but I couldn't resist ;-)
	splashScreen( raspi_cart_splash );
	#endif

		#ifdef USE_LATCH_FOR_GAMEEXROM
		if ( SET_EXROM == 0 )
			clrLatch( LATCH_EXROM ); else
			setLatch( LATCH_EXROM ); 
		if ( SET_GAME == 0 )
			clrLatch( LATCH_GAME ); else
			setLatch( LATCH_GAME ); 
		outputLatch();
		#else

		u32 set = 0, clr = 0;

		if ( SET_EXROM == 0 )
			clr |= bEXROM; else
			set |= bEXROM; 

		if ( SET_GAME == 0 )
			clr |= bGAME; else
			set |= bGAME; 

		write32( ARM_GPIO_GPSET0, set );
		write32( ARM_GPIO_GPCLR0, clr ); 

		#endif
	#endif

	// convert CBM80 rom into a cache optimized format
	cart_cacheoptimized = (unsigned char *)( ((u32)&cart_cacheoptimized_pool+64) & ~63 );

	for ( unsigned int i = 0; i < 8192; i++ )
	{
		int realAdr = ( ( i & 255 ) << 5 ) | ( ( i >> 8 ) & 31 );
		cart_cacheoptimized[ realAdr ] = cart[ i ];
	}

	return bOK;
}

__attribute__( ( always_inline ) ) inline void warmCache()
{
	u8 *ptr = (u8*)&cart_cacheoptimized[ 0 ];
	for ( register u32 i = 0; i < 8192 / 64; i++ )
	{
		CACHE_PRELOAD( ptr );
		ptr += 64;
	}
}

// instruction cache prefetching
__attribute__( ( always_inline ) ) inline void prefetchI( const void *ptr )
{
	asm volatile( "pli [%0]\n" : : "r" ( ptr ) );
}

void CKernel::Run( void )
{
	// setup FIQ
	m_InputPin.ConnectInterrupt( this->FIQHandler, this );
	m_InputPin.EnableInterrupt( GPIOInterruptOnRisingEdge );

	warmCache();

	// wait forever
	while ( true )
	{
		warmCache();
		prefetchI( *((void**)this->FIQHandler) ); 
		//asm volatile ("loop:");
		asm volatile ("wfi");
		//asm volatile ("B loop");
	}

	// and we'll never reach this...
	m_InputPin.DisableInterrupt();
}

void CKernel::FIQHandler (void *pParam)
{
	register u32 g2;
	register u32 g3;
	register u32 D;

	prefetchI( &&cachesetup ); 
	prefetchI( &&romaccess ); 

	BEGIN_CYCLE_COUNTER

	// here's a really nasty trick:
	// we switch the multiplexers to A8..12, but because of some external delay we read the signals before they switch :-P (relaxes timing)
	write32( ARM_GPIO_GPSET0, 1 << DIR_CTRL_257 ); 

	// get A0-A7 immediately -- caution: IO1, IO2, ... etc. are not yet valid (the PLA delay is very likely not yet over)
	g2 = read32( ARM_GPIO_GPLEV0 );

	// block wrong executions
	if ( !( g2 & bPHI ) ) 
	{
		write32( ARM_GPIO_GPCLR0, 1 << DIR_CTRL_257 ); 
		return;
	}

	// we got the A0..A7 part of the address which we will access
	// and preload this chunk of 32 bytes into the cache
	// (otherwise cache misses may occur and the bus write cycle might be missed)
	//
	// slightly slower version (only assuming A0-A7 map to consecutive GPIOs):
	// u32 addr = ( ( g2 >> A0 ) & 255 ) << 5;
	// slightly faster, but be careful: the following line assumes that A0 == 5 (!) -- then it matches to position where we want A0-A7 shifted to
cachesetup:
	register u32 addr = g2 & ( 255 << 5 );
	CACHE_PRELOAD( &cart_cacheoptimized[ addr ] );

	// this value is very critical! 
	// too low => switching to A8..12 not ready and you'll read A0..A7 again (or some mess)
	// too high => you'll be late for writing to the bus
	// 160 works well für RPi 3B, for a 3B+ choose ... TODO
	#ifndef TIMINGS_RPI3B_PLUS
	WAIT_UP_TO_CYCLE( 160 );
	#else
	WAIT_UP_TO_CYCLE( 200 ); // 407: 200
	#endif
	g3 = read32( ARM_GPIO_GPLEV0 );

romaccess:
	if ( ( g3 & ROM_LH ) || !( g3 & bRW ) )
	{
		write32( ARM_GPIO_GPCLR0, 1 << DIR_CTRL_257 ); 
		return;
	}

	// make our address complete
	addr |= ( g3 >> A8 ) & 31;

	// read cartridge rom
	D = cart_cacheoptimized[ addr ] << D0;

	// and put it onto the c64-bus, enable the 74LVC245, and switch the multiplexer back to A0..A7
	write32( ARM_GPIO_GPSET0, D );
	write32( ARM_GPIO_GPCLR0, (D_FLAG & ( ~D )) | (1 << GPIO_OE) | (1 << DIR_CTRL_257) );

	// TODO: timings for RPi 3B+ are different!
	#ifndef TIMINGS_RPI3B_PLUS
	WAIT_UP_TO_CYCLE( 600 );
	#else
	WAIT_UP_TO_CYCLE( 725 ); 
	#endif

	// disable 74LVC245 
	write32( ARM_GPIO_GPSET0, (1 << GPIO_OE) );
}

int main( void )
{
	CKernel kernel;
	if ( kernel.Initialize() )
		kernel.Run();

	halt();
	return EXIT_HALT;
}
