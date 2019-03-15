/*
 oscilloscope_hack.cpp

 RasPIC64 - A framework for interfacing the C64 and a Raspberry Pi 3B/3B+
          - RasPI SID: a SID and SFX Sound Expander Emulation 
		    (using reSID by Dag Lem and FMOPL by Jarek Burczynski, Tatsuyuki Satoh, Marco van den Heuvel, and Acho A. Tang)
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
{

#ifdef USE_HDMI_VIDEO

#define COLOR0 COLOR16 (10, 31, 20)
#define COLOR1 COLOR16 (10, 20, 31)
#define COLOR2 COLOR16 (31, 15, 15)

static u32 scopeValue[ 3 ][ 512 ];
static u32 scopeX = 0;
static u32 scopeUpdate = 0;

if ( ( scopeUpdate++ & 3 ) == 0 )
{
	scopeX++;
	scopeX &= 511;
	u32 x = scopeX + 200;

	u32 y = 400 + ( val1 >> 8 );
	m_Screen.SetPixel( x, scopeValue[ 0 ][ scopeX ], 0 );
	m_Screen.SetPixel( x, y, COLOR0 );
	scopeValue[ 0 ][ scopeX ] = y;

#ifndef SID2_DISABLED
	y = 528 + ( val2 >> 8 );
	m_Screen.SetPixel( x, scopeValue[ 1 ][ scopeX ], 0 );
	m_Screen.SetPixel( x, y, COLOR1 );
	scopeValue[ 1 ][ scopeX ] = y;
#endif

#ifdef EMULATE_OPL2
	y = 528 + 128 + ( valOPL >> 8 );
	m_Screen.SetPixel( x, scopeValue[ 2 ][ scopeX ], 0 );
	m_Screen.SetPixel( x, y, COLOR2 );
	scopeValue[ 2 ][ scopeX ] = y;
#endif
}

#endif

#ifdef USE_OLED
static u32 scopeXOLED = 0;
static u32 scopeUpdateOLED = 0;
renderDone = 0;
if ( ( scopeUpdateOLED++ & 3 ) == 0 )
{
	scopeXOLED++;
	scopeXOLED &= 127;
	if ( scopeXOLED == 0 )
		memcpy( oledFrameBuffer, raspi_sid_splash, 128 * 64 / 8 );

	s32 y = 32 + min( 29, max( -29, (left+right) / 2 / 192 ) );
	oledSetPixel( scopeXOLED, y );
	oledClearPixel( scopeXOLED, y - 2 );
	oledClearPixel( scopeXOLED, y - 1 );
	oledClearPixel( scopeXOLED, y + 1 );
	oledClearPixel( scopeXOLED, y + 2 );

	if ( scopeXOLED == 127 )
		renderDone = 1;
}
#endif

}