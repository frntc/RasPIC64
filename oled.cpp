//
// oled.cpp part of...
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
#include <circle/memio.h>
#include <circle/memory.h>
#include "oled.h"
#include "lowlevel_arm.h"
#include "latch.h"

u8 oledFrameBuffer[ 128 * 64 / 8 ];

void oledClear()
{
	memset( oledFrameBuffer, 0, 128 * 64 / 8 );
}

void sendFramebuffer()
{
	u32 j = 0;
	for ( int y = 0; y < 64 / 8; y++ )
	{
		ssd1306_setpos( 0, y );
		ssd1306_send_data_start();
		for ( int x = 0; x < 128; x++ )
			ssd1306_send_byte( oledFrameBuffer[ j++ ] );
		ssd1306_send_data_stop();
	}
}

void splashScreen( const u8 *fb )
{
	ssd1306_init();
	ssd1306_send_command( 0x81 ); // SSD1306_SETCONTRAST
	ssd1306_send_command( 0x9F ); // 0x9F or 0xCF
	ssd1306_send_command( 0x2E ); // SSD1306_DEACTIVATE_SCROLL

	for ( int y = 0; y < 64 / 8; y++ )
	{
		ssd1306_setpos( 0, y );
		ssd1306_send_data_start();
		for ( int x = 0; x < 128; x++ )
			ssd1306_send_byte( *fb++ );
		ssd1306_send_data_stop();
	}

	BEGIN_CYCLE_COUNTER
	while ( !bufferEmptyI2C() )
	{
		outputLatch();
		RESTART_CYCLE_COUNTER
		WAIT_UP_TO_CYCLE( 7000 )
	}
	write32( ARM_GPIO_GPCLR0, (1 << LATCH_CONTROL) ); 
}