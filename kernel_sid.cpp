/*
	__________               __________.___      _________.___________   
	\______   \_____    _____\______   \   |    /   _____/|   \______ \  
	 |       _/\__  \  /  ___/|     ___/   |    \_____  \ |   ||    |  \ 
	 |    |   \ / __ \_\___ \ |    |   |   |    /        \|   ||    `   \
	 |____|_  /(____  /____  >|____|   |___|   /_______  /|___/_______  /
			\/      \/     \/                          \/             \/ 


 kernel_sid.cpp

 RasPIC64 - A framework for interfacing the C64 and a Raspberry Pi 3B/3B+
          - RasPI SID: a SID and SFX Sound Expander Emulation 
		    (using reSID by Dag Lem and FMOPL by Jarek Burczynski, Tatsuyuki Satoh, Marco van den Heuvel, and Acho A. Tang)
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

// define this if you use a RaspberryPi 3B+
#define TIMINGS_RPI3B_PLUS

#include "kernel_sid.h"

//                 _________.___________         ____                      ________    ______  ____________  
//_______   ____  /   _____/|   \______ \       /  _ \       ___.__. _____ \_____  \  /  __  \/_   \_____  \ 
//\_  __ \_/ __ \ \_____  \ |   ||    |  \      >  _ </\    <   |  |/     \  _(__  <  >      < |   |/  ____/ 
// |  | \/\  ___/ /        \|   ||    `   \    /  <_\ \/     \___  |  Y Y  \/       \/   --   \|   /       \ 
// |__|    \___  >_______  /|___/_______  /    \_____\ \     / ____|__|_|  /______  /\______  /|___\_______ \
//             \/        \/             \/            \/     \/          \/       \/        \/             \/
#include "resid/sid.h"
using namespace reSID;

u32 CLOCKFREQ = 985248;	// exact clock frequency of the C64 will be measured at start up


#define NUM_SIDS 2
SID *sid[ NUM_SIDS ];

#ifdef EMULATE_OPL2
FM_OPL *pOPL;
u32 fmOutRegister;
#endif

// a ring buffer storing SID-register writes (filled in FIQ handler)
// TODO should be much smaller
#define RING_SIZE (1024*128)
u32 ringBufGPIO[ RING_SIZE ];
unsigned long long ringTime[ RING_SIZE ];
u32 ringWrite;

// prepared GPIO output when SID-registers are read
u32 outRegisters[ 32 ];

// counts the #cycles when the C64-reset line is pulled down (to detect a reset)
u32 resetCounter;

//  __     __                __      ___                   ___ 
// /__` | |  \     /\  |\ | |  \    |__   |\/|    | |\ | |  |  
// .__/ | |__/    /~~\ | \| |__/    |     |  |    | | \| |  |  
//                                                            
void initSID()
{
	resetCounter = 0;

	for ( int i = 0; i < NUM_SIDS; i++ )
	{
		sid[ i ] = new SID;

		for ( int j = 0; j < 24; j++ )
			sid[ i ]->write( j, 0 );

		if ( SID_MODEL[ i ] == 6581 )
		{
			sid[ i ]->set_chip_model( MOS6581 );
		} else
		{
			sid[ i ]->set_chip_model( MOS8580 );
			if ( SID_DigiBoost[ i ] == 0 )
			{
				sid[ i ]->set_voice_mask( 0x07 );
				sid[ i ]->input( 0 );
			} else
			{
				sid[ i ]->set_voice_mask( 0x0f );
				sid[ i ]->input( -32768 );
			}
		}
	}

#ifdef EMULATE_OPL2
	pOPL = ym3812_init( 3579545, SAMPLERATE );
	ym3812_reset_chip( pOPL );
#endif

	// ring buffer init
	ringWrite = 0;
	for ( int i = 0; i < RING_SIZE; i++ )
		ringTime[ i ] = 0;
}


unsigned long long cycleCountC64;


boolean CKernel::Initialize( void )
{
	boolean bOK = TRUE;

#ifdef USE_HDMI_VIDEO
	if ( bOK ) bOK = m_Screen.Initialize();

	if ( bOK )
	{
		CDevice *pTarget = m_DeviceNameService.GetDevice( m_Options.GetLogDevice(), FALSE );
		if ( pTarget == 0 )
			pTarget = &m_Screen;

		bOK = m_Logger.Initialize( pTarget );
	}
#endif

	if ( bOK ) bOK = m_Interrupt.Initialize();
	if ( bOK ) bOK = m_Timer.Initialize();
#ifdef USE_VCHIQ_SOUND
	if ( bOK ) bOK = m_VCHIQ.Initialize();
#endif

	// initialize ARM cycle counters (for accurate timing)
	initCycleCounter();

	// initialize GPIOs
	gpioInit();
	SET_BANK2_OUTPUT

	// initialize latch and software I2C buffer
	#ifdef USE_LATCH_OUTPUT
	initLatch();

	setLatch( LATCH_EXROM );
	setLatch( LATCH_GAME );
	outputLatch();

	#ifdef USE_OLED
	// I know this is a gimmick, but I couldn't resist ;-)
	splashScreen( raspi_sid_splash );
	#endif
	#endif

	return bOK;
}


void CKernel::Run( void )
{
	m_Logger.Write( "", LogNotice, "initialize SIDs..." );
	initSID();

	//
	// setup FIQ
	//
	m_InputPin.ConnectInterrupt( this->FIQHandler, this );
	m_InputPin.EnableInterrupt( GPIOInterruptOnRisingEdge );

	cycleCountC64 = 0;
	while ( cycleCountC64 < 10 ) 
	{
		m_Scheduler.MsSleep( 100 );
	}


	//
	// measure clock rate of the C64 (more accurate syncing with emulation, esp. for HDMI output)
	//
	cycleCountC64 = 0;
	unsigned long long startTime = m_Timer.GetClockTicks();
	unsigned long long curTime;

	do {
		curTime = m_Timer.GetClockTicks();
	} while ( curTime - startTime < 1000000 );

	unsigned long long clockFreq = cycleCountC64 * 1000000 / ( curTime - startTime );
	CLOCKFREQ = clockFreq;
	m_Logger.Write( "", LogNotice, "Measured C64 clock frequency: %u Hz", (u32)CLOCKFREQ );

	for ( int i = 0; i < NUM_SIDS; i++ )
		sid[ i ]->set_sampling_parameters( CLOCKFREQ, SAMPLE_INTERPOLATE, SAMPLERATE );

	//
	// initialize sound output (either PWM which is output in the FIQ handler, or via HDMI)
	//
	initSoundOutput( &m_pSound, &m_VCHIQ );

	m_Logger.Write( "", LogNotice, "start emulating..." );
	cycleCountC64 = 0;

	unsigned long long nCyclesEmulated = 0;
	unsigned long long samplesElapsed = 0;

	// how far did we consume the commands in the ring buffer?
	unsigned int ringRead = 0;

	// new main loop mainloop
	while ( true )
	{
		if ( resetCounter > 3 )
		{
			resetCounter = 0;
			for ( int i = 0; i < NUM_SIDS; i++ )
				for ( int j = 0; j < 24; j++ )
					sid[ i ]->write( j, 0 );

			#ifdef EMULATE_OPL2
			ym3812_reset_chip( pOPL );
			#endif
		}

	#ifdef USE_OLED
		static u32 renderDone = 0;
		if ( bufferEmptyI2C() && renderDone )
		{
			sendFramebuffer();
			renderDone = 0;
		}
	#endif

	#ifndef EMULATION_IN_FIQ

		#ifndef USE_PWM_DIRECT
		static u32 nSamplesInThisRun = 0;
		#endif

		unsigned long long cycleCount = cycleCountC64;
		while ( cycleCount > nCyclesEmulated )
		{
		#ifndef USE_PWM_DIRECT
			static int start = 0;
			if ( nSamplesInThisRun > 2205 / 8 )
			{
				if ( !start )
				{
					m_pSound->Start();
					start = 1;
				} else
				{
					//m_Scheduler.MsSleep( 1 );
					m_Scheduler.Yield();
				}
				nSamplesInThisRun = 0;
			}
			nSamplesInThisRun++;
		#endif

			unsigned long long samplesElapsedBefore = samplesElapsed;

			do { // do SID emulation until time passed to create an additional sample (i.e. there may be several cycles until a sample value is created)
				#ifdef USE_PWM_DIRECT
				u32 cyclesToEmulate = 8;
				#else			
				u32 cyclesToEmulate = 2;
				#endif
				sid[ 0 ]->clock( cyclesToEmulate );
				#ifndef SID2_DISABLED
				sid[ 1 ]->clock( cyclesToEmulate );
				#endif

				outRegisters[ 27 ] = encodeGPIO( sid[ 0 ]->read( 27 ) );
				outRegisters[ 28 ] = encodeGPIO( sid[ 0 ]->read( 28 ) );

				nCyclesEmulated += cyclesToEmulate;

				// apply register updates (we do one-cycle emulation steps, but in case we need to catch up...)
				unsigned int readUpTo = ringWrite;

				if ( ringRead != readUpTo && nCyclesEmulated >= ringTime[ ringRead ] )
				{
					unsigned char A, D;
					decodeGPIO( ringBufGPIO[ ringRead ], &A, &D );

					#ifdef EMULATE_OPL2
					if ( ringBufGPIO[ ringRead ] & bIO2 )
					{
						if ( ( ( A & ( 1 << 4 ) ) == 0 ) )
							ym3812_write( pOPL, 0, D ); else
							ym3812_write( pOPL, 1, D );
					} else
					#endif
					#if !defined(SID2_DISABLED) && !defined(SID2_PLAY_SAME_AS_SID1)
					// TODO: generic masks
					if ( ringBufGPIO[ ringRead ] & SID2_MASK )
					{
						sid[ 1 ]->write( A & 31, D );
					} else
					#endif
					{
						sid[ 0 ]->write( A & 31, D );
						outRegisters[ A & 31 ] = encodeGPIO( D );
						#if !defined(SID2_DISABLED) && defined(SID2_PLAY_SAME_AS_SID1)
						sid[ 1 ]->write( A & 31, D );
						#endif
					}

					ringRead++;
					ringRead &= ( RING_SIZE - 1 );
				}

				samplesElapsed = ( ( unsigned long long )nCyclesEmulated * ( unsigned long long )SAMPLERATE ) / ( unsigned long long )CLOCKFREQ;

			} while ( samplesElapsed == samplesElapsedBefore );

			s16 val1 = sid[ 0 ]->output();
			s16 val2 = 0;
			s16 valOPL = 0;

		#ifndef SID2_DISABLED
			val2 = sid[ 1 ]->output();
		#endif

		#ifdef EMULATE_OPL2
			ym3812_update_one( pOPL, &valOPL, 1 );
			// TODO asynchronous read back is an issue, needs to be fixed
			fmOutRegister = encodeGPIO( ym3812_read( pOPL, 0 ) ); 
		#endif

			//
			// mixer
			//
			s32 left, right;

			#ifdef MIXER_MONO
			left = right = ( (s32)val1 + (s32)val2 + (s32)valOPL ) / 3;
			#endif
			#ifdef MIXER_SID_STEREO
			#ifdef EMULATE_OPL2
			left  = ( (s32)val1 + (s32)valOPL / 2 ) * 2 / 3;
			right = ( (s32)val2 + (s32)valOPL / 2 ) * 2 / 3;
			#else
			left  = (s32)val1;
			right = (s32)val2;
			#endif
			#endif

			#ifdef USE_PWM_DIRECT
			putSample( left, right );
			#else
			putSample( left );
			putSample( right );
			#endif

			// ugly code which renders 3 oscilloscopes (SID1, SID2, FM) to HDMI and 1 for the OLED
			#include "oscilloscope_hack.h"
		}
	#endif
	}

	m_InputPin.DisableInterrupt();
}


void CKernel::FIQHandler( void *pParam )
{
	u32 g2;

	BEGIN_CYCLE_COUNTER

	static u32 latchDelayOut = 10;

	// wait >= 25ns after FIQ start
	//WAIT_UP_TO_CYCLE( 45 );
	#ifndef TIMINGS_RPI3B_PLUS
	WAIT_UP_TO_CYCLE( 45 );
	#else
	WAIT_UP_TO_CYCLE( 90 );
	#endif

	// get A0-A7, IO1, IO2, ...
	g2 = read32( ARM_GPIO_GPLEV0 );

	// block wrong executions
	if ( !( g2 & bPHI ) ) return;

	if ( !( g2 & bRESET ) ) resetCounter ++;

	cycleCountC64 ++;

	// optionally: switch to A8..12 if we'd need more address lines
	// attention: needs switching back before the FIQ handler is left
	// write32( ARM_GPIO_GPSET0, 1 << DIR_CTRL_257 ); 
	 
	// preload cache
	CACHE_PRELOAD( &ringWrite );
	CACHE_PRELOAD( &outRegisters[ 0 ] );
	CACHE_PRELOAD( &outRegisters[ 16 ] );

	// optionally: read A8..12 if we'd need more address lines
	//	WAIT_UP_TO_CYCLE( 220 );
	//	g3 = read32( ARM_GPIO_GPLEV0 );

	//  __   ___       __      __     __  
	// |__) |__   /\  |  \    /__` | |  \ 
	// |  \ |___ /~~\ |__/    .__/ | |__/ 
	//
	if ( !( g2 & bCS ) && ( g2 & bRW ) )
	{
		latchDelayOut ++;

		u32 A = ( g2 >> A0 ) & 31;
		u32 D = outRegisters[ A ];

		write32( ARM_GPIO_GPSET0, D );
		write32( ARM_GPIO_GPCLR0, ( D_FLAG & ( ~D ) ) | ( 1 << GPIO_OE ) );

		#ifndef TIMINGS_RPI3B_PLUS
		WAIT_UP_TO_CYCLE( 690 );
		#else
		WAIT_UP_TO_CYCLE( 805 );
		#endif

		// disable 74LV245 
		write32( ARM_GPIO_GPSET0, ( 1 << GPIO_OE ) );

		return;
	} else
	//  __   ___       __      ___       
	// |__) |__   /\  |  \    |__   |\/| 
	// |  \ |___ /~~\ |__/    |     |  | 
	//                                   
	#ifdef EMULATE_OPL2
	if ( ( ( g2 & bRW ) && !( g2 & bIO2 ) ) &&  
 	     ( ( ( g2 >> A0 ) & 255 ) == 0x60 ) )
	{
		//
		// this is not a real read of the YM3812 status register!
		// only a fake that let's the detection routine be satisfied
		//
		static u32 fmFakeOutput = 0;
		u32 D = encodeGPIO( fmFakeOutput ); 
		fmFakeOutput = 0xc0 - fmFakeOutput;

		write32( ARM_GPIO_GPSET0, D_FLAG & D );
		write32( ARM_GPIO_GPCLR0, ( D_FLAG & ( ~D ) ) | ( 1 << GPIO_OE ) );

		#ifndef TIMINGS_RPI3B_PLUS
		WAIT_UP_TO_CYCLE( 690 );
		#else
		WAIT_UP_TO_CYCLE( 805 );
		#endif

		// disable 74LV245 
		write32( ARM_GPIO_GPSET0, ( 1 << GPIO_OE ) );
		return;
	} else
	//       __    ___  ___     ___       
	// |  | |__) |  |  |__     |__   |\/| 
	// |/\| |  \ |  |  |___    |     |  | 
	//                                    
	if ( !( g2 & bRW ) && !( g2 & bIO2 ) ) 
	{
		// set bank 2 GPIOs to input (D0-D7)
		SET_BANK2_INPUT 

		// enable 74LVC245
		write32( ARM_GPIO_GPCLR0, ( 1 << GPIO_OE ) );

		#ifndef TIMINGS_RPI3B_PLUS
		WAIT_UP_TO_CYCLE( 500 );
		#else
		WAIT_UP_TO_CYCLE( 560 );
		#endif

		u32 g1 = read32( ARM_GPIO_GPLEV0 );

		// disable 74LV245
		write32( ARM_GPIO_GPSET0, 1 << GPIO_OE ); 

		SET_BANK2_OUTPUT 

		ringBufGPIO[ ringWrite ] = ( g2 & A_FLAG ) | ( g1 & D_FLAG ) | bIO2;
		ringTime[ ringWrite ] = cycleCountC64;
		ringWrite ++;
		ringWrite &= ( RING_SIZE - 1 );
		return;
	} else
	#endif // EMULATE_OPL2
	//       __    ___  ___     __     __  
	// |  | |__) |  |  |__     /__` | |  \ 
	// |/\| |  \ |  |  |___    .__/ | |__/ 
	//                                   
	if ( !( g2 & bCS ) && !( g2 & bRW ) )
	{
		latchDelayOut ++;
		
		// set bank 2 GPIOs to input (D0-D7)
		SET_BANK2_INPUT 

		// enable 74LVC245
		write32( ARM_GPIO_GPCLR0, ( 1 << GPIO_OE ) );

		// wait until ... ns after FIQ start
		#ifndef TIMINGS_RPI3B_PLUS
		WAIT_UP_TO_CYCLE( 500 );
		#else
		WAIT_UP_TO_CYCLE( 570 );
		#endif

		// read D0..D7
		u32 g1 = read32( ARM_GPIO_GPLEV0 );

		// disable 74LV245
		write32( ARM_GPIO_GPSET0, ( 1 << GPIO_OE ) ); 

		SET_BANK2_OUTPUT 

		ringBufGPIO[ ringWrite ] = ( ( g2 & (A_FLAG|SID2_MASK) ) | ( g1 & D_FLAG ) ) & ~bIO2;
		ringTime[ ringWrite ] = cycleCountC64;
		ringWrite ++;
		ringWrite &= ( RING_SIZE - 1 );
		
		// optionally we could directly set the SID-output registers (instead of where the emulation runs)
		//u32 A = ( g2 >> A0 ) & 31;
		//outRegisters[ A ] = g1 & D_FLAG;
		return;
	}

	//  ___                      ___    __                     ___    __  
	// |__   |\/| |  | |     /\   |  | /  \ |\ |    | |\ |    |__  | /  \ 
	// |___  |  | \__/ |___ /~~\  |  | \__/ | \|    | | \|    |    | \__X 
	// OPTIONAL and omitted for this release
	//																	
	#ifdef EMULATION_IN_FIQ
	run_emulation:
	#include "fragment_emulation_in_fiq.h"
	#endif		

	//  __                 __       ___  __       ___ 
	// |__) |  |  |\/|    /  \ |  |  |  |__) |  |  |  
	// |    |/\|  |  |    \__/ \__/  |  |    \__/  |  
	// OPTIONAL
	//											
	#ifdef USE_PWM_DIRECT
	static unsigned long long samplesElapsedBeforeFIQ = 0;

	unsigned long long samplesElapsedFIQ = ( ( unsigned long long )cycleCountC64 * ( unsigned long long )SAMPLERATE ) / ( unsigned long long )CLOCKFREQ;

	if ( samplesElapsedFIQ != samplesElapsedBeforeFIQ )
	{
		samplesElapsedBeforeFIQ = samplesElapsedFIQ;

		u32 s = getSample();
		u16 s1 = s & 65535;
		u16 s2 = s >> 16;

		s32 d1 = ( ( *(s16*)&s1 + 32768 ) * PWMRange ) >> 16;
		s32 d2 = ( ( *(s16*)&s2 + 32768 ) * PWMRange ) >> 16;
		write32( ARM_PWM_DAT1, d1 );
		write32( ARM_PWM_DAT2, d2 );
	} 
	#endif

	//           ___  __       
	// |     /\   |  /  ` |__| 
	// |___ /~~\  |  \__, |  | 
	//
	#ifdef USE_LATCH_OUTPUT
	if ( --latchDelayOut == 0 )
	{
		latchDelayOut = 2;
		outputLatch();
	}
	#endif
}


int main( void )
{
	CKernel kernel;
	if ( kernel.Initialize() )
		kernel.Run();

	halt();
	return EXIT_HALT;
}
