/*
	__________               __________.___      _________.___________   
	\______   \_____    _____\______   \   |    /   _____/|   \______ \  
	 |       _/\__  \  /  ___/|     ___/   |    \_____  \ |   ||    |  \ 
	 |    |   \ / __ \_\___ \ |    |   |   |    /        \|   ||    `   \
	 |____|_  /(____  /____  >|____|   |___|   /_______  /|___/_______  /
			\/      \/     \/                          \/             \/ 


 sound.cpp

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
#include "kernel_sid.h"

#define WRITE_CHANNELS		2		// 1: Mono, 2: Stereo
#define QUEUE_SIZE_MSECS 	50		// size of the sound queue in milliseconds duration
#define CHUNK_SIZE			2000	// number of samples, written to sound device at once

#define FORMAT		SoundFormatSigned16
#define TYPE		s16
#define TYPE_SIZE	sizeof (s16)
#define FACTOR		((1 << 15)-1)
#define NULL_LEVEL	0

// forward declarations
void initPWMOutput();
void cbSound( void *d );
void clearSoundBuffer();

void initSoundOutput( CSoundBaseDevice **m_pSound, CVCHIQDevice *m_VCHIQ )
{
	clearSoundBuffer();

#ifdef USE_PWM_DIRECT
	initPWMOutput();
#else
	if ( m_pSound == NULL || m_VCHIQ == NULL )
		return;

	//( *m_pSound ) = new CPWMSoundBaseDevice( &m_Interrupt, SAMPLERATE, CHUNK_SIZE );
	( *m_pSound ) = new CVCHIQSoundBaseDevice( m_VCHIQ, SAMPLERATE, CHUNK_SIZE, VCHIQSoundDestinationHDMI );
	( *m_pSound )->AllocateQueue( QUEUE_SIZE_MSECS );
	( *m_pSound )->SetWriteFormat( FORMAT, WRITE_CHANNELS );
	( *m_pSound )->RegisterNeedDataCallback( cbSound, (void*)( *m_pSound ) );

	PCMCountLast = PCMCountCur = 0;
	for ( u32 i = 0; i < QUEUE_SIZE_MSECS * SAMPLERATE / 1000 * 3 / 2; i++ )
	{
		putSample( 0 );
		putSample( 0 );
	}
#endif
}

// __________  __      __  _____         _________                        .___
// \______   \/  \    /  \/     \       /   _____/ ____  __ __  ____    __| _/
//  |     ___/\   \/\/   /  \ /  \      \_____  \ /  _ \|  |  \/    \  / __ | 
//  |    |     \        /    Y    \     /        (  <_> )  |  /   |  \/ /_/ | 
//  |____|      \__/\  /\____|__  /    /_______  /\____/|____/|___|  /\____ | 
//                   \/         \/             \/                  \/      \/ 

// for PWM Output
//
s32 sampleBuffer[ 128 ];
u32 smpLast, smpCur;

#ifdef USE_PWM_DIRECT

#define CLOCK_FREQ			500000000
#define CLOCK_DIVIDER		2 

// PWM control register
#define ARM_PWM_CTL_PWEN1	(1 << 0)
#define ARM_PWM_CTL_MODE1	(1 << 1)
#define ARM_PWM_CTL_RPTL1	(1 << 2)
#define ARM_PWM_CTL_SBIT1	(1 << 3)
#define ARM_PWM_CTL_POLA1	(1 << 4)
#define ARM_PWM_CTL_USEF1	(1 << 5)
#define ARM_PWM_CTL_CLRF1	(1 << 6)
#define ARM_PWM_CTL_MSEN1	(1 << 7)
#define ARM_PWM_CTL_PWEN2	(1 << 8)
#define ARM_PWM_CTL_MODE2	(1 << 9)
#define ARM_PWM_CTL_RPTL2	(1 << 10)
#define ARM_PWM_CTL_SBIT2	(1 << 11)
#define ARM_PWM_CTL_POLA2	(1 << 12)
#define ARM_PWM_CTL_USEF2	(1 << 13)
#define ARM_PWM_CTL_MSEN2	(1 << 14)

// PWM status register
#define ARM_PWM_STA_FULL1	(1 << 0)
#define ARM_PWM_STA_EMPT1	(1 << 1)
#define ARM_PWM_STA_WERR1	(1 << 2)
#define ARM_PWM_STA_RERR1	(1 << 3)
#define ARM_PWM_STA_GAPO1	(1 << 4)
#define ARM_PWM_STA_GAPO2	(1 << 5)
#define ARM_PWM_STA_GAPO3	(1 << 6)
#define ARM_PWM_STA_GAPO4	(1 << 7)
#define ARM_PWM_STA_BERR	(1 << 8)
#define ARM_PWM_STA_STA1	(1 << 9)
#define ARM_PWM_STA_STA2	(1 << 10)
#define ARM_PWM_STA_STA3	(1 << 11)
#define ARM_PWM_STA_STA4	(1 << 12)

u32 PWMRange;

void initPWMOutput()
{
	CGPIOPin   *m_Audio1 = new CGPIOPin( GPIOPinAudioLeft, GPIOModeAlternateFunction0 );
	CGPIOPin   *m_Audio2 = new CGPIOPin( GPIOPinAudioRight, GPIOModeAlternateFunction0 );
	CGPIOClock *m_Clock  = new CGPIOClock( GPIOClockPWM, GPIOClockSourcePLLD );

	u32 nSampleRate = SAMPLERATE;
	PWMRange = ( CLOCK_FREQ / CLOCK_DIVIDER + nSampleRate / 2 ) / nSampleRate;

	PeripheralEntry();

	m_Clock->Start( CLOCK_DIVIDER );
	CTimer::SimpleusDelay( 2000 );

	write32( ARM_PWM_RNG1, PWMRange );
	write32( ARM_PWM_RNG2, PWMRange );

	u32 nControl = ARM_PWM_CTL_PWEN1 | ARM_PWM_CTL_PWEN2;
	write32( ARM_PWM_CTL, nControl );

	CTimer::SimpleusDelay( 2000 );

	PeripheralExit();
}
#endif

//   ___ ___________      _____  .___       _________                        .___
//  /   |   \______ \    /     \ |   |     /   _____/ ____  __ __  ____    __| _/
// /    ~    \    |  \  /  \ /  \|   |     \_____  \ /  _ \|  |  \/    \  / __ | 
// \    Y    /    `   \/    Y    \   |     /        (  <_> )  |  /   |  \/ /_/ | 
//  \___|_  /_______  /\____|__  /___|    /_______  /\____/|____/|___|  /\____ | 
//        \/        \/         \/                 \/                  \/      \/ 
#ifdef USE_VCHIQ_SOUND
short PCMBuffer[ PCMBufferSize ];
u32 PCMCountLast, PCMCountCur;

u32 samplesInBuffer()
{
	u32 nFrames;
	if ( PCMCountLast <= PCMCountCur )
		nFrames = PCMCountCur - PCMCountLast; else
		nFrames = PCMCountCur + PCMBufferSize - PCMCountLast; 
	return nFrames;
}

u32 samplesInBufferFree()
{
	return PCMBufferSize - 16 - samplesInBuffer();
}

bool pcmBufferFull()
{
	if ( ( (PCMCountCur+1) == (PCMCountLast) ) ||
		 ( PCMCountCur == (PCMBufferSize-1) && PCMCountLast == 0 ) )
		return true;

	return false;	     
}
#endif

//
// callback called when more samples are needed by the HDMI sound playback
// this code is incredibly ugly and inefficient (more memory copies than necessary)
//
#ifndef USE_PWM_DIRECT

short temp[ 65536 ];

//
// don't look too close... 
// this is more than ugly, but the version without this copying has a bug when reaching the end of the buffer
// presumably this happens when writing part 1 of two does not transfer all bytes and then we're loosing a frame 
// once this is fixed, call 2x m_pSound->Write instead of copying the data to temp
//
void cbSound( void *d )
{
	CSoundBaseDevice *m_pSound = (CSoundBaseDevice*)d;

	u32 nWriteFrames = samplesInBuffer();
	u32 nFramesNeeded = m_pSound->GetQueueSizeFrames() - m_pSound->GetQueueFramesAvail();
	u32 padding = 0;

	nWriteFrames = min( nWriteFrames, nFramesNeeded );

	if ( nFramesNeeded > nWriteFrames )
		padding = nFramesNeeded - nWriteFrames;

	// maybe we need to split writes
	u32 nWriteSplit[ 2 ];
	u32 nParts = 1;

	if ( nWriteFrames + PCMCountLast > PCMBufferSize )
	{
		nWriteSplit[ 0 ] = PCMBufferSize - PCMCountLast;
		nWriteSplit[ 1 ] = nWriteFrames - nWriteSplit[ 0 ];
		nParts = 2;
	}
	{
		nWriteSplit[ 0 ] = nWriteFrames;
		nWriteSplit[ 1 ] = 0;
	}

	u32 p = 0;
	short lastL = 0, lastR = 0;
	for ( u32 i = 0; i < nParts; i++ )
	{
		for ( u32 j = 0; j < nWriteSplit[ i ]; j++ )
		{
			lastL = PCMBuffer[ PCMCountLast++ ];
			temp[ p ++ ] = lastL;
			PCMCountLast %= PCMBufferSize;

			lastR = PCMBuffer[ PCMCountLast++ ];
			temp[ p ++ ] = lastR;
			PCMCountLast %= PCMBufferSize;
		}
	}
	for ( u32 i = 0; i < padding; i++ )
	{
		temp[ p ++ ] = lastL;
		temp[ p ++ ] = lastR;
	}

	unsigned nWriteBytes = p * TYPE_SIZE;
	m_pSound->Write( &temp[ 0 ], nWriteBytes );
}
#endif

void clearSoundBuffer()
{
#ifdef USE_PWM_DIRECT
	memset( sampleBuffer, 0, sizeof( u32 ) * 128 );
#else
	memset( PCMBuffer, 0, sizeof( short ) * PCMBufferSize );
#endif
}

