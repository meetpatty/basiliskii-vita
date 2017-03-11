/*
 *  audio_psp.cpp - Audio support, PSP implementation
 *
 *  Basilisk II (C) 1997-2005 Christian Bauer
 *  Portions written by Marc Hellwig
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */


#include <pspkernel.h>
#include <pspdebug.h>
#include <pspaudio.h>

#include "sysdeps.h"

#include "cpu_emulation.h"
#include "main.h"
#include "prefs.h"
#include "user_strings.h"
#include "audio.h"
#include "audio_defs.h"

#define DEBUG 0
#include "debug.h"


// Global variables
static SceUID audio_irq_done_sem;	// Signal from interrupt to streaming thread: data block read
static SceUID bufferEmpty;

static int sound_channel = PSP_AUDIO_NEXT_CHANNEL;
static volatile int sound_volume = PSP_AUDIO_VOLUME_MAX;
static volatile uint32 sound_status = 0;

static volatile int pcmflip = 0;
static int16 __attribute__((aligned(16))) pcmout1[4096 * 2]; // 4096 stereo samples
static int16 __attribute__((aligned(16))) pcmout2[4096 * 2];

static int audio_block_size;


/*
 * Audio Threads
 *
 */

int fillBuffer(SceSize args, void *argp)
{
	int16 *fillbuf;

	while(sound_status != 0xDEADBEEF)
	{
		sceKernelWaitSema(bufferEmpty, 1, 0);
		fillbuf = pcmflip ? pcmout2 : pcmout1;
		if (AudioStatus.num_sources)
		{
			// Trigger audio interrupt to get new buffer
			D(bug("stream: triggering irq\n"));
			SetInterruptFlag(INTFLAG_AUDIO);
			TriggerInterrupt();
			D(bug("stream: waiting for ack\n"));
			sceKernelWaitSema(audio_irq_done_sem, 1, 0);
			//sem_wait(&audio_irq_done_sem);
			D(bug("stream: ack received\n"));

			// Get size of audio data
			uint32 apple_stream_info = ReadMacInt32(audio_data + adatStreamInfo);
			if (apple_stream_info)
			{
				int work_size = ReadMacInt32(apple_stream_info + scd_sampleCount) * (AudioStatus.sample_size >> 3) * AudioStatus.channels;
				D(bug("stream: work_size %d\n", work_size));
				if (work_size > audio_block_size)
					work_size = audio_block_size; // safety check - should never mix more than audio_frames_per_block
				int16 *p = (int16 *)Mac2HostAddr(ReadMacInt32(apple_stream_info + scd_buffer));
				for (int i=0; i<work_size/2; i++)
					fillbuf[i] = ntohs(p[i]);
				if (work_size < audio_block_size)
					memset((char *)fillbuf+work_size, 0, audio_block_size-work_size); // last block - clear to end of buffer
				D(bug("stream: data written\n"));
			}
			 else
				memset((char *)fillbuf, 0, audio_block_size);
		}
		else
		{
			// Audio not active, play silence
			memset((char *)fillbuf, 0, audio_block_size);
		}
	}
	sceKernelExitDeleteThread(0);
	return 0;
}

int audioOutput(SceSize args, void *argp)
{
	int16 *playbuf;

	while(sound_status != 0xDEADBEEF)
	{
		playbuf = pcmflip ? pcmout1 : pcmout2;
		pcmflip ^= 1;
		sceKernelSignalSema(bufferEmpty, 1);
		sceAudioOutputBlocking(sound_channel, sound_volume, playbuf);
	}
	sceKernelExitDeleteThread(0);
	return 0;
}

/*
 *  Initialization
 *
 */

// Set AudioStatus to reflect current audio stream format
static void set_audio_status_format(void)
{
	AudioStatus.sample_rate = audio_sample_rates[0];
	AudioStatus.sample_size = audio_sample_sizes[0];
	AudioStatus.channels = audio_channel_counts[0];
}

void AudioInit(void)
{
	// Init audio status and feature flags
	audio_sample_rates.push_back(44100 << 16);
	audio_sample_sizes.push_back(16);
	audio_channel_counts.push_back(2);
	set_audio_status_format();
	AudioStatus.mixer = 0;
	AudioStatus.num_sources = 0;
	audio_component_flags = cmpWantsRegisterMessage | kStereoOut | k16BitOut;

	// Sound disabled in prefs? Then do nothing
	if (PrefsFindBool("nosound"))
		return;

	// Init semaphores
	bufferEmpty = sceKernelCreateSema("Buffer Empty", 0, 1, 1, 0);
	audio_irq_done_sem = sceKernelCreateSema("Audio IRQ Done", 0, 0, 1, 0);

	// reserve audio channel
	audio_frames_per_block = 4096;
	audio_block_size = (AudioStatus.sample_size >> 3) * AudioStatus.channels * audio_frames_per_block;
	D(bug("AudioInit: block size %d\n", block_size));
	sound_channel = sceAudioChReserve(sound_channel, audio_frames_per_block, PSP_AUDIO_FORMAT_STEREO);

	sound_status = 0; // threads running

	// create audio playback thread to provide timing
	int audioThid = sceKernelCreateThread("audioOutput", audioOutput, 0x16, 0x1800, PSP_THREAD_ATTR_USER, NULL);
	if(audioThid < 0)
	{
		printf("FATAL: Cannot create audioOutput thread\n");
		return; // no audio
	}
	sceKernelStartThread(audioThid, 0, NULL);

	// Start streaming thread
	int bufferThid = sceKernelCreateThread("bufferFilling", fillBuffer, 0x15, 0x1800, PSP_THREAD_ATTR_USER, NULL);
	if(bufferThid < 0)
	{
		sound_status = 0xDEADBEEF; // kill the audioOutput thread
		sceKernelDelayThread(100*1000);
		sceAudioChRelease(sound_channel);
		sound_channel = PSP_AUDIO_NEXT_CHANNEL;
		printf("FATAL: Cannot create bufferFilling thread\n");
		return;
	}
	sceKernelStartThread(bufferThid, 0, NULL);

	// Everything OK
	audio_open = true;
}


/*
 *  Deinitialization
 */

void AudioExit(void)
{
	// Stop stream
	if (audio_open)
	{
		sound_status = 0xDEADBEEF;
		sceKernelSignalSema(bufferEmpty, 1); // fillbuffer thread is probably waiting.
		sceKernelDelayThread(100*1000);
		sceAudioChRelease(sound_channel);
		sound_channel = PSP_AUDIO_NEXT_CHANNEL;
	}

	audio_open = false;

	// Delete semaphores
	sceKernelDeleteSema(bufferEmpty);
	sceKernelDeleteSema(audio_irq_done_sem);
}


/*
 *  First source added, start audio stream
 */

void audio_enter_stream()
{
	// Streaming thread is always running to avoid clicking noises
}


/*
 *  Last source removed, stop audio stream
 */

void audio_exit_stream()
{
	// Streaming thread is always running to avoid clicking noises
}

/*
 *  MacOS audio interrupt, read next data block
 */

void AudioInterrupt(void)
{
	D(bug("AudioInterrupt\n"));

	// Get data from apple mixer
	if (AudioStatus.mixer)
	{
		M68kRegisters r;
		r.a[0] = audio_data + adatStreamInfo;
		r.a[1] = AudioStatus.mixer;
		Execute68k(audio_data + adatGetSourceData, &r);
		D(bug(" GetSourceData() returns %08lx\n", r.d[0]));
	}
	else
		WriteMacInt32(audio_data + adatStreamInfo, 0);

	// Signal stream function
	sceKernelSignalSema(audio_irq_done_sem, 1);
	D(bug("AudioInterrupt done\n"));
}


/*
 *  Set sampling parameters
 *  "index" is an index into the audio_sample_rates[] etc. arrays
 *  It is guaranteed that AudioStatus.num_sources == 0
 */

bool audio_set_sample_rate(int index)
{
	return true;
}

bool audio_set_sample_size(int index)
{
	return true;
}

bool audio_set_channels(int index)
{
	return true;
}


/*
 *  Get/set audio info
 */

bool audio_get_main_mute(void)
{
	return false;
}

uint32 audio_get_main_volume(void)
{
	if (audio_open) {
		uint32 v = (sound_volume << 8) / PSP_AUDIO_VOLUME_MAX;
		return (v << 16) | v;
	} else
		return 0x01000100;
}

bool audio_get_speaker_mute(void)
{
	return false;
}

uint32 audio_get_speaker_volume(void)
{
	return 0x01000100;
}

void audio_set_main_mute(bool mute)
{
}

void audio_set_main_volume(uint32 vol)
{
	if (audio_open)
		sound_volume = (((vol >> 16) + (vol & 0xffff)) * PSP_AUDIO_VOLUME_MAX) >> 9;
}

void audio_set_speaker_mute(bool mute)
{
}

void audio_set_speaker_volume(uint32 vol)
{
}
