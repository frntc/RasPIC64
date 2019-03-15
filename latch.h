//  .____            __         .__          ___                                ________          __                 __           ___    
//  |    |   _____ _/  |_  ____ |  |__      /  /   _____   ___________   ____   \_____  \  __ ___/  |_______  __ ___/  |_  ______ \  \   
//  |    |   \__  \\   __\/ ___\|  |  \    /  /   /     \ /  _ \_  __ \_/ __ \   /   |   \|  |  \   __\____ \|  |  \   __\/  ___/  \  \  
//  |    |___ / __ \|  | \  \___|   Y  \  (  (   |  Y Y  (  <_> )  | \/\  ___/  /    |    \  |  /|  | |  |_> >  |  /|  |  \___ \    )  ) 
//  |_______ (____  /__|  \___  >___|  /   \  \  |__|_|  /\____/|__|    \___  > \_______  /____/ |__| |   __/|____/ |__| /____  >  /  /  
//          \/    \/          \/     \/     \__\       \/                   \/          \/            |__|                    \/  /__/   
//
// latch.h part of...
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

#ifndef _latch_h
#define _latch_h

#include <circle/bcm2835.h>
#include <circle/types.h>
#include <circle/gpiopin.h>
#include <circle/memio.h>
#include "gpio_defs.h"
#include "lowlevel_arm.h"

#define LATCH_RESET		(1<<D0)
#define LATCH_GAME		(1<<D1)
#define LATCH_EXROM		(1<<D2)
#define LATCH_SDA		(1<<D6)
#define LATCH_SCL		(1<<D7)

// ugly, need to make this inlining nicer...

// current and last status of the data lines
// connected to the latch input (GPIO D0-D7 -> 74LVX573D D0-D7)
extern u32 latchD, latchClr, latchSet;
extern u32 latchDOld;

extern void initLatch();

// a tiny ring buffer for simple I2C output via the latch
#define FAKE_I2C_BUF_SIZE ( 65536 * 8 )
extern u8 i2cBuffer[ FAKE_I2C_BUF_SIZE ];
extern u32 i2cBufferCountLast, i2cBufferCountCur;

static __attribute__( ( always_inline ) ) inline void setLatch( u32 f )
{
	latchSet |= f;
}

static __attribute__( ( always_inline ) ) inline void clrLatch( u32 f )
{
	latchClr |= f;
}

static __attribute__( ( always_inline ) ) inline void setLatchFIQ( u32 f )
{
	latchD |= f;
}

static __attribute__( ( always_inline ) ) inline void clrLatchFIQ( u32 f )
{
	latchD &= ~f;
}

static __attribute__( ( always_inline ) ) inline void putI2CCommand( u32 c )
{
	i2cBuffer[ i2cBufferCountCur ++ ] = c;
	i2cBufferCountCur &= ( FAKE_I2C_BUF_SIZE - 1 );
}

static __attribute__( ( always_inline ) ) inline u32 getI2CCommand()
{
	u32 ret = i2cBuffer[ i2cBufferCountLast ++ ];
	i2cBufferCountLast &= ( FAKE_I2C_BUF_SIZE - 1 );
	return ret;
}

static __attribute__( ( always_inline ) ) inline u32 peekI2CCommand()
{
	u32 ret = i2cBuffer[ i2cBufferCountLast ];
	return ret;
}

static __attribute__( ( always_inline ) ) inline boolean bufferEmptyI2C()
{
	return ( i2cBufferCountLast == i2cBufferCountCur );
}


static __attribute__( ( always_inline ) ) inline void outputLatch()
{
	if ( !bufferEmptyI2C() )
	{
		NextCommand:
		u32 c = getI2CCommand();
		u32 port = (c >> 1) & 1;
		static u32 lastSDAValue = 255;

		if ( port == 0 ) // SDA
		{
			// optimization: if SDA didn't change, no need to output it to I2C
			if ( (c & 1) == lastSDAValue )
			{
				if ( !bufferEmptyI2C() )
					goto NextCommand; else
					goto NothingToDo; 
			}
			lastSDAValue = c & 1;

			if ( c & 1 ) setLatchFIQ( LATCH_SDA ); else clrLatchFIQ( LATCH_SDA );
		} else
		//if ( port == 1 ) // SCL
		{
			if ( c & 1 ) setLatchFIQ( LATCH_SCL ); else clrLatchFIQ( LATCH_SCL );
		}
	}

	setLatchFIQ( latchSet );
	clrLatchFIQ( latchClr );
	latchSet = latchClr = 0;
	
NothingToDo:;
	if ( latchD != latchDOld )
	{
		latchDOld = latchD;

		write32( ARM_GPIO_GPSET0, ( D_FLAG & latchD ) );
		write32( ARM_GPIO_GPCLR0, ( D_FLAG & ( ~latchD ) ) );

		BEGIN_CYCLE_COUNTER
		write32( ARM_GPIO_GPSET0, (1 << LATCH_CONTROL) );
//		WAIT_UP_TO_CYCLE( 50 );
		WAIT_UP_TO_CYCLE( 60 );
		write32( ARM_GPIO_GPCLR0, (1 << LATCH_CONTROL) ); 
	}
}

 
#endif

 