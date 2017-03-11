/*
 *  extfs_unix.cpp - MacOS file system for access native file system access, Unix specific stuff
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

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>

#include "sysdeps.h"
#include "extfs.h"
#include "extfs_defs.h"

#define DEBUG 0
#include "debug.h"


extern int ftruncate(int fildes, off_t length);


// Default Finder flags
const uint16 DEFAULT_FINDER_FLAGS = kHasBeenInited;

// AppleSingle/Double entry IDs
#define DATA_FORK   1
#define RSRC_FORK   2
#define REAL_NAME   3
#define COMMENT     4
#define ICON_BW     5
#define ICON_COLOR  6
#define FILE_INFO   7
#define FINDER_INFO 9

// AppleSingle/Double magic numbers
#define MAGIC_SINGLE 0x00051600
#define MAGIC_DOUBLE 0x00051607

// AppleSingle/Double versions
#define VERSION_AUX 0x00010000
#define VERSION_OSX 0x00020000

// AppleSingle/Double structures

struct EntryDescriptors {
    uint32 id;
    uint32 offset;
    uint32 length;
};

struct AppleSingle {
    uint32 magicNumber;
    uint32 version;
    char home[16];
    uint16 numEntries;
} __attribute__((packed));


static AppleSingle aHeader;
static EntryDescriptors aEntries[8];

static int helpers[16][3]; // fd based table of index, offset, and length

#define XFS_INDEX(x) helpers[x][0]
#define XFS_OFFSET(x) helpers[x][1]
#define XFS_LENGTH(x) helpers[x][2]

#if 0
#define LOGGING
#define LOG(...) fprintf(log, __VA_ARGS__)
static FILE *log;
#else
#define LOG(...)
#endif

/*
 *  Initialization
 */

void extfs_init(void)
{
#ifdef LOGGING
    log = fopen("errorlog.txt", "w");
#endif
}


/*
 *  Deinitialization
 */

void extfs_exit(void)
{
#ifdef LOGGING
    fclose(log);
#endif
}


/*
 *  Add component to path name
 */

void add_path_component(char *path, const char *component)
{
	int l = strlen(path);
	if (l < MAX_PATH_LENGTH-1 && path[l-1] != '/') {
		path[l] = '/';
		path[l+1] = 0;
	}
	strncat(path, component, MAX_PATH_LENGTH-1);
}


/*
 *  Helper support
 */

static int open_helper(const char *path, int id, int flag)
{
	char helper_path[MAX_PATH_LENGTH];
	char xpath[MAX_PATH_LENGTH];
	int len, i;

    LOG("Opening %s of %s\n", (id==FINDER_INFO) ? "finder info" : (id==RSRC_FORK) ? "resource fork" : "???", path);

    strncpy(xpath, path, MAX_PATH_LENGTH);
	if (xpath[strlen(xpath) - 1] == '/')	// Remove trailing "/" so that directories can also have finf
		xpath[strlen(xpath) - 1] = 0;

    // generate AppleDouble filename from the original filename
    char *temp = strrchr(xpath, '/');
    if (temp == NULL)
    {
        strcpy(helper_path, "._");
        strncat(helper_path, xpath, MAX_PATH_LENGTH);
    }
    else
    {
        strncpy(helper_path, xpath, (int)temp - (int)xpath + 1);
        helper_path[(int)temp - (int)xpath + 1] = 0;
        strcat(helper_path, "._");
        strcat(helper_path, &temp[1]);
    }

	if ((flag & O_ACCMODE) == O_RDWR || (flag & O_ACCMODE) == O_WRONLY)
		flag |= O_CREAT;
	int fd = open(helper_path, flag, 0777);
	if (fd < 0)
        goto err;

    len = read(fd, (void *)&aHeader, sizeof(aHeader));
    if (len < sizeof(aHeader))
    {
        // just created, make a "blank" AppleDouble header with entries
        LOG("Created new AppleDouble file\n");
        aHeader.magicNumber = do_byteswap_32(MAGIC_DOUBLE);
        aHeader.version = do_byteswap_32(VERSION_OSX);
        strcpy(aHeader.home, "Mac OS X        ");
        aHeader.numEntries = do_byteswap_16(2);
        aEntries[0].id = do_byteswap_32(FINDER_INFO);
        aEntries[0].offset = do_byteswap_32(0x32);
        aEntries[0].length = do_byteswap_32(32);
        aEntries[1].id = do_byteswap_32(RSRC_FORK);
        aEntries[1].offset = do_byteswap_32(0x52);
        aEntries[1].length = do_byteswap_32(0);
        lseek(fd, 0, SEEK_SET);
        write(fd, (void *)&aHeader, sizeof(aHeader));
        write(fd, (void *)aEntries, 2 * sizeof(EntryDescriptors));
        len = 0;
        for (i=0; i<8; i++)
            write(fd, (void *)&len, 4);
        lseek(fd, sizeof(aHeader), SEEK_SET);
    }

    len = read(fd, (void *)aEntries, do_byteswap_16(aHeader.numEntries) * sizeof(EntryDescriptors));
    if (len == do_byteswap_16(aHeader.numEntries) * sizeof(EntryDescriptors))
    {
        for (i=0; i<do_byteswap_16(aHeader.numEntries); i++)
            if (do_byteswap_32(aEntries[i].id) == id)
                break;
        if (i != do_byteswap_16(aHeader.numEntries))
        {
            XFS_INDEX(fd) = i;
            XFS_OFFSET(fd) = do_byteswap_32(aEntries[i].offset);
            XFS_LENGTH(fd) = do_byteswap_32(aEntries[i].length);
            LOG("fd = %d, index = %d, offset = %d, length = %d\n", fd, XFS_INDEX(fd), XFS_OFFSET(fd), XFS_LENGTH(fd));
            return fd;
        }
    }
err:
    // not present or some other error
    LOG("Err - helper_fd = %d\n", fd);
    return fd;
}

static int open_finf(const char *path, int flag)
{
	int fd = open_helper(path, FINDER_INFO, flag);
    if (fd >= 0)
        lseek(fd, XFS_OFFSET(fd), SEEK_SET); // go to start of data
	return fd;
}

static int open_rsrc(const char *path, int flag)
{
    int fd = open_helper(path, RSRC_FORK, flag);
    if (fd >= 0)
        lseek(fd, XFS_OFFSET(fd), SEEK_SET); // go to start of data
	return fd;
}

static void update_helper(int fd)
{
    off_t curr = lseek(fd, 0, SEEK_CUR);
    off_t size = lseek(fd, 0, SEEK_END);

    // update the resource fork entry in the file - slows writing, but it's just the resource fork... who cares?
    aEntries[XFS_INDEX(fd)].length = do_byteswap_32(size - (off_t)XFS_OFFSET(fd));
    lseek(fd, sizeof(aHeader) + XFS_INDEX(fd) * sizeof(EntryDescriptors), SEEK_SET);
    write(fd, (void *)&aEntries[XFS_INDEX(fd)], sizeof(EntryDescriptors));

    // return to where we were
    lseek(fd, curr, SEEK_SET);
}

/*
 *  Get/set finder info for file/directory specified by full path
 */

struct ext2type {
	const char *ext;
	uint32 type;
	uint32 creator;
};

static const ext2type e2t_translation[] = {
	{".Z", FOURCC('Z','I','V','M'), FOURCC('L','Z','I','V')},
	{".gz", FOURCC('G','z','i','p'), FOURCC('G','z','i','p')},
	{".hqx", FOURCC('T','E','X','T'), FOURCC('S','I','T','x')},
	{".bin", FOURCC('T','E','X','T'), FOURCC('S','I','T','x')},
	{".pdf", FOURCC('P','D','F',' '), FOURCC('C','A','R','O')},
	{".ps", FOURCC('T','E','X','T'), FOURCC('t','t','x','t')},
	{".sit", FOURCC('S','I','T','!'), FOURCC('S','I','T','x')},
	{".tar", FOURCC('T','A','R','F'), FOURCC('T','A','R',' ')},
	{".uu", FOURCC('T','E','X','T'), FOURCC('S','I','T','x')},
	{".uue", FOURCC('T','E','X','T'), FOURCC('S','I','T','x')},
	{".zip", FOURCC('Z','I','P',' '), FOURCC('Z','I','P',' ')},
	{".8svx", FOURCC('8','S','V','X'), FOURCC('S','N','D','M')},
	{".aifc", FOURCC('A','I','F','C'), FOURCC('T','V','O','D')},
	{".aiff", FOURCC('A','I','F','F'), FOURCC('T','V','O','D')},
	{".au", FOURCC('U','L','A','W'), FOURCC('T','V','O','D')},
	{".mid", FOURCC('M','I','D','I'), FOURCC('T','V','O','D')},
	{".midi", FOURCC('M','I','D','I'), FOURCC('T','V','O','D')},
	{".mp2", FOURCC('M','P','G',' '), FOURCC('T','V','O','D')},
	{".mp3", FOURCC('M','P','G',' '), FOURCC('T','V','O','D')},
	{".wav", FOURCC('W','A','V','E'), FOURCC('T','V','O','D')},
	{".bmp", FOURCC('B','M','P','f'), FOURCC('o','g','l','e')},
	{".gif", FOURCC('G','I','F','f'), FOURCC('o','g','l','e')},
	{".lbm", FOURCC('I','L','B','M'), FOURCC('G','K','O','N')},
	{".ilbm", FOURCC('I','L','B','M'), FOURCC('G','K','O','N')},
	{".jpg", FOURCC('J','P','E','G'), FOURCC('o','g','l','e')},
	{".jpeg", FOURCC('J','P','E','G'), FOURCC('o','g','l','e')},
	{".pict", FOURCC('P','I','C','T'), FOURCC('o','g','l','e')},
	{".png", FOURCC('P','N','G','f'), FOURCC('o','g','l','e')},
	{".sgi", FOURCC('.','S','G','I'), FOURCC('o','g','l','e')},
	{".tga", FOURCC('T','P','I','C'), FOURCC('o','g','l','e')},
	{".tif", FOURCC('T','I','F','F'), FOURCC('o','g','l','e')},
	{".tiff", FOURCC('T','I','F','F'), FOURCC('o','g','l','e')},
	{".htm", FOURCC('T','E','X','T'), FOURCC('M','O','S','S')},
	{".html", FOURCC('T','E','X','T'), FOURCC('M','O','S','S')},
	{".txt", FOURCC('T','E','X','T'), FOURCC('t','t','x','t')},
	{".rtf", FOURCC('T','E','X','T'), FOURCC('M','S','W','D')},
	{".c", FOURCC('T','E','X','T'), FOURCC('R','*','c','h')},
	{".C", FOURCC('T','E','X','T'), FOURCC('R','*','c','h')},
	{".cc", FOURCC('T','E','X','T'), FOURCC('R','*','c','h')},
	{".cpp", FOURCC('T','E','X','T'), FOURCC('R','*','c','h')},
	{".cxx", FOURCC('T','E','X','T'), FOURCC('R','*','c','h')},
	{".h", FOURCC('T','E','X','T'), FOURCC('R','*','c','h')},
	{".hh", FOURCC('T','E','X','T'), FOURCC('R','*','c','h')},
	{".hpp", FOURCC('T','E','X','T'), FOURCC('R','*','c','h')},
	{".hxx", FOURCC('T','E','X','T'), FOURCC('R','*','c','h')},
	{".s", FOURCC('T','E','X','T'), FOURCC('R','*','c','h')},
	{".S", FOURCC('T','E','X','T'), FOURCC('R','*','c','h')},
	{".i", FOURCC('T','E','X','T'), FOURCC('R','*','c','h')},
	{".mpg", FOURCC('M','P','E','G'), FOURCC('T','V','O','D')},
	{".mpeg", FOURCC('M','P','E','G'), FOURCC('T','V','O','D')},
	{".mov", FOURCC('M','o','o','V'), FOURCC('T','V','O','D')},
	{".fli", FOURCC('F','L','I',' '), FOURCC('T','V','O','D')},
	{".avi", FOURCC('V','f','W',' '), FOURCC('T','V','O','D')},
	{".qxd", FOURCC('X','D','O','C'), FOURCC('X','P','R','3')},
	{".hfv", FOURCC('D','D','i','m'), FOURCC('d','d','s','k')},
	{".dsk", FOURCC('D','D','i','m'), FOURCC('d','d','s','k')},
	{".img", FOURCC('r','o','h','d'), FOURCC('d','d','s','k')},
	{NULL, 0, 0}	// End marker
};

void get_finfo(const char *path, uint32 finfo, uint32 fxinfo, bool is_dir)
{
	// Set default finder info
	Mac_memset(finfo, 0, SIZEOF_FInfo);
	if (fxinfo)
		Mac_memset(fxinfo, 0, SIZEOF_FXInfo);
	WriteMacInt16(finfo + fdFlags, DEFAULT_FINDER_FLAGS);
	WriteMacInt32(finfo + fdLocation, (uint32)-1);

	// Read Finder info file
	int fd = open_finf(path, O_RDONLY);
	if (fd >= 0) {
		ssize_t actual = read(fd, Mac2HostAddr(finfo), SIZEOF_FInfo);
		if (fxinfo)
			actual += read(fd, Mac2HostAddr(fxinfo), SIZEOF_FXInfo);
		close(fd);
        LOG("Returning finfo for %d\n", fd);
		if (actual >= SIZEOF_FInfo)
			return;
	}

    LOG("Getting finfo from table for %d\n", fd);
	// No Finder info file, translate file name extension to MacOS type/creator
	if (!is_dir) {
		int path_len = strlen(path);
		for (int i=0; e2t_translation[i].ext; i++) {
			int ext_len = strlen(e2t_translation[i].ext);
			if (path_len < ext_len)
				continue;
			if (!strcmp(path + path_len - ext_len, e2t_translation[i].ext)) {
				WriteMacInt32(finfo + fdType, e2t_translation[i].type);
				WriteMacInt32(finfo + fdCreator, e2t_translation[i].creator);
				break;
			}
		}
	}
}

void set_finfo(const char *path, uint32 finfo, uint32 fxinfo, bool is_dir)
{
	// Open Finder info file
	int fd = open_finf(path, O_RDWR);
	if (fd < 0)
		return;

    LOG("Setting finfo for %d\n", fd);
	// Write file
	write(fd, Mac2HostAddr(finfo), SIZEOF_FInfo);
	if (fxinfo)
		write(fd, Mac2HostAddr(fxinfo), SIZEOF_FXInfo);
	close(fd);
}


/*
 *  Resource fork emulation functions
 */

uint32 get_rfork_size(const char *path)
{
	// Open resource file
	int fd = open_rsrc(path, O_RDONLY);
	if (fd < 0)
		return 0;

    LOG("Getting fork size for %d (%d)\n", fd, XFS_LENGTH(fd));
	// Close file and return size
	close(fd);
	return XFS_LENGTH(fd);
}

int open_dfork(const char *path, int flag)
{
    int fd = open(path, flag);

    if (fd >= 0)
    {
        LOG("Opening data fork %d\n", fd);
        XFS_INDEX(fd) = -1;
        XFS_OFFSET(fd) = 0;
    }

    return fd;
}

int open_rfork(const char *path, int flag)
{
	return open_rsrc(path, flag);
}

void close_rfork(const char *path, int fd)
{
    LOG("Closing resource fork %d\n", fd);
	close(fd);
}


/*
 *  Read "length" bytes from file to "buffer",
 *  returns number of bytes read (or -1 on error)
 */

ssize_t extfs_read(int fd, void *buffer, size_t length)
{
    LOG("Reading file %d (%d)\n", fd, length);
	return read(fd, buffer, length);
}


/*
 *  Write "length" bytes from "buffer" to file,
 *  returns number of bytes written (or -1 on error)
 */

ssize_t extfs_write(int fd, void *buffer, size_t length)
{
    LOG("Writing file %d (%d)\n", fd, length);
	int len = write(fd, buffer, length);

	if (XFS_INDEX(fd) >= 0)
        update_helper(fd);

    return len;
}

/*
 *  Seek to offset in the file,
 *  returns position after seek
 */

off_t extfs_seek(int fd, off_t offset, int whence)
{
    LOG("Seeking file %d (%d/%d)\n", fd, offset, whence);
    if (whence == SEEK_SET)
        return lseek(fd, offset + (off_t)XFS_OFFSET(fd), whence) - (off_t)XFS_OFFSET(fd);

    return lseek(fd, offset, whence) - (off_t)XFS_OFFSET(fd);
}

/*
 *  Get file status
 */

int extfs_fstat(int fd, struct stat *buf)
{
    int ret = fstat(fd, buf);

    if (XFS_INDEX(fd) >= 0)
        buf->st_size -= XFS_OFFSET(fd);

    return ret;
}

int extfs_ftruncate(int fd, off_t length)
{
    return ftruncate(fd, length + XFS_OFFSET(fd));
}

/*
 *  Remove file/directory (and associated helper files),
 *  returns false on error (and sets errno)
 */

bool extfs_remove(const char *path)
{
	// Remove helper first, don't complain if this fails
	char helper_path[MAX_PATH_LENGTH];
	char xpath[MAX_PATH_LENGTH];

    strncpy(xpath, path, MAX_PATH_LENGTH);
	if (xpath[strlen(xpath) - 1] == '/')	// Remove trailing "/" so that directories can also have finf
		xpath[strlen(xpath) - 1] = 0;

    // generate AppleDouble filename from the original filename
    char *temp = strrchr(xpath, '/');
    if (temp == NULL)
    {
        strcpy(helper_path, "._");
        strncat(helper_path, xpath, MAX_PATH_LENGTH);
    }
    else
    {
        strncpy(helper_path, xpath, (int)temp - (int)xpath + 1);
        helper_path[(int)temp - (int)xpath + 1] = 0;
        strcat(helper_path, "._");
        strcat(helper_path, &temp[1]);
    }

	remove(helper_path);

	// Now remove file or directory
	if (remove(path) < 0)
			return false;

	return true;
}


/*
 *  Rename/move file/directory (and associated helper files),
 *  returns false on error (and sets errno)
 */

bool extfs_rename(const char *old_path, const char *new_path)
{
	// Rename helper first, don't complain if this fails
	char old_helper_path[MAX_PATH_LENGTH], new_helper_path[MAX_PATH_LENGTH];
	char xpath[MAX_PATH_LENGTH];

    strncpy(xpath, old_path, MAX_PATH_LENGTH);
	if (xpath[strlen(xpath) - 1] == '/')	// Remove trailing "/" so that directories can also have finf
		xpath[strlen(xpath) - 1] = 0;

    // generate AppleDouble filename from the original filename
    char *temp = strrchr(xpath, '/');
    if (temp == NULL)
    {
        strcpy(old_helper_path, "._");
        strncat(old_helper_path, xpath, MAX_PATH_LENGTH);
    }
    else
    {
        strncpy(old_helper_path, xpath, (int)temp - (int)xpath + 1);
        old_helper_path[(int)temp - (int)xpath + 1] = 0;
        strcat(old_helper_path, "._");
        strcat(old_helper_path, &temp[1]);
    }

    strncpy(xpath, new_path, MAX_PATH_LENGTH);
	if (xpath[strlen(xpath) - 1] == '/')	// Remove trailing "/" so that directories can also have finf
		xpath[strlen(xpath) - 1] = 0;

    // generate AppleDouble filename from the original filename
    temp = strrchr(xpath, '/');
    if (temp == NULL)
    {
        strcpy(new_helper_path, "._");
        strncat(new_helper_path, xpath, MAX_PATH_LENGTH);
    }
    else
    {
        strncpy(new_helper_path, xpath, (int)temp - (int)xpath + 1);
        new_helper_path[(int)temp - (int)xpath + 1] = 0;
        strcat(new_helper_path, "._");
        strcat(new_helper_path, &temp[1]);
    }

	rename(old_helper_path, new_helper_path);

	// Now rename file
	return rename(old_path, new_path) == 0;
}

// Convert from the host OS filename encoding to MacRoman
const char *host_encoding_to_macroman(const char *filename)
{
	return filename;
}

// Convert from MacRoman to host OS filename encoding
const char *macroman_to_host_encoding(const char *filename)
{
	return filename;
}
