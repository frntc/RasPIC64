// lowlevel_arm.h part of...
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
#include "lowlevel_arm.h"

// initialize what we need for the performance counters
void initCycleCounter()
{
	int flag = 1;

	asm volatile ( "mcr p15, 0, %0, c9, c14, 0" :: "r" ( 1 ) );

	flag |= 2; // reset all counters to 0
	flag |= 4; // reset cycle counter to 0
	flag |= 16;

	asm volatile ( "MCR p15, 0, %0, c9, c12, 0\t\n" :: "r"( flag ) );		// program performance counter control register
	asm volatile ( "MCR p15, 0, %0, c9, c12, 1\t\n" :: "r"( 0x8000000f ) );	// enable all counters
	asm volatile ( "MCR p15, 0, %0, c9, c12, 3\t\n" :: "r"( 0x80000001 ) );	// clear overflows
	asm volatile ( "MCR p15, 0, %0, c9, c12, 5\t\n" :: "r"( 0x00 ) );		// select counter 0
	asm volatile ( "MCR p15, 0, %0, c9, c13, 1\t\n" :: "r"( 0xD ) );		// write event (0x11 = cycle count)
}

