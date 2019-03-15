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
#ifndef _lowlevel_arm_h
#define _lowlevel_arm_h

#define AA __attribute__ ((aligned (64)))

#define BEGIN_CYCLE_COUNTER \
						  		unsigned long armCycleCounter asm ("r9"); \
								armCycleCounter = 0; \
								asm volatile ("MRC p15, 0, %0, c9, c13, 0\t\n": "=r"(armCycleCounter));  

#define RESTART_CYCLE_COUNTER \
								asm volatile ("MRC p15, 0, %0, c9, c13, 0\t\n": "=r"(armCycleCounter));  

#define READ_CYCLE_COUNTER( cc ) \
								asm volatile ("MRC p15, 0, %0, c9, c13, 0\t\n": "=r"(cc));  


#define WAIT_UP_TO_CYCLE( wc ) { \
								unsigned long cc2  asm ("r10"); \
								do { \
									asm volatile ("MRC p15, 0, %0, c9, c13, 0\t\n": "=r"(cc2)); \
								} while ( (cc2-armCycleCounter) < (wc) ); }

#define WAIT_UP_TO_CYCLE_AFTER( wc, cc ) { \
								unsigned long cc2  asm ("r11"); \
								do { \
									asm volatile ("MRC p15, 0, %0, c9, c13, 0\t\n": "=r"(cc2)); \
								} while ( (cc2-cc) < (wc) ); }

#define CACHE_PRELOAD( ptr ) { asm volatile ("pld\t[%0]" :: "r" (ptr)); }


void initCycleCounter();

#endif

 