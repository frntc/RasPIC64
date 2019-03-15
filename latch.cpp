//  .____            __         .__          ___                                ________          __                 __           ___    
//  |    |   _____ _/  |_  ____ |  |__      /  /   _____   ___________   ____   \_____  \  __ ___/  |_______  __ ___/  |_  ______ \  \   
//  |    |   \__  \\   __\/ ___\|  |  \    /  /   /     \ /  _ \_  __ \_/ __ \   /   |   \|  |  \   __\____ \|  |  \   __\/  ___/  \  \  
//  |    |___ / __ \|  | \  \___|   Y  \  (  (   |  Y Y  (  <_> )  | \/\  ___/  /    |    \  |  /|  | |  |_> >  |  /|  |  \___ \    )  ) 
//  |_______ (____  /__|  \___  >___|  /   \  \  |__|_|  /\____/|__|    \___  > \_______  /____/ |__| |   __/|____/ |__| /____  >  /  /  
//          \/    \/          \/     \/     \__\       \/                   \/          \/            |__|                    \/  /__/   
//
// latch.cpp part of...
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
#include "latch.h"

// current and last status of the data lines
// connected to the latch input (GPIO D0-D7 -> 74LVX573D D0-D7)
u32 latchD, latchClr, latchSet;
u32 latchDOld;

// a tiny ring buffer for simple I2C output via the latch
// (since we really do not have enough GPIOs available)
u8 i2cBuffer[ FAKE_I2C_BUF_SIZE ];
u32 i2cBufferCountLast, i2cBufferCountCur;

void initLatch()
{
	latchD = LATCH_RESET | LATCH_GAME | LATCH_EXROM;
	latchClr = latchSet = 0;
	latchDOld = 0xFFFFFFFF;
	i2cBufferCountLast = i2cBufferCountCur = 0;
	putI2CCommand( 0 );
}