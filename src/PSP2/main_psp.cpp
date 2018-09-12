/*
 *  main_psp.cpp - Startup code for PSP
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


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <malloc.h>

#include <vita2d.h>
#include <psp2/apputil.h>
#include <psp2/common_dialog.h>
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <psp2/io/dirent.h>
#include <psp2/power.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include "gui_psp.h"
#include "debugScreen.h"
#include "danzeff/danzeff.h"

#include "sysdeps.h"

#include <string>
using std::string;

#include "cpu_emulation.h"
#include "sys.h"
#include "rom_patches.h"
#include "xpram.h"
#include "timer.h"
#include "video.h"
#include "emul_op.h"
#include "prefs.h"
#include "prefs_editor.h"
#include "macos_util.h"
#include "user_strings.h"
#include "version.h"
#include "main.h"
#include "psp2_touch.h"

#define Get_String(x) const_cast<char*>(GetString(x))


#define PSP_VERS	 1
#define PSP_REVS	 0


#define DEBUG 0
#include "debug.h"

void gui_font_init(void);
void do_gui(struct gui_menu *menu, void *menufn, char *exit_msg);

// Constants
const char ROM_FILE_NAME[] = "roms/mac.rom";     // try to open this file if "rom" not in prefs

const int SCRATCH_MEM_SIZE = 0x10000;	// Size of scratch memory area


// Global variables

// CPU and FPU type, addressing mode
int CPUType;
bool CPUIs68060;
int FPUType;
bool TwentyFourBitAddressing;

static uint8 last_xpram[XPRAM_SIZE];				// Buffer for monitoring XPRAM changes

static bool tick_thread_active = false;				// Flag: 60Hz thread installed
static volatile bool tick_thread_cancel = false;	// Flag: Cancel 60Hz thread
//static int tick_thread;								// 60Hz thread

#if USE_SCRATCHMEM_SUBTERFUGE
uint8 *ScratchMem = NULL;			                // Scratch memory for Mac ROM writes
#endif

bool emerg_quit = false;						    // Flag: emergency quit requested

char psp_home[256];
char temp[256];
char *partitions[] = { "ux0:", "uma0:" };
char *data_dir = "data/BasiliskII/";
char *data_sub_dirs[] = { "cdroms", "disks", "files", "hardfiles", "imaps", "roms" };
int num_partitions = 2;
int num_data_sub_dirs = 6;

int psp_net_error = 0;
int psp_net_available = 0;

char szMyIPAddr[32];
char *psp_local_ip_addr = NULL;                     // String: the local IP address for the PSP

#define KERNELMODE 0       /* 1 is untested but required for some keyboards to change baudrate */


extern bool refresh_okay;
extern void psp_video_setup(void);

extern void Sys_drive_stop(void);
extern void Sys_drive_restart(void);

// Prototypes
static int tick_func(SceSize args, void *argp);
static void one_tick(void);

void psp_time_init(void)
{
    /*
    int tzOffset = 0;
    sceUtilityGetSystemParamInt(PSP_SYSTEMPARAM_ID_INT_TIMEZONE, &tzOffset);
    int tzOffsetAbs = tzOffset < 0 ? -tzOffset : tzOffset;
    int hours = tzOffsetAbs / 60;
    int minutes = tzOffsetAbs - hours * 60;
    int pspDaylight = 0;
    sceUtilityGetSystemParamInt(PSP_SYSTEMPARAM_ID_INT_DAYLIGHTSAVINGS, &pspDaylight);
    static char tz[18];
    sprintf(tz, "GMT%s%02i:%02i%s", tzOffset < 0 ? "+" : "-", hours, minutes, pspDaylight ? " DST" : "");
    setenv("TZ", tz, 1);
    tzset();
    */
}

/*
 *  Main program
 */

int main(int argc, char **argv)
{
    // Set common dialog config
	sceAppUtilInit(&(SceAppUtilInitParam){}, &(SceAppUtilBootParam){});
	sceCommonDialogSetConfigParam(&(SceCommonDialogConfigParam){});

	char str[256];

    psvDebugScreenInit();
    psvDebugScreenSetBgColor(0xFF000000);
    psvDebugScreenSetFgColor(0xFFFFFFFF);
    psvDebugScreenClear(0xFF000000);

	sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);

    // Print some info
    psvDebugScreenClear(0xFF000000);
    psvDebugScreenPrintf("\n\n  ");
    psvDebugScreenPrintf(Get_String(STR_ABOUT_TEXT1), VERSION_MAJOR, VERSION_MINOR);
    psvDebugScreenPrintf("\n  %s\n", Get_String(STR_ABOUT_TEXT2));
    sceKernelDelayThread(2*1000*1000);

    int i, d, found = 0;
    for (i = 0; i < num_partitions; i++) {
        strcpy(psp_home, partitions[i]);
        strcat(psp_home, data_dir);

        d = sceIoDopen(psp_home);
        if (d >= 0) {
            sceIoDclose(d);
            found = 1;
            break;
        }
    }

    if (!found) {
        psvDebugScreenPrintf("\n\n  ");
        psvDebugScreenPrintf("Data directory not found! Exiting...");
        sceKernelDelayThread(2*1000*1000);
        return 0;
    }

    for (i = 0; i < num_data_sub_dirs; i++) {
        strcpy(temp, psp_home);
        strcat(temp, data_sub_dirs[i]);
        d = sceIoDopen(temp);
        if (d >= 0)
            sceIoDclose(d);
        else
            sceIoMkdir(temp, 0777);
    }

    psp_time_init();

    vita2d_init();

	gui_font_init();

	danzeff_load();

	psp2InitTouch();

	// Initialize variables
	RAMBaseHost = NULL;
	ROMBaseHost = NULL;

	// Read preferences
	PrefsInit(argc, argv);
	// Init system routines
	SysInit();

	// Show preferences editor
	if (!PrefsFindBool("nogui"))
		if (!PrefsEditor())
			QuitEmulator();

    // set CPU frequency
    int freq = PrefsFindInt32("pspspeed");
    switch (freq)
    {
        case 1:
        scePowerSetArmClockFrequency(111);
        break;
        case 2:
        scePowerSetArmClockFrequency(166);
        break;
        case 3:
        scePowerSetArmClockFrequency(222);
        break;
        case 4:
        scePowerSetArmClockFrequency(266);
        break;
        case 5:
        scePowerSetArmClockFrequency(333);
        break;
        case 6:
        scePowerSetArmClockFrequency(444);
        break;
        case 7:
        // Setting maximum clocks
        scePowerSetArmClockFrequency(444);
        scePowerSetBusClockFrequency(222);
        scePowerSetGpuClockFrequency(222);
        scePowerSetGpuXbarClockFrequency(166);
        break;
    }

	// Read RAM size
	RAMSize = PrefsFindInt32("ramsize") & 0xfff00000;	// Round down to 1MB boundary
	if (RAMSize < 1024*1024) {
		WarningAlert(Get_String(STR_SMALL_RAM_WARN));
		RAMSize = 1024*1024;
	}

	// Create areas for Mac RAM and ROM
	uint8 *ram_rom_area = (uint8 *)memalign(64, RAMSize + 0x100000);
	if (ram_rom_area == NULL) {
		ErrorAlert(STR_NO_MEM_ERR);
		QuitEmulator();
	}

	RAMBaseHost = ram_rom_area;
	ROMBaseHost = RAMBaseHost + RAMSize;

#if USE_SCRATCHMEM_SUBTERFUGE
	// Allocate scratch memory
	ScratchMem = (uint8 *)memalign(64, SCRATCH_MEM_SIZE);
	if (ScratchMem == NULL) {
		ErrorAlert(STR_NO_MEM_ERR);
		QuitEmulator();
	}
	ScratchMem += SCRATCH_MEM_SIZE/2;	// ScratchMem points to middle of block
#endif

#if DIRECT_ADDRESSING
	// RAMBaseMac shall always be zero
	MEMBaseDiff = (uintptr)RAMBaseHost;
	RAMBaseMac = 0;
	ROMBaseMac = Host2MacAddr(ROMBaseHost);
#endif
	D(bug("Mac RAM starts at %p (%08x)\n", RAMBaseHost, RAMBaseMac));
	D(bug("Mac ROM starts at %p (%08x)\n", ROMBaseHost, ROMBaseMac));

	// Get rom file path from preferences
	const char *rom_path = PrefsFindString("rom");

	// Load Mac ROM
	int rom_fd = open(rom_path ? rom_path : ROM_FILE_NAME, O_RDONLY);
	if (rom_fd < 0) {
		ErrorAlert(STR_NO_ROM_FILE_ERR);
		QuitEmulator();
	}
	ROMSize = lseek(rom_fd, 0, SEEK_END);
	uint32 rom_start = ROMSize & 65535; // any odd number of bytes to 64K should be just a header
	if ((ROMSize-rom_start) != 64*1024 && (ROMSize-rom_start) != 128*1024 && (ROMSize-rom_start) != 256*1024 && (ROMSize-rom_start) != 512*1024 && (ROMSize-rom_start) != 1024*1024) {
		ErrorAlert(STR_ROM_SIZE_ERR);
		close(rom_fd);
		QuitEmulator();
	}
	lseek(rom_fd, rom_start, SEEK_SET);
	if (read(rom_fd, ROMBaseHost, ROMSize-rom_start) != (ssize_t)(ROMSize-rom_start)) {
		ErrorAlert(STR_ROM_FILE_READ_ERR);
		close(rom_fd);
		QuitEmulator();
	}

	// Initialize everything
	if (!InitAll())
		QuitEmulator();
	D(bug("Initialization complete\n"));

#ifndef USE_CPU_EMUL_SERVICES
	// start 60Hz thread
	int thid = sceKernelCreateThread("tick_thread", tick_func, 0x10000100, 0x1800, 0, 0, NULL);

	if(thid < 0) {
		sprintf(str, Get_String(STR_TICK_THREAD_ERR));
		ErrorAlert(str);
		QuitEmulator();
	}
	sceKernelStartThread(thid, 0, 0);
	tick_thread_active = true;
	D(bug("60Hz thread started\n"));

#endif
	// Start 68k and jump to ROM boot routine
	D(bug("Starting emulation...\n"));
	Start680x0();

	QuitEmulator();
	return 0;
}


/*
 *  Quit emulator
 */

void QuitEmulator(void)
{
	D(bug("QuitEmulator\n"));

#if EMULATED_68K
	// Exit 680x0 emulation
	Exit680x0();
#endif

#if defined(USE_CPU_EMUL_SERVICES)
	// Show statistics
	uint64 emulated_ticks_end = GetTicks_usec();
	D(bug("%ld ticks in %ld usec = %f ticks/sec [%ld tick checks]\n",
		  (long)emulated_ticks_count, (long)(emulated_ticks_end - emulated_ticks_start),
		  emulated_ticks_count * 1000000.0 / (emulated_ticks_end - emulated_ticks_start), (long)n_check_ticks));
#else
	// Stop 60Hz thread
	if (tick_thread_active) {
		tick_thread_cancel = true;
		sceKernelDelayThread(100*1000);
	}
#endif

    // Deinitialize everything
	ExitAll();

	// Free ROM/RAM areas
	if (RAMBaseHost != NULL) {
		free(RAMBaseHost);
		RAMBaseHost = NULL;
		ROMBaseHost = NULL;
	}

#if USE_SCRATCHMEM_SUBTERFUGE
	// Delete scratch memory area
	if (ScratchMem != NULL) {
		free((void *)(ScratchMem - SCRATCH_MEM_SIZE/2));
		ScratchMem = NULL;
	}
#endif

	// Exit system routines
	SysExit();

	// Exit preferences
	PrefsExit();

	sceKernelExitProcess(0);
	exit(0);
}


/*
 *  Code was patched, flush caches if neccessary (i.e. when using a real 680x0
 *  or a dynamically recompiling emulator)
 */

void FlushCodeCache(void *start, uint32 size)
{
    // not needed yet
	//sceKernelDcacheWritebackAll();
	//sceKernelIcacheClearAll();
}


/*
 *  Mutexes
 */

struct B2_mutex {
	int dummy;
};

B2_mutex *B2_create_mutex(void)
{
	return new B2_mutex;
}

void B2_lock_mutex(B2_mutex *mutex)
{
}

void B2_unlock_mutex(B2_mutex *mutex)
{
}

void B2_delete_mutex(B2_mutex *mutex)
{
	delete mutex;
}


/*
 *  Interrupt flags (must be handled atomically!)
 */

uint32 InterruptFlags = 0;

#if EMULATED_68K
void SetInterruptFlag(uint32 flag)
{
	// PSP uses cooperative multitasking, so no need to worry
	InterruptFlags |= flag;
}

void ClearInterruptFlag(uint32 flag)
{
	// PSP uses cooperative multitasking, so no need to worry
	InterruptFlags &= ~flag;
}
#endif

/*
 *  XPRAM watchdog thread (saves XPRAM every minute)
 */

static void xpram_watchdog(void)
{
	if (memcmp(last_xpram, XPRAM, XPRAM_SIZE)) {
		memcpy(last_xpram, XPRAM, XPRAM_SIZE);
		SaveXPRAM();
	}
}


static void one_second(void)
{
	// Pseudo Mac 1Hz interrupt, update local time
	WriteMacInt32(0x20c, TimerDateTime());

	SetInterruptFlag(INTFLAG_1HZ);
	TriggerInterrupt();

	static int second_counter = 0;
	if (++second_counter > 60) {
		second_counter = 0;
		xpram_watchdog();
	}
}

static void one_tick(void)
{
	static int tick_counter = 0;
	if (++tick_counter > 60) {
		tick_counter = 0;
		one_second();
	}

	// Trigger 60Hz interrupt
	if (ROMVersion != ROM_VERSION_CLASSIC || HasMacStarted()) {
		SetInterruptFlag(INTFLAG_60HZ);
		TriggerInterrupt();
	}
}

/*
 *  60Hz thread (really 60.15 Hz or 59.94 Hz if wait on vblank)
 */

static int tick_func(SceSize args, void *argp)
{
    bool timing = PrefsFindBool("relaxed60hz");

    if (timing)
    {
        while (!tick_thread_cancel)
        {
            one_tick();
            sceKernelDelayThread(16625);
        }
    }
    else
    {
        uint64 next = GetTicks_usec();
        while (!tick_thread_cancel)
        {
            one_tick();
            next += 16625;
            int64 delay = next - GetTicks_usec();
            if (delay > 0)
                Delay_usec(delay);
            else if (delay < -16625)
                next = GetTicks_usec();
        }
    }

	sceKernelExitDeleteThread(0);
	return 0;
}


/*
 *  Display alert
 */

void display_alert(char *title, char *prefix, char *button, char *text)
{
    struct gui_menu AlertMenu[] = {
        { "", GUI_CENTER | GUI_TEXT, (void *)0xFFFFFF, (void *)0x1020FF, GUI_DISABLED }, // title
        { "", GUI_CENTER | GUI_DIVIDER, 0, 0, GUI_DISABLED },
        { "", GUI_CENTER | GUI_STRING, 0, 0, GUI_DISABLED },  // prefix: text
        { 0, GUI_END_OF_MENU, 0, 0, 0 } // end of menu
    };

	AlertMenu[0].text = title;
	AlertMenu[2].text = prefix;
	AlertMenu[2].field1 = (void *)&text;

	do_gui(AlertMenu, NULL, button);
}


/*
 *  Display error alert
 */

void ErrorAlert(const char *text)
{
	if (PrefsFindBool("nogui"))
		printf(Get_String(STR_SHELL_ERROR_PREFIX), text);
	else
		display_alert(Get_String(STR_ERROR_ALERT_TITLE), Get_String(STR_GUI_ERROR_PREFIX), Get_String(STR_QUIT_BUTTON), const_cast<char*>(text));
}


/*
 *  Display warning alert
 */

void WarningAlert(const char *text)
{
	if (PrefsFindBool("nogui"))
		printf(Get_String(STR_SHELL_WARNING_PREFIX), text);
	else
		display_alert(Get_String(STR_WARNING_ALERT_TITLE), Get_String(STR_GUI_WARNING_PREFIX), Get_String(STR_OK_BUTTON), const_cast<char*>(text));
}


/*
 *  Display choice alert
 */

bool ChoiceAlert(const char *text, const char *pos, const char *neg)
{
	int choice;

	struct gui_list choice_list[] = {
		{ "", 0 },
		{ "", 1 },
		{ 0, GUI_END_OF_LIST }
	};

	struct gui_menu ChoiceMenu[] = {
		{ "Alert! Please make a choice.", GUI_CENTER | GUI_TEXT, (void *)0xFFFFFF, (void *)0x1020FF, GUI_DISABLED },
		{ "", GUI_CENTER | GUI_DIVIDER, 0, 0, GUI_DISABLED },
		{ "", GUI_CENTER | GUI_SELECT, &choice_list, &choice, GUI_ENABLED }, // text: pos/neg
		{ 0, GUI_END_OF_MENU, 0, 0, 0 } // end of menu
	};

	ChoiceMenu[2].text = const_cast<char*>(text);
	choice_list[0].text = const_cast<char*>(pos);
	choice_list[1].text = const_cast<char*>(neg);
	choice = 0; // default to pos choice

	do_gui(ChoiceMenu, NULL, "Select current choice");

	return choice ? false : true;
}
