/*
 *  user_strings_psp.cpp - PSP-specific localizable strings
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

#include "sysdeps.h"
#include "user_strings.h"
#include "user_strings_psp.h"


// Platform-specific string definitions
user_string_def platform_strings[] = {
	// Common strings that have a platform-specific variant
	{STR_EXTFS_CTRL, "PSP MemStick"},
	{STR_EXTFS_NAME, "PSP MemStick Directory Tree"},
	{STR_EXTFS_VOLUME_NAME, "MS0"},

	{STR_CREATE_VOLUME_PANEL_BUTTON, "Create Hardfile"},

	{STR_GUI_ERROR_PREFIX, "Basilisk II error"},
	{STR_GUI_WARNING_PREFIX, "Basilisk II warning"},
	{STR_ROM_SIZE_ERR, "You can only use a 512K or 1MB Mac ROM."},

	// Purely platform-specific strings
    {STR_MENU_BACK, "Previous Menu"},
    {STR_MENU_MENU, "X = Enter Sub-Menu"},
    {STR_MENU_FUNCTION, "X = Do Function"},
    {STR_MENU_SELECT, "X = Hold to change Selection with D-Pad"},
    {STR_MENU_TOGGLE, "X = Toggle Flag"},
    {STR_MENU_INTEGER, "X = Hold to change Integer with D-Pad"},
    {STR_MENU_FILE, "X = Select File"},
    {STR_MENU_STRING, "X = Input String"},
    {STR_MENU_ON, "on"},
    {STR_MENU_OFF, "off"},

    {STR_PSP_SERIAL, "PSP Serial"},
    {STR_PSP_IRDA, "PSP IRDA"},
    {STR_ACCESS_POINT, "Network Access Point"},

	{STR_PSP_PANE_TITLE, "PSP Specific Settings"},
	{STR_PSP_CPU_FREQ, "PSP CPU Clock Frequency"},
	{STR_PSP_VIDEO_OUT, "PSP Video Output"},
	{STR_PSP_LCD_PANE_TITLE, "PSP LCD Settings"},
	{STR_PSP_TV_PANE_TITLE, "PSP TV Settings"},
	{STR_PSP_MONITOR_ASPECT, "Monitor Aspect Ratio"},
	{STR_PSP_MONITOR_LACED, "Monitor uses Interlace"},
	{STR_PSP_DISPLAY_ASPECT, "Display Aspect Ratio"},
	{STR_PSP_OVERSCAN, "Draw in Overscan Region"},
	{STR_PSP_RELAXED_60HZ, "Relaxed timing for 60 Hz IRQ"},

    {STR_TICK_THREAD_ERR, "Cannot create thread for ticks."},
    {STR_NO_NET_THREAD_ERR, "Cannot create network thread."},
	{STR_NO_ACCESS_POINT_ERR, "Cannot connect to Access Point."},
	{STR_DANZEFF_ERR, "Cannot initialize Danzeff OSK."},

    {STR_BOOT_FLOPPY_LAB, "Floppy Disk"},
    {STR_CREATE_FLOPPY_PANEL_BUTTON, "Create 1.4 MB Floppy"},

	{-1, NULL}	// End marker
};


/*
 *  Fetch pointer to string, given the string number
 */

const char *GetString(int num)
{
	// First search for platform-specific string
	int i = 0;
	while (platform_strings[i].num >= 0) {
		if (platform_strings[i].num == num)
			return platform_strings[i].str;
		i++;
	}

	// Not found, search for common string
	i = 0;
	while (common_strings[i].num >= 0) {
		if (common_strings[i].num == num)
			return common_strings[i].str;
		i++;
	}
	return NULL;
}
