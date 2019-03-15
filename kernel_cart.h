//
// kernel_cart.h
//
// RasPIC64 - A framework for interfacing the C64 and a Raspberry Pi 3B/3B+
//          - RasPI Cart: example how to implement a CBM80 cartridge
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
#ifndef _kernel_h
#define _kernel_h

// support output signals via the latch (if not used, you'd have to set EXROM and GAME manually!)
#define USE_LATCH_OUTPUT

// use the OLED connected to the latch
#define USE_OLED

#if defined(USE_OLED) && !defined(USE_LATCH_OUTPUT)
#define USE_LATCH_OUTPUT
#endif

#define USE_HDMI_VIDEO

#include <circle/startup.h>
#include <circle/bcm2835.h>
#include <circle/memio.h>
#include <circle/memory.h>
#include <circle/koptions.h>
#include <circle/devicenameservice.h>
#include <circle/screen.h>
#include <circle/interrupt.h>
#include <circle/timer.h>
#include <circle/logger.h>
#include <circle/sched/scheduler.h>
#include <circle/types.h>
#include <circle/gpioclock.h>
#include <circle/gpiopin.h>
#include <circle/gpiopinfiq.h>
#include <circle/gpiomanager.h>
#include <circle/util.h>

#include "lowlevel_arm.h"
#include "gpio_defs.h"
#include "latch.h"

#ifdef USE_OLED
#include "oled.h"
#include "splash_raspicart.h"
#endif

class CKernel
{
public:
	CKernel( void )
		: m_CPUThrottle( CPUSpeedMaximum ),
	#ifdef USE_HDMI_VIDEO
		m_Screen( m_Options.GetWidth(), m_Options.GetHeight() ),
	#endif
		m_Timer( &m_Interrupt ),
		m_Logger( m_Options.GetLogLevel(), &m_Timer ),
		m_InputPin( PHI2, GPIOModeInput, &m_Interrupt )
	{
	}

	~CKernel( void )
	{
	}

	boolean Initialize( void );

	void Run( void );

private:
	static void FIQHandler( void *pParam );

	// do not change this order
	CMemorySystem		m_Memory;
	CKernelOptions		m_Options;
	CDeviceNameService	m_DeviceNameService;
	CCPUThrottle		m_CPUThrottle;
#ifdef USE_HDMI_VIDEO
	CScreenDevice		m_Screen;
#endif
	CInterruptSystem	m_Interrupt;
	CTimer				m_Timer;
	CLogger				m_Logger;
	CScheduler			m_Scheduler;
	CGPIOPinFIQ			m_InputPin;
};

#endif
