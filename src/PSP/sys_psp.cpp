/*
 *  sys_unix.cpp - System dependent routines, Unix implementation
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

#include <sys/ioctl.h>
#include <sys/stat.h>
#include <errno.h>


#include <pspkernel.h>
#include <pspctrl.h>
#include <pspdebug.h>
#include <pspsdk.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "main.h"
#include "macos_util.h"
#include "prefs.h"
#include "user_strings.h"
#include "sys.h"

#define DEBUG 0
#include "debug.h"


// File handles are pointers to these structures
struct file_handle {
	char *name;		    // Copy of device/file name
	int fd;
	bool is_file;		// Flag: plain file or /dev/something?
	bool is_floppy;		// Flag: floppy device
	bool is_cdrom;		// Flag: CD-ROM device
	bool read_only;		// Copy of Sys_open() flag
	loff_t start_byte;	// Size of file header (if any)
	loff_t file_size;	// Size of file data (only valid if is_file is true)
	int toc_fd;         // filedesc for cdrom TOC file
    char *toc_name;     // copy of filename for CD TOC
	file_handle *next;  // next handle in list
};

char *psp_floppy_inserted = NULL;           // String: filename of floppy inserted
char *psp_cdrom_inserted = NULL;            // String: filename of cdrom inserted
bool psp_cdrom_locked = false;              // Flag: cdrom lock

file_handle *floppy_fh = NULL;
file_handle *cdrom_fh = NULL;

file_handle *fh_list = NULL;	            // singly-linked list of all handles


/*
 *  Initialization
 */

void SysInit(void)
{
}


/*
 *  Deinitialization
 */

void SysExit(void)
{
}


/*
 *  This gets called when no "floppy" prefs items are found
 *  It scans for available floppy drives and adds appropriate prefs items
 */

void SysAddFloppyPrefs(void)
{
}


/*
 *  This gets called when no "disk" prefs items are found
 *  It scans for available HFS volumes and adds appropriate prefs items
 *	On OS X, we could do the same, but on an OS X machine I think it is
 *	very unlikely that any mounted volumes would contain a system which
 *	is old enough to boot a 68k Mac, so we just do nothing here for now.
 */

void SysAddDiskPrefs(void)
{
}


/*
 *  This gets called when no "cdrom" prefs items are found
 *  It scans for available CD-ROM drives and adds appropriate prefs items
 */

void SysAddCDROMPrefs(void)
{
	// Don't scan for drives if nocdrom option given
	if (PrefsFindBool("nocdrom"))
		return;
}


/*
 *  Add default serial prefs (must be added, even if no ports present)
 */

void SysAddSerialPrefs(void)
{
}


/*
 *  Open file/device, create new file handle (returns NULL on error)
 */

void *Sys_open(const char *name, bool read_only)
{
	bool is_file = false;

    // determine if hardfile - is_file will be set only if extension is "hfv" or if no extension
    char *tmp = strrchr(name, '.');
    if (tmp)
        is_file = !strncmp(tmp, ".hfv", 4);
    else
        is_file = true;

	D(bug("Sys_open(%s, %s)\n", name, read_only ? "read-only" : "read/write"));

    // check for cdrom/floppy placeholder
    if (!strncmp(name, "placeholder", 11))
    {
		file_handle *fh = new file_handle;
        fh->next = fh_list;
        fh_list = fh;
		fh->name = strdup(name);
		fh->fd = -1;
		fh->toc_fd = -1;
		fh->is_file = false;
		fh->read_only = read_only;
		fh->start_byte = 0;
		fh->is_floppy = !strncmp(tmp, ".dsk", 4);
		fh->is_cdrom = !fh->is_floppy;
		if (fh->is_floppy)
            floppy_fh = fh;
        else
            cdrom_fh = fh;
		return fh;
    }

	// Check if write access is allowed, set read-only flag if not
	if (!read_only && access(name, W_OK))
		read_only = true;

	// Open file/device
	int fd = open(name, read_only ? O_RDONLY : O_RDWR);

	if (fd < 0 && !read_only) {
		// Read-write failed, try read-only
		read_only = true;
		fd = open(name, O_RDONLY);
	}

	if (fd >= 0) {
		file_handle *fh = new file_handle;
        fh->next = fh_list;
        fh_list = fh;
		fh->name = strdup(name);
		fh->fd = fd;
		fh->toc_fd = -1;
		fh->is_file = is_file;
		fh->read_only = read_only;
		fh->start_byte = 0;
		fh->is_floppy = false;
		fh->is_cdrom = false;
		if (fh->is_file) {
			// Detect disk image file layout
			loff_t size = 0;
			size = lseek(fd, 0, SEEK_END);
			uint8 data[256];
			lseek(fd, 0, SEEK_SET);
			read(fd, data, 256);
			FileDiskLayout(size, data, fh->start_byte, fh->file_size);
		} else {
		    // determine if floppy or cdrom - is floppy only if extension is "dsk", otherwise is assumed to be a cdrom
		    char *tmp = strrchr(name, '.');
		    if (tmp)
		    {
		        fh->is_floppy = !strncmp(tmp, ".dsk", 4);
		        fh->is_cdrom = !fh->is_floppy;
                // Detect disk image file layout
                loff_t size = 0;
                size = lseek(fd, 0, SEEK_END);
                uint8 data[256];
                lseek(fd, 0, SEEK_SET);
                read(fd, data, 256);
                FileDiskLayout(size, data, fh->start_byte, fh->file_size);
		    }
		}
		if (fh->is_floppy)
            floppy_fh = fh;
        else if (fh->is_cdrom)
            cdrom_fh = fh;
		return fh;
	} else {
		printf("WARNING: Cannot open %s (%s)\n", name, strerror(errno));
		return NULL;
	}
}


/*
 *  Close file/device, delete file handle
 */

void Sys_close(void *arg)
{
	file_handle *fh = (file_handle *)arg;
	if (!fh)
		return;

    if (fh->fd >= 0)
        close(fh->fd);

	if (fh->name)
		free(fh->name);

	delete fh;
}


/*
 *  Read "length" bytes from file/device, starting at "offset", to "buffer",
 *  returns number of bytes read (or 0)
 */

size_t Sys_read(void *arg, void *buffer, loff_t offset, size_t length)
{
	file_handle *fh = (file_handle *)arg;
	if (!fh)
		return 0;
	if (fh->fd < 0)
		return 0;

	// Seek to position
	if (lseek(fh->fd, offset + fh->start_byte, SEEK_SET) < 0)
		return 0;

	// Read data
	return read(fh->fd, buffer, length);
}


/*
 *  Write "length" bytes from "buffer" to file/device, starting at "offset",
 *  returns number of bytes written (or 0)
 */

size_t Sys_write(void *arg, void *buffer, loff_t offset, size_t length)
{
	file_handle *fh = (file_handle *)arg;
	if (!fh)
		return 0;
	if (fh->fd < 0)
		return 0;

	// Seek to position
	if (lseek(fh->fd, offset + fh->start_byte, SEEK_SET) < 0)
		return 0;

	// Write data
	return write(fh->fd, buffer, length);
}


/*
 *  Return size of file/device (minus header)
 */

loff_t SysGetFileSize(void *arg)
{
	file_handle *fh = (file_handle *)arg;
	if (!fh)
		return 0;

	if (fh->is_file)
		return fh->file_size;
	else {
	    if (fh->is_floppy)
            return (loff_t)1440*512; // always high-density

        if (fh->fd < 0)
            return (loff_t)650*1024;

		return lseek(fh->fd, 0, SEEK_END) - fh->start_byte;
	}
}


/*
 *  Eject volume (if applicable)
 */

void SysEject(void *arg)
{
	file_handle *fh = (file_handle *)arg;
	if (!fh)
		return;

	if (fh->is_floppy) {
	    if (floppy_fh->fd >= 0) {
            close(floppy_fh->fd);
            floppy_fh->fd = -1;
	    }
	    psp_floppy_inserted = NULL;
	} else if (fh->is_cdrom) {
	    if (cdrom_fh->fd >= 0) {
            close(cdrom_fh->fd);
            cdrom_fh->fd = -1;
	    }
	    psp_cdrom_inserted = NULL;
	}
}


/*
 *  Format volume (if applicable)
 */

bool SysFormat(void *arg)
{
	file_handle *fh = (file_handle *)arg;
	if (!fh)
		return false;
	if (fh->fd < 0)
        return false;

	//!!
	return true;
}


/*
 *  Check if file/device is read-only (this includes the read-only flag on Sys_open())
 */

bool SysIsReadOnly(void *arg)
{
	file_handle *fh = (file_handle *)arg;
	if (!fh)
		return true;

	return fh->read_only;
}


/*
 *  Check if the given file handle refers to a fixed or a removable disk
 */

bool SysIsFixedDisk(void *arg)
{
	file_handle *fh = (file_handle *)arg;
	if (!fh)
		return true;

	if (fh->is_file)
		return true;
	else if (fh->is_floppy || fh->is_cdrom)
		return false;
	else
		return true;
}


/*
 *  Check if a disk is inserted in the drive (always true for files)
 */

bool SysIsDiskInserted(void *arg)
{
	file_handle *fh = (file_handle *)arg;
	if (!fh)
		return false;

	if (fh->is_file) {
		return true;

	} else if (fh->is_floppy) {
	    //return psp_floppy_inserted != NULL;
	    return fh->fd >= 0;

	} else if (fh->is_cdrom) {
	    //return psp_cdrom_inserted != NULL;
	    return fh->fd >= 0;

	} else
		return true;
}


/*
 *  Prevent medium removal (if applicable)
 */

void SysPreventRemoval(void *arg)
{
	file_handle *fh = (file_handle *)arg;
	if (!fh)
		return;

	if (fh->is_cdrom)
		psp_cdrom_locked = true;
}


/*
 *  Allow medium removal (if applicable)
 */

void SysAllowRemoval(void *arg)
{
	file_handle *fh = (file_handle *)arg;
	if (!fh)
		return;

	if (fh->is_cdrom)
		psp_cdrom_locked = false;
}


/*
 *  Read CD-ROM TOC (binary MSF format, 804 bytes max.)
 */

bool SysCDReadTOC(void *arg, uint8 *toc)
{
	file_handle *fh = (file_handle *)arg;
	if (!fh)
		return false;
	if (fh->fd < 0)
		return false;

	if (fh->is_cdrom) {
	    // need to add TOC handling!
		return false;
	} else
		return false;
}


/*
 *  Read CD-ROM position data (Sub-Q Channel, 16 bytes, see SCSI standard)
 */

bool SysCDGetPosition(void *arg, uint8 *pos)
{
	file_handle *fh = (file_handle *)arg;
	if (!fh)
		return false;
	if (fh->fd < 0)
		return false;

	if (fh->is_cdrom) {
		return false;
	} else
		return false;
}


/*
 *  Play CD audio
 */

bool SysCDPlay(void *arg, uint8 start_m, uint8 start_s, uint8 start_f, uint8 end_m, uint8 end_s, uint8 end_f)
{
	file_handle *fh = (file_handle *)arg;
	if (!fh)
		return false;
	if (fh->fd < 0)
		return false;

	if (fh->is_cdrom) {
		return false;
	} else
		return false;
}


/*
 *  Pause CD audio
 */

bool SysCDPause(void *arg)
{
	file_handle *fh = (file_handle *)arg;
	if (!fh)
		return false;
	if (fh->fd < 0)
		return false;

	if (fh->is_cdrom) {
		return false;
	} else
		return false;
}


/*
 *  Resume paused CD audio
 */

bool SysCDResume(void *arg)
{
	file_handle *fh = (file_handle *)arg;
	if (!fh)
		return false;
	if (fh->fd < 0)
		return false;

	if (fh->is_cdrom) {
		return false;
	} else
		return false;
}


/*
 *  Stop CD audio
 */

bool SysCDStop(void *arg, uint8 lead_out_m, uint8 lead_out_s, uint8 lead_out_f)
{
	file_handle *fh = (file_handle *)arg;
	if (!fh)
		return false;
	if (fh->fd < 0)
		return false;

	if (fh->is_cdrom) {
		return false;
	} else
		return false;
}


/*
 *  Perform CD audio fast-forward/fast-reverse operation starting from specified address
 */

bool SysCDScan(void *arg, uint8 start_m, uint8 start_s, uint8 start_f, bool reverse)
{
	file_handle *fh = (file_handle *)arg;
	if (!fh)
		return false;
	if (fh->fd < 0)
		return false;

	// Not supported
	return false;
}


/*
 *  Set CD audio volume (0..255 each channel)
 */

void SysCDSetVolume(void *arg, uint8 left, uint8 right)
{
	file_handle *fh = (file_handle *)arg;
	if (!fh)
		return;

	if (fh->is_cdrom) {
        //!
	}
}


/*
 *  Get CD audio volume (0..255 each channel)
 */

void SysCDGetVolume(void *arg, uint8 &left, uint8 &right)
{
	file_handle *fh = (file_handle *)arg;
	if (!fh)
		return;

	left = right = 0;
	if (fh->is_cdrom) {
        //!
	}
}

/*
 * Suspend / Resume functions
 */

void Sys_drive_stop(void)
{
    file_handle *fh = fh_list;
    while (fh)
    {
        if (fh->fd >= 0)
            close(fh->fd); // is open - close
        if (fh->toc_fd >= 0)
            close(fh->toc_fd);
        fh = fh->next;
    }
}

void Sys_drive_restart(void)
{
    file_handle *fh = fh_list;
    while (fh)
    {
        if (fh->fd >= 0)
            fh->fd = open(fh->name, fh->read_only ? O_RDONLY : O_RDWR); // was open previously - reopen
        if (fh->toc_fd >= 0)
            fh->toc_fd = open(fh->toc_name, O_RDONLY); // was open previously - reopen
        fh = fh->next;
    }
}
