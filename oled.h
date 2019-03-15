//
// oled.h part of...
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
#include <circle/types.h>
#include <circle/util.h>

typedef unsigned char		u8;
typedef unsigned short		u16;

extern void ssd1306_init( void );
extern void ssd1306_fill( u8 p );
extern void ssd1306_setpos( u8 x, u8 y );
extern void ssd1306_fill4( u8, u8, u8, u8 );
extern void ssd1306_fill2( u8 p1, u8 p2 );
extern void ssd1306_fillscreen( u8 fill );
extern void ssd1306_char_font6x8( char ch );
extern void ssd1306_string_font6x8( char *s );
extern void ssd1306_numdec_font6x8( u16 num );
extern void ssd1306_numdecp_font6x8( u16 num );
extern void ssd1306_draw_bmp( u8 x0, u8 y0, u8 x1, u8 y1, const u8 bitmap[] );
extern void ssd1306_send_byte( u8 byte );
extern void ssd1306_send_command( u8 command );
extern void ssd1306_send_data_start( void );
extern void ssd1306_send_data_stop( void );

extern u8 oledFrameBuffer[ 128 * 64 / 8 ];

static inline void oledSetPixel( u32 x, u32 y )
{
	oledFrameBuffer[ x + ( y / 8 ) * 128 ] |= ( 1 << ( y & 7 ) );
}

static inline void oledClearPixel( u32 x, u32 y )
{
	oledFrameBuffer[ x + ( y / 8 ) * 128 ] &= ~( 1 << ( y & 7 ) );
}

extern void oledClear();
extern void sendFramebuffer();
extern void splashScreen( const u8 *fb );
