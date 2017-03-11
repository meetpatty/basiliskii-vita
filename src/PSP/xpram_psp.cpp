/*
 *  xpram_psp.cpp - XPRAM handling, PSP specific stuff
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

#include "sysdeps.h"
#include "xpram.h"


// XPRAM file name and path
const char XPRAM_FILE_NAME[] = "BasiliskII_XPRAM";


/*
 *  Load XPRAM from settings file
 */

void LoadXPRAM(void)
{
    int fd = sceIoOpen(XPRAM_FILE_NAME, PSP_O_RDONLY, 0777);
    if (fd > 0) {
        sceIoRead(fd, XPRAM, XPRAM_SIZE);
        sceIoClose(fd);
    }
}


/*
 *  Save XPRAM to settings file
 */

void SaveXPRAM(void)
{
    int fd = sceIoOpen(XPRAM_FILE_NAME, PSP_O_CREAT | PSP_O_TRUNC | PSP_O_WRONLY, 0777);
    if (fd > 0) {
        sceIoWrite(fd, XPRAM, XPRAM_SIZE);
        sceIoClose(fd);
    }
}


/*
 *  Delete PRAM file
 */

void ZapPRAM(void)
{
    sceIoRemove(XPRAM_FILE_NAME);
}
