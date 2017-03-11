/*
 *  timer_psp.cpp - Time Manager emulation, PSP specific stuff
 *
 *  Basilisk II (C) 1997-2005 Christian Bauer
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
#include <psprtc.h>

#include "sysdeps.h"
#include "macos_util.h"
#include "timer.h"

#define DEBUG 0
#include "debug.h"


uint32 TicksPerMicroSecond = 0xffffffff;


/*
 *  Return microseconds since boot (64 bit)
 */

void Microseconds(uint32 &hi, uint32 &lo)
{
	D(bug("Microseconds\n"));

#if 1
	// Only do this once because it hurts
	if (TicksPerMicroSecond == 0xffffffff)
		TicksPerMicroSecond = (uint32)(sceRtcGetTickResolution() * 0.000001);

	uint64 tl;
	sceRtcGetCurrentTick(&tl);
	tl = tl / TicksPerMicroSecond;
#else
	struct timeval t;
	gettimeofday(&t, NULL);
	uint64 tl = (uint64)t.tv_sec * 1000000 + t.tv_usec;
#endif
	hi = tl >> 32;
	lo = tl;
}


/*
 *  Return local date/time in Mac format (seconds since 1.1.1904)
 */

uint32 TimerDateTime(void)
{
    time_t rawtime;

    //time(&rawtime);
    sceKernelLibcTime(&rawtime);
	return TimeToMacTime(rawtime);
}


/*
 *  Get current time
 */

void timer_current_time(tm_time_t &t)
{
#if 1
	// Only do this once because it hurts
	if (TicksPerMicroSecond == 0xffffffff)
		TicksPerMicroSecond = (uint32)(sceRtcGetTickResolution() * 0.000001);

	sceRtcGetCurrentTick(&t);
	t = t / TicksPerMicroSecond;
#else
	gettimeofday(&t, NULL);
#endif
}


/*
 *  Add times
 */

void timer_add_time(tm_time_t &res, tm_time_t a, tm_time_t b)
{
#if 1
    res = a + b;
#else
	res.tv_sec = a.tv_sec + b.tv_sec;
	res.tv_usec = a.tv_usec + b.tv_usec;
	if (res.tv_usec >= 1000000) {
		res.tv_sec++;
		res.tv_usec -= 1000000;
	}
#endif
}


/*
 *  Subtract times
 */

void timer_sub_time(tm_time_t &res, tm_time_t a, tm_time_t b)
{
#if 1
    res = a - b;
#else
	res.tv_sec = a.tv_sec - b.tv_sec;
	res.tv_usec = a.tv_usec - b.tv_usec;
	if (res.tv_usec < 0) {
		res.tv_sec--;
		res.tv_usec += 1000000;
	}
#endif
}


/*
 *  Compare times (<0: a < b, =0: a = b, >0: a > b)
 */

int timer_cmp_time(tm_time_t a, tm_time_t b)
{
#if 1
    return a - b;
#else
	if (a.tv_sec == b.tv_sec)
		return a.tv_usec - b.tv_usec;
	else
		return a.tv_sec - b.tv_sec;
#endif
}


/*
 *  Convert Mac time value (>0: milliseconds, <0: microseconds) to tm_time_t
 */

void timer_mac2host_time(tm_time_t &res, int32 mactime)
{
#if 1
	if (mactime > 0)
		res = mactime * 1000;	// Time in milliseconds
	else
		res = -mactime;			// Time in negative microseconds
#else
	if (mactime > 0) {
		// Time in milliseconds
		res.tv_sec = mactime / 1000;
		res.tv_usec = (mactime % 1000) * 1000;
	} else {
		// Time in negative microseconds
		res.tv_sec = -mactime / 1000000;
		res.tv_usec = -mactime % 1000000;
	}
#endif
}


/*
 *  Convert positive tm_time_t to Mac time value (>0: milliseconds, <0: microseconds)
 *  A negative input value for hosttime results in a zero return value
 *  As long as the microseconds value fits in 32 bit, it must not be converted to milliseconds!
 */

int32 timer_host2mac_time(tm_time_t hosttime)
{
#if 1
	if (hosttime < 0)
		return 0;
	else if (hosttime > 0x7fffffff)
		return hosttime / 1000;	// Time in milliseconds
	else
		return -hosttime;		// Time in negative microseconds
#else
	if (hosttime.tv_sec < 0)
		return 0;
	else {
		uint64 t = (uint64)hosttime.tv_sec * 1000000 + hosttime.tv_usec;
		if (t > 0x7fffffff)
			return t / 1000;	// Time in milliseconds
		else
			return -t;			// Time in negative microseconds
	}
#endif
}


/*
 *  Get current value of microsecond timer
 */

uint64 GetTicks_usec(void)
{
#if 1
	// Only do this once because it hurts
	if (TicksPerMicroSecond == 0xffffffff)
		TicksPerMicroSecond = (uint32)(sceRtcGetTickResolution() * 0.000001);

	uint64 tl;
	sceRtcGetCurrentTick(&tl);
	return tl / TicksPerMicroSecond;
#else
	struct timeval t;
	gettimeofday(&t, NULL);
	return (uint64)t.tv_sec * 1000000 + t.tv_usec;
#endif
}


/*
 *  Delay by specified number of microseconds (<1 second)
 */

void Delay_usec(uint32 usec)
{
	sceKernelDelayThread(usec);
}


/*
 *  Suspend emulator thread, virtual CPU in idle mode
 */

void idle_wait(void)
{
	sceKernelDelayThread(10000);
}


/*
 *  Resume execution of emulator thread, events just arrived
 */

void idle_resume(void)
{
}
