//   __________________.___________   
//  /  _____/\______   \   \_____  \  
// /   \  ___ |     ___/   |/   |   \ 
// \    \_\  \|    |   |   /    |    \
//  \______  /|____|   |___\_______  /
//         \/                      \/ 
//
// gpio_defs.cpp part of...
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
#include <circle/bcm2835.h>
#include <circle/gpiopin.h>
#include <circle/memio.h>
#include "gpio_defs.h"

static void INP_GPIO( int pin )
{
	unsigned nSelReg = ARM_GPIO_GPFSEL0 + ( pin / 10 ) * 4;
	unsigned nShift = ( pin % 10 ) * 3;

	u32 nValue = read32 ( nSelReg );
	nValue &= ~( 7 << nShift );
	write32 ( nSelReg, nValue );
}

static void OUT_GPIO( int pin )
{
	unsigned nSelReg = ARM_GPIO_GPFSEL0 + ( pin / 10 ) * 4;
	unsigned nShift = ( pin % 10 ) * 3;

	u32 nValue = read32 ( nSelReg );
	nValue &= ~( 7 << nShift );
	nValue |= 1 << nShift;
	write32 ( nSelReg, nValue );
}

void gpioInit()
{
	// D0 - D7
	INP_GPIO( D0 );	INP_GPIO( D1 ); INP_GPIO( D2 );	INP_GPIO( D3 );
	INP_GPIO( D4 );	INP_GPIO( D5 );	INP_GPIO( D6 );	INP_GPIO( D7 );
	SET_BANK2_INPUT

	// A0-A7 (A8-A12, ROML, ROMH use the same GPIOs)
	INP_GPIO( A0 );	INP_GPIO( A1 );	INP_GPIO( A2 );	INP_GPIO( A3 );
	INP_GPIO( A4 ); INP_GPIO( A5 ); INP_GPIO( A6 ); INP_GPIO( A7 );

	// RW, PHI2, CS etc.
	INP_GPIO( RW );		INP_GPIO( CS );
	INP_GPIO( PHI2 );	INP_GPIO( RESET );
	INP_GPIO( IO1 );	INP_GPIO( IO2 );

	// GPIOs for controlling the level shifter
	INP_GPIO( GPIO_OE );
	OUT_GPIO( GPIO_OE );
	write32( ARM_GPIO_GPSET0, 1 << GPIO_OE );

	// ... and the multiplexer
	INP_GPIO( DIR_CTRL_257 );
	OUT_GPIO( DIR_CTRL_257 );
	write32( ARM_GPIO_GPCLR0, 1 << DIR_CTRL_257 );

	INP_GPIO( LATCH_CONTROL );
	OUT_GPIO( LATCH_CONTROL );
	write32( ARM_GPIO_GPSET0, 1 << LATCH_CONTROL );

	// currently not used
	/* INP_GPIO( LATCH_OE );
	OUT_GPIO( LATCH_OE );
	write32( ARM_GPIO_GPSET0, 1 << LATCH_OE ); */
}

// decodes SID address and data from GPIOs using the above mapping
void decodeGPIO( u32 g, u8 *a, u8 *d )
{
	u32 A, D;

	A = ( g >> A0 ) & 31;
	D = ( g >> D0 ) & 255;

	#ifdef EMULATE_OPL2
	A |= ( ( g & bIO2 ) >> IO2 ) << 6;
	#endif

	*a = A; *d = D;
}

void decodeGPIOData( u32 g, u8 *d )
{
	*d = ( g >> D0 ) & 255;
}

// encodes one byte for the D0-D7-GPIOs using the above mapping
u32 encodeGPIO( u32 v )
{
	return ( v & 255 ) << D0;
}
