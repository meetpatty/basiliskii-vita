/*
 *  prefs_editor_psp.cpp - Preferences editor, PSP implementation
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

#include <psp2/io/fcntl.h>
#include <psp2/kernel/processmgr.h>

#include "gui_psp.h"

extern void do_gui(struct gui_menu *menu, void *menufn, char *exit_msg);
extern char *RequestString (char *initialStr);

#include "version.h"
#include "sysdeps.h"
#include "prefs.h"
#include "prefs_editor.h"
#include "user_strings.h"
#include "user_strings_psp.h"

#define Get_String(x) const_cast<char*>(GetString(x))

/*
 *
 * GUI variables
 *
 */

// prefs with default values
int psp_screen_mode = 4; // 726x544
int psp_screen_depth = 1; // 256 color
int psp_screen_rate = 0; // 60 Hz
int psp_sound_enable = 1; // sound on

int psp_ram_size = 12; // 12 MB
int psp_model_id = 1; // Quadra 900
int psp_mac_cpu = 4; // 68040
char *psp_rom_file = NULL;

int psp_seriala_select = 0; // disabled
int psp_serialb_select = 0; // disabled
int psp_udp_enable = 0; // UDP Tunnel disabled
int psp_udp_port = 6066; // UDP Tunnel port
int psp_net_accesspoint = 0; // connect to first access point

int psp_boot_select = 0; // boot any device
int psp_extfs_enable = 0; // disable access to memstick
char *psp_hardfile0 = NULL;
char *psp_hardfile1 = NULL;
char *psp_hardfile2 = NULL;
char *psp_hardfile3 = NULL;
char *psp_cdromfile = NULL;
char *psp_floppyfile = NULL;
int psp_file_size = 0; // start at 100 MB
char *psp_hardfile = NULL;

int psp_cpu_speed = 0; // default speed

int psp_lcd_aspect = 0; // 4:3
int psp_lcd_border = 1; // default to inverting the data

int psp_60hz_timing = 1; // default to relaxed timing
int psp_rear_touch_enable = 0; // do not use rear touch panel
int psp_indirect_touch_enable = 0; // do not use indirect touch on front panel
int psp_pointer_speed = 3; // indirect touch pointer speed is 1.0
int psp_analog_deadzone = 2; // analog deadzone default is 10%

extern char *psp_floppy_inserted;                    // String: filename of floppy inserted
extern char *psp_cdrom_inserted;                     // String: filename of cdrom inserted
extern char psp_home[256];

// Memory support code

#define MEMORY_USER_SIZE 0x04000000
int _newlib_heap_size_user = 192 * 1024 * 1024;

static uint32 memFreeSpace(uint32 inAccuracy) {
     if(!inAccuracy)
          inAccuracy = 1;

     uint32 tempBlockSize = (MEMORY_USER_SIZE >> 1);
     uint32 tempFreeSpace = 0;
     void* tempPointers[8192];
     uint32 tempPtr = 0;
     while((tempPtr < 8192) && (tempBlockSize > inAccuracy)) {
          tempPointers[tempPtr] = malloc(tempBlockSize);
          while(tempPointers[tempPtr]) {
               tempPtr++;
               tempFreeSpace += tempBlockSize;
               tempPointers[tempPtr] = malloc(tempBlockSize);
          }
          tempBlockSize >>= 1;
     }

     tempPtr = 0;
     while(tempPointers[tempPtr]) {
          free(tempPointers[tempPtr]);
          tempPtr++;
     }

     return tempFreeSpace;
}

static int psp_amount_free(void)
{
    return (memFreeSpace(256*1024) - 1792*1024)/(1024*1024);
}

// Network support code

extern int psp_net_error;

char *getconfname(int confnum) {
    static char confname[128];

    //if (sceUtilityCheckNetParam(confnum) != 0)
        return 0;

    /*sceUtilityGetNetParam(confnum, PSP_NETPARAM_NAME, (netData *)confname);
    return confname;*/
}

// Hardfile support code

void psp_create_hardfile(void *arg)
{
    int i;
    char filename[512];
    char textbuf[512];

    int filesize = psp_file_size ? psp_file_size * 200 : 100;

    psp_hardfile = RequestString("hardfile0.hfv");
    if (!psp_hardfile)
        return; // abort creation

    strcpy(filename, psp_home);
    strcat(filename, "hardfiles/");
    strcat(filename, psp_hardfile);
    sprintf(textbuf, "Making %d MB hardfile:\n\n%s", filesize, filename);

    gui_Print_Left(textbuf, 0xFFCCCCCC);

    int fd = sceIoOpen(filename, SCE_O_CREAT | SCE_O_TRUNC | SCE_O_WRONLY, 0777);

	if (fd <= 0)
	{
        sceKernelDelayThread(4*1000*1000);
        gui_Print_Left("Could not create hardfile!", 0xFFED4337);
        sceKernelDelayThread(4*1000*1000);
        return;
	}

    void *buffer = malloc(1024*1024);
    memset(buffer, 0, 1024*1024);

    for (i=0; i<filesize; i++)
    {
        sceIoWrite(fd, buffer, 1024*1024);
        //pspDebugScreenPrintf(".");
    }
    sceIoClose(fd);
    free(buffer);

    gui_Print_Left("Hardfile successfully created.", 0xFFCCCCCC);
    sceKernelDelayThread(4*1000*1000);
}

void psp_create_floppy(void *arg)
{
    int i;
    char filename[512];
    char message[512];

    psp_hardfile = RequestString("floppy0.dsk");
    if (!psp_hardfile)
        return; // abort creation

    strcpy(filename, psp_home);
    strcat(filename, "disks/");
    strcat(filename, psp_hardfile);
    sprintf(message, "Making 1.4 MB floppy:\n\n%s", filename);

    gui_Print_Left(message, 0xFFCCCCCC);

    int fd = sceIoOpen(filename, SCE_O_CREAT | SCE_O_TRUNC | SCE_O_WRONLY, 0777);

	if (fd <= 0)
	{
        sceKernelDelayThread(4*1000*1000);
        gui_Print_Left("Could not create floppy!", 0xFFED4337);
        sceKernelDelayThread(4*1000*1000);
        return;
	}

    void *buffer = malloc(80*512);
    memset(buffer, 0, 80*512);

    for (i=0; i<36; i++)
    {
        sceIoWrite(fd, buffer, 80*512);
       // pspDebugScreenPrintf(".");
    }
    sceIoClose(fd);
    free(buffer);

    gui_Print_Left("Floppy successfully created.", 0xFFCCCCCC);
    sceKernelDelayThread(4*1000*1000);
}

/*
 *
 * GUI structures
 *
 */

	struct gui_menu AboutLevel[] = {
		{ "", GUI_CENTER | GUI_TEXT, (void *)0xFFFFFF, (void *)0x1020FF, GUI_DISABLED },
		{ Get_String(STR_ABOUT_TEXT2), GUI_CENTER | GUI_TEXT, (void *)0xFFFFFF, (void *)0x1020FF, GUI_DISABLED },
		{ "", GUI_CENTER | GUI_DIVIDER, 0, 0, GUI_DISABLED },
		{ "Basilisk II comes with ABSOLUTELY NO WARRANTY.", GUI_CENTER | GUI_TEXT, (void *)0x8888FF, 0, GUI_DISABLED },
		{ "This is free software, and you are welcome to redistribute it", GUI_CENTER | GUI_TEXT, (void *)0x8888FF, 0, GUI_DISABLED },
		{ "under the terms of the GNU General Public License.", GUI_CENTER | GUI_TEXT, (void *)0x8888FF, 0, GUI_DISABLED },
		{ "", GUI_CENTER | GUI_DIVIDER, 0, 0, GUI_DISABLED },
		{ "Vita conversion by meetpatty based off Chilly Willy's PSP port", GUI_CENTER | GUI_TEXT, (void *)0xFFFFFF, (void *)0xCCCCCC, GUI_DISABLED },
		{ "Uses Vita2d by xerpi", GUI_CENTER | GUI_TEXT, (void *)0xFF8888, 0, GUI_DISABLED },
		{ "and ChicagoFLF by Robin Casady et al", GUI_CENTER | GUI_TEXT, (void *)0x88FF88, 0, GUI_DISABLED },
		{ 0, GUI_END_OF_MENU, 0, 0, 0 } // end of menu
	};

	struct gui_list psp_aspect_list[] = {
		{ "4:3", 0 },
		{ "16:9", 1 },
        { "Fit", 2},
		{ 0, GUI_END_OF_LIST }
	};

	struct gui_list psp_cpu_speed_list[] = {
		{ "Default", 0 },
		{ "111 MHz", 1 },
		{ "166 MHz", 2 },
		{ "266 MHz", 3 },
		{ "300 MHz", 4 },
		{ "333 MHz", 5 },
		{ "444 MHz", 6 },
		{ "Max CPU/GPU/Bus", 7 },
		{ 0, GUI_END_OF_LIST }
	};

	struct gui_list psp_pointer_speed_list[] = {
		{ "0.25", 0 },
		{ "0.50", 1 },
		{ "0.75", 2 },
		{ "1.00", 3 },
		{ "1.25", 4 },
		{ "1.50", 5 },
		{ "1.75", 6 },
		{ "2.00", 7 },
		{ 0, GUI_END_OF_LIST }
	};

    struct gui_list psp_analog_deadzone_list[] = {
        { "5%", 0 },
        { "10%", 1 },
        { "15%", 2 },
        { "20%", 3 },
        { "25%", 4 },
        { "30%", 5 },
        { 0, GUI_END_OF_LIST }
    };

	struct gui_menu PspLevel[] = {
		{ Get_String(STR_PSP_CPU_FREQ), GUI_CENTER | GUI_SELECT, &psp_cpu_speed_list, &psp_cpu_speed, GUI_ENABLED },
		{ Get_String(STR_PSP_DISPLAY_ASPECT), GUI_CENTER | GUI_SELECT, &psp_aspect_list, &psp_lcd_aspect, GUI_ENABLED },
		{ Get_String(STR_PSP_RELAXED_60HZ), GUI_CENTER | GUI_TOGGLE, &psp_60hz_timing, 0, GUI_ENABLED },
		{ Get_String(STR_PSP_REAR_TOUCH), GUI_CENTER | GUI_TOGGLE, &psp_rear_touch_enable, 0, GUI_ENABLED },
		{ Get_String(STR_PSP_INDIRECT_TOUCH), GUI_CENTER | GUI_TOGGLE, &psp_indirect_touch_enable, 0, GUI_ENABLED },
		{ Get_String(STR_PSP_POINTER_SPEED), GUI_CENTER | GUI_SELECT, &psp_pointer_speed_list, &psp_pointer_speed, GUI_ENABLED },
		{ Get_String(STR_PSP_ANALOG_DEADZONE), GUI_CENTER | GUI_SELECT, &psp_analog_deadzone_list, &psp_analog_deadzone, GUI_ENABLED },
		{ 0, GUI_END_OF_MENU, 0, 0, 0 } // end of menu
	};

	struct gui_list vol_filesize_list[] = {
		{ "100", 0 },
		{ "200", 1 },
		{ "400", 2 },
		{ "600", 3 },
		{ "800", 4 },
		{ "1000", 5 },
		{ "1200", 6 },
		{ "1400", 7 },
		{ "1600", 8 },
		{ "1800", 9 },
		{ "2000", 10 },
		{ 0, GUI_END_OF_LIST }
	};

	struct gui_menu CreateVolLevel[] = {
		{ Get_String(STR_HARDFILE_SIZE_CTRL), GUI_CENTER | GUI_SELECT, &vol_filesize_list, &psp_file_size, GUI_ENABLED },
        { Get_String(STR_CREATE_VOLUME_PANEL_BUTTON), GUI_CENTER | GUI_FUNCTION, (void *)&psp_create_hardfile, NULL, GUI_ENABLED },
        { Get_String(STR_CREATE_FLOPPY_PANEL_BUTTON), GUI_CENTER | GUI_FUNCTION, (void *)&psp_create_floppy, NULL, GUI_ENABLED },
		{ 0, GUI_END_OF_MENU, 0, 0, 0 } // end of menu
	};

	struct gui_list vol_boot_list[] = {
		{ Get_String(STR_BOOT_ANY_LAB), 0 },
		{ Get_String(STR_BOOT_CDROM_LAB), 1 },
		{ 0, GUI_END_OF_LIST }
	};

    struct gui_menu VolLevel[] = {
        { Get_String(STR_BOOTDRIVER_CTRL), GUI_CENTER | GUI_SELECT, &vol_boot_list, &psp_boot_select, GUI_ENABLED },
        { Get_String(STR_EXTFS_CTRL), GUI_CENTER | GUI_TOGGLE, &psp_extfs_enable, 0, GUI_ENABLED },
        { Get_String(STR_CREATE_VOLUME_TITLE), GUI_CENTER | GUI_MENU, CreateVolLevel, 0, GUI_DISABLED },
		{ "", GUI_CENTER | GUI_DIVIDER, 0, 0, GUI_DISABLED },
		{ Get_String(STR_VOLUMES_CTRL), GUI_CENTER | GUI_TEXT, 0, 0, GUI_DISABLED },
        { Get_String(STR_VOL_FILE_LAB), GUI_LEFT | GUI_FILE, &psp_hardfile0, (void *)"hardfiles", GUI_ENABLED},
        { Get_String(STR_VOL_FILE_LAB), GUI_LEFT | GUI_FILE, &psp_hardfile1, (void *)"hardfiles", GUI_ENABLED},
        { Get_String(STR_VOL_FILE_LAB), GUI_LEFT | GUI_FILE, &psp_hardfile2, (void *)"hardfiles", GUI_ENABLED},
        { Get_String(STR_VOL_FILE_LAB), GUI_LEFT | GUI_FILE, &psp_hardfile3, (void *)"hardfiles", GUI_ENABLED},
        { Get_String(STR_BOOT_CDROM_LAB), GUI_LEFT | GUI_FILE, &psp_cdromfile, (void *)"cdroms", GUI_ENABLED},
        { Get_String(STR_BOOT_FLOPPY_LAB), GUI_LEFT | GUI_FILE, &psp_floppyfile, (void *)"disks", GUI_ENABLED},
		{ 0, GUI_END_OF_MENU, 0, 0, 0 } // end of menu
    };

	struct gui_list net_wap_list[] = {
		{ "none", 0 },
		{ NULL, 1 },
		{ NULL, 2 },
		{ NULL, 3 },
		{ NULL, 4 },
		{ NULL, 5 },
		{ NULL, 6 },
		{ NULL, 7 },
		{ NULL, 8 },
		{ NULL, 9 },
		{ NULL, 10 },
		{ 0, GUI_END_OF_LIST }
	};

	int net_port_range[2] = { 5000, 65535 };

	struct gui_list net_serial_list[] = {
	    { Get_String(STR_NONE_LAB), 0 },
		{ Get_String(STR_PSP_SERIAL), 1 },
		{ Get_String(STR_PSP_IRDA), 2 },
		{ 0, GUI_END_OF_LIST }
	};

    struct gui_menu NetLevel[] = {
        { Get_String(STR_SERIALA_CTRL), GUI_CENTER | GUI_SELECT, &net_serial_list, &psp_seriala_select, GUI_DISABLED },
        { Get_String(STR_SERIALB_CTRL), GUI_CENTER | GUI_SELECT, &net_serial_list, &psp_serialb_select, GUI_DISABLED },
        { Get_String(STR_UDPTUNNEL_CTRL), GUI_CENTER | GUI_TOGGLE, &psp_udp_enable, 0, GUI_DISABLED },
        { Get_String(STR_UDPPORT_CTRL), GUI_CENTER | GUI_INTEGER, &psp_udp_port, &net_port_range, GUI_DISABLED },
		{ Get_String(STR_ACCESS_POINT), GUI_CENTER | GUI_SELECT, &net_wap_list, &psp_net_accesspoint, GUI_DISABLED },
		{ 0, GUI_END_OF_MENU, 0, 0, 0 } // end of menu
    };

	struct gui_list mem_cpu_list[] = {
		{ Get_String(STR_CPU_68020_LAB), 0 },
		{ Get_String(STR_CPU_68020_FPU_LAB), 1 },
		{ Get_String(STR_CPU_68030_LAB), 2 },
		{ Get_String(STR_CPU_68030_FPU_LAB), 3 },
		{ Get_String(STR_CPU_68040_LAB), 4 },
		{ 0, GUI_END_OF_LIST }
	};

	struct gui_list mem_model_list[] = {
		{ Get_String(STR_MODELID_5_LAB), 0 },
		{ Get_String(STR_MODELID_14_LAB), 1 },
		{ 0, GUI_END_OF_LIST }
	};

	int mem_ram_range[2] = { 4, 16 };

    struct gui_menu MemLevel[] = {
        { Get_String(STR_RAMSIZE_CTRL), GUI_CENTER | GUI_INTEGER, &psp_ram_size, &mem_ram_range, GUI_ENABLED },
        { Get_String(STR_MODELID_CTRL), GUI_CENTER | GUI_SELECT, &mem_model_list, &psp_model_id, GUI_ENABLED },
        { Get_String(STR_CPU_CTRL), GUI_CENTER | GUI_SELECT, &mem_cpu_list, &psp_mac_cpu, GUI_ENABLED },
		{ "", GUI_CENTER | GUI_DIVIDER, 0, 0, GUI_DISABLED },
        { Get_String(STR_ROM_FILE_CTRL), GUI_LEFT | GUI_FILE, &psp_rom_file, (void *)"roms", GUI_ENABLED},
		{ 0, GUI_END_OF_MENU, 0, 0, 0 } // end of menu
    };

	struct gui_list gfx_rate_list[] = {
		{ "60 Hz", 0 },
		{ "30 Hz", 1 },
		{ "20 Hz", 2 },
		{ "15 Hz", 3 },
		{ "12 Hz", 4 },
		{ "10 Hz", 5 },
		{ "8.5 Hz", 6 },
		{ "7.5 Hz", 7 },
		{ 0, GUI_END_OF_LIST }
	};

	struct gui_list gfx_depth_list[] = {
		{ Get_String(STR_4_BIT_LAB), 0 },
		{ Get_String(STR_8_BIT_LAB), 1 },
		{ Get_String(STR_15_BIT_LAB), 2 },
		{ Get_String(STR_24_BIT_LAB), 3 },
		{ 0, GUI_END_OF_LIST }
	};

	struct gui_list gfx_screen_list[] = {
		{ "512x384", 0 },
		{ "640x360", 1 },
		{ "640x400", 2 },
		{ "640x480", 3 },
		{ "726x544", 4 },
		{ "768x432", 5 },
		{ "768x576", 6 },
		{ 0, GUI_END_OF_LIST }
	};

    struct gui_menu GfxLevel[] = {
        { Get_String(STR_SCREEN_MODE_CTRL), GUI_CENTER | GUI_SELECT, &gfx_screen_list, &psp_screen_mode, GUI_ENABLED },
        { Get_String(STR_COLOR_DEPTH_CTRL), GUI_CENTER | GUI_SELECT, &gfx_depth_list, &psp_screen_depth, GUI_ENABLED },
        { Get_String(STR_FRAMESKIP_CTRL), GUI_CENTER | GUI_SELECT, &gfx_rate_list, &psp_screen_rate, GUI_ENABLED },
		{ "", GUI_CENTER | GUI_DIVIDER, 0, 0, GUI_DISABLED },
        { Get_String(STR_SOUND_CTRL), GUI_CENTER | GUI_TOGGLE, &psp_sound_enable, 0, GUI_ENABLED},
		{ 0, GUI_END_OF_MENU, 0, 0, 0 } // end of menu
    };

	struct gui_menu TopLevel[] = {
	    { Get_String(STR_PREFS_TITLE), GUI_CENTER | GUI_TEXT, (void *)0xFF8888, 0, GUI_DISABLED },
		{ Get_String(STR_GRAPHICS_SOUND_PANE_TITLE), GUI_CENTER | GUI_MENU, GfxLevel, 0, GUI_ENABLED },
		{ Get_String(STR_MEMORY_MISC_PANE_TITLE), GUI_CENTER | GUI_MENU, MemLevel, 0, GUI_ENABLED },
		{ Get_String(STR_SERIAL_NETWORK_PANE_TITLE), GUI_CENTER | GUI_MENU, NetLevel, 0, GUI_DISABLED },
		{ Get_String(STR_VOLUMES_PANE_TITLE), GUI_CENTER | GUI_MENU, VolLevel, 0, GUI_ENABLED },
		{ Get_String(STR_PSP_PANE_TITLE), GUI_CENTER | GUI_MENU, PspLevel, 0, GUI_ENABLED },
		{ Get_String(STR_PREFS_ITEM_ABOUT), GUI_CENTER | GUI_MENU, AboutLevel, 0, GUI_ENABLED },
		{ 0, GUI_END_OF_MENU, 0, 0, 0 } // end of menu
	};

/*
 *  Show preferences editor
 *  Returns true when user clicked on "Start", false otherwise
 */

bool PrefsEditor(void)
{
	char vstr[32];
	int i;

	sprintf(vstr, Get_String(STR_ABOUT_TEXT1), VERSION_MAJOR, VERSION_MINOR);
	AboutLevel[0].text = vstr;

    mem_ram_range[1] = psp_amount_free(); // amount of free memory in MB

    if (psp_net_error)
    {
        NetLevel[2].enable = GUI_DISABLED; // disable UDP Tunnel
    }
    else
    {
        for (i=1; i<11; i++)
        {
            char *cfg;
            cfg = getconfname(i);
            if (cfg)
            {
                //printf("Found connection %d (%s)\n", i, cfg);
                if (net_wap_list[i].text)
                    free(net_wap_list[i].text);
                net_wap_list[i].text = strdup(cfg);
                net_wap_list[i].index = i;
            }
            else
                break;
        }
        net_wap_list[i].index = GUI_END_OF_LIST;
    }

    // set initial values from prefs

    const char *temp = PrefsFindString("screen");
    if (temp)
    {
        int width, height, depth;
        sscanf(temp, "%d/%d/%d", &width, &height, &depth);
        if (width == 512 && height == 384)
            psp_screen_mode = 0;
        else if (width == 640 && height == 360)
            psp_screen_mode = 1;
        else if (width == 640 && height == 400)
            psp_screen_mode = 2;
        else if (width == 640 && height == 480)
            psp_screen_mode = 3;
         else if (width == 726 && height == 544)
            psp_screen_mode = 4;
        else if (width == 768 && height == 432)
            psp_screen_mode = 5;
        else if (width == 768 && height == 576)
            psp_screen_mode = 6;
        switch (depth)
        {
            case 4:
            psp_screen_depth = 0;
            break;
            case 8:
            psp_screen_depth = 1;
            break;
            case 15:
            psp_screen_depth = 2;
            break;
            case 24:
            psp_screen_depth = 3;
            break;
        }
    }

    psp_screen_rate = PrefsFindInt32("frameskip") - 1;
    if (psp_screen_rate < 0 || psp_screen_rate > 7)
        psp_screen_rate = 0;

    psp_sound_enable = PrefsFindBool("nosound") ? 0 : 1;

    psp_ram_size = PrefsFindInt32("ramsize") / (1024* 1024);
    if (psp_ram_size <= 2 || psp_ram_size > mem_ram_range[1])
        psp_ram_size = 8;

    switch (PrefsFindInt32("modelid"))
    {
        case 5:
        psp_model_id = 0;
        break;
        case 14:
        psp_model_id = 1;
        break;
    }

    psp_mac_cpu = (PrefsFindInt32("cpu") - 2) * 2;
    if (psp_mac_cpu < 0)
        psp_mac_cpu = 4;
    if (PrefsFindBool("fpu"))
        psp_mac_cpu +=1;
    if (psp_mac_cpu > 4)
        psp_mac_cpu = 4;

    temp = PrefsFindString("rom");
    if (temp)
        psp_rom_file = strdup(temp);

    // do serial here eventually

    psp_udp_enable = PrefsFindBool("udptunnel") ? 1 : 0;

    psp_udp_port = PrefsFindInt32("udpport");
    if (psp_udp_port <= 5000 || psp_udp_port > 65535)
        psp_udp_port = 6066;

    psp_net_accesspoint = PrefsFindInt32("udpapoint");

    psp_boot_select = PrefsFindInt32("bootdriver");

    temp = PrefsFindString("disk", 0);
    if (temp)
        psp_hardfile0 = strdup(temp);

    temp = PrefsFindString("disk", 1);
    if (temp)
        psp_hardfile1 = strdup(temp);

    temp = PrefsFindString("disk", 2);
    if (temp)
        psp_hardfile2 = strdup(temp);

    temp = PrefsFindString("disk", 3);
    if (temp)
        psp_hardfile3 = strdup(temp);

    temp = PrefsFindString("cdrom");
    if (temp)
        if (strncmp(temp, "placeholder", 11))
            psp_cdromfile = strdup(temp);

    temp = PrefsFindString("floppy");
    if (temp)
        if (strncmp(temp, "placeholder", 11))
            psp_floppyfile = strdup(temp);

    if (PrefsFindString("extfs"))
        psp_extfs_enable = 1;

    psp_cpu_speed = PrefsFindInt32("pspspeed");

    psp_lcd_aspect = PrefsFindInt32("pspdar");

    if (PrefsFindBool("relaxed60hz"))
        psp_60hz_timing = PrefsFindBool("relaxed60hz") ? 1 : 0;

    if (PrefsFindBool("reartouch"))
        psp_rear_touch_enable = PrefsFindBool("reartouch") ? 1 : 0;

    if (PrefsFindBool("indirecttouch"))
        psp_indirect_touch_enable = PrefsFindBool("indirecttouch") ? 1 : 0;

    if (PrefsFindInt32("pointerspeed"))
       psp_pointer_speed = PrefsFindInt32("pointerspeed");

    if (PrefsFindInt32("analogdeadzone"))
       psp_analog_deadzone = PrefsFindInt32("analogdeadzone");

    // YEAH!! Do that GUI!!
	do_gui(TopLevel, NULL, Get_String(STR_PREFS_ITEM_START));

    // set prefs from final values

    char scrnmodes[7][4][12] = {
        { "512/384/4", "512/384/8", "512/384/15", "512/384/24" },
        { "640/360/4", "640/360/8", "640/360/15", "640/360/24" },
        { "640/400/4", "640/400/8", "640/400/15", "640/400/24" },
        { "640/480/4", "640/480/8", "640/480/15", "640/480/24" },
        { "726/544/4", "726/544/8", "726/544/15", "726/544/24" },
        { "768/432/4", "768/432/8", "768/432/15", "768/432/24" },
        { "768/576/4", "768/576/8", "768/576/15", "768/576/24" }
    };
    PrefsReplaceString("screen", scrnmodes[psp_screen_mode][psp_screen_depth]);

    PrefsReplaceInt32("frameskip", psp_screen_rate + 1);

    PrefsReplaceBool("nosound", psp_sound_enable ? false : true);

    PrefsReplaceInt32("ramsize", psp_ram_size * 1024 * 1024);

    PrefsReplaceInt32("modelid", psp_model_id ? 14 : 5);

    PrefsReplaceInt32("cpu", psp_mac_cpu / 2 + 2);

    PrefsReplaceBool("fpu", psp_mac_cpu == 1 || psp_mac_cpu >= 3);

    if (psp_rom_file)
        PrefsReplaceString("rom", psp_rom_file);
    else
        PrefsRemoveItem("rom");

    // put serial stuff here eventually

    PrefsReplaceBool("udptunnel", psp_udp_enable == 1);

    PrefsReplaceInt32("udpport", psp_udp_port);

    PrefsReplaceInt32("udpapoint", psp_net_accesspoint);

    PrefsReplaceInt32("bootdriver", psp_boot_select);

    PrefsRemoveItem("disk");
    PrefsRemoveItem("disk");
    PrefsRemoveItem("disk");
    PrefsRemoveItem("disk");
    if (psp_hardfile0)
        PrefsAddString("disk", psp_hardfile0);
    if (psp_hardfile1)
        PrefsAddString("disk", psp_hardfile1);
    if (psp_hardfile2)
        PrefsAddString("disk", psp_hardfile2);
    if (psp_hardfile3)
        PrefsAddString("disk", psp_hardfile3);

    if (psp_cdromfile)
        PrefsReplaceString("cdrom", psp_cdromfile);
    else
        PrefsReplaceString("cdrom", "placeholder.bin");
    psp_cdrom_inserted = psp_cdromfile;

    if (psp_floppyfile)
        PrefsReplaceString("floppy", psp_floppyfile);
    else
        PrefsReplaceString("floppy", "placeholder.dsk");
    psp_floppy_inserted = psp_floppyfile;

    PrefsRemoveItem("extfs");
    if (psp_extfs_enable) {
        char temp_path[256];
        strcpy(temp_path, psp_home);
        strcat(temp_path, "files");
        PrefsReplaceString("extfs", temp_path);
    }

    PrefsReplaceInt32("pspspeed", psp_cpu_speed);

    PrefsReplaceInt32("pspdar", psp_lcd_aspect);

    PrefsReplaceBool("relaxed60hz", psp_60hz_timing ? true : false);

    PrefsReplaceBool("reartouch", psp_rear_touch_enable ? true : false);

    PrefsReplaceBool("indirecttouch", psp_indirect_touch_enable ? true : false);

    PrefsReplaceInt32("pointerspeed", psp_pointer_speed);

    PrefsReplaceInt32("analogdeadzone", psp_analog_deadzone);

    SavePrefs();

	return true;
}
