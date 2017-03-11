#include <pspkernel.h>
#include <pspctrl.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <pspgum.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>

#include "intraFont.h"

#define printf pspDebugScreenPrintf


#define MAXFILES 256
#define PAGESIZE 16

struct fileentries {
	char filename[FILENAME_MAX];
	char path[FILENAME_MAX];
	int flags;
};

static struct fileentries thefiles[MAXFILES];

static int maxfiles;


/****************************************************************************
 * get_buttons
 *
 ****************************************************************************/

static unsigned int get_buttons()
{
	SceCtrlData pad;

	sceCtrlReadBufferPositive(&pad, 1);
	return pad.Buttons;
}

/****************************************************************************
 * ParseDirectory
 *
 * Parse the directory, returning the number of files found
 ****************************************************************************/

int parse_dir (char *path, struct fileentries *the_files, int maxentries)
{
	DIR *dir;
	DIR *test_dir;
	struct dirent *dirent = 0;
	struct stat fstat;
	char file_name[FILENAME_MAX];
	FILE *file;
	int i;
	int maxfiles = 0;

	/* open directory */
	if ( ( dir = opendir( path ) ) == 0 )
		return 0;

	while ( ( dirent = readdir( dir ) ) != 0 )
	{
		if ( dirent->d_name[0] == '.' ) continue;
		/* get stats */
		sprintf( file_name, "%s/%s", path, dirent->d_name );
		if ( stat( file_name, &fstat ) == -1 ) continue;
		/* check directory */
		if ( S_ISDIR( fstat.st_mode ) )
		{
			if ( ( test_dir = opendir( file_name ) ) == 0  ) continue;
			closedir( test_dir );
			memset (&the_files[maxfiles], 0, sizeof (struct fileentries));
			strncpy(the_files[maxfiles].path, path, FILENAME_MAX);
			the_files[maxfiles].path[FILENAME_MAX-1] = 0;
			strncpy(the_files[maxfiles].filename, dirent->d_name, FILENAME_MAX);
			the_files[maxfiles].filename[FILENAME_MAX-1] = 0;
			the_files[maxfiles].flags = 1;
			maxfiles++;
		}
		else
		/* check regular file */
		if ( S_ISREG( fstat.st_mode ) )
		{
			/* test it */
			if ( ( file = fopen( file_name, "r" ) ) == 0 ) continue;
			fclose( file );
			memset (&the_files[maxfiles], 0, sizeof (struct fileentries));
			strncpy(the_files[maxfiles].path, path, FILENAME_MAX);
			the_files[maxfiles].path[FILENAME_MAX-1] = 0;
			strncpy(the_files[maxfiles].filename, dirent->d_name, FILENAME_MAX);
			the_files[maxfiles].filename[FILENAME_MAX-1] = 0;
			maxfiles++;
		}

		if (maxfiles == maxentries)
			break;
	}
	/* close dir */
	closedir( dir );

	// sort them!
	for (i=0; i<maxfiles-1; i++)
	{
		char tempfilename[FILENAME_MAX];
		char temppath[FILENAME_MAX];
		int tempflags;

		if ((!the_files[i].flags && the_files[i+1].flags) || // directories first
			(the_files[i].flags && the_files[i+1].flags && strcasecmp(the_files[i].filename, the_files[i+1].filename) > 0) ||
			(!the_files[i].flags && !the_files[i+1].flags && strcasecmp(the_files[i].filename, the_files[i+1].filename) > 0))
		{
			strcpy(tempfilename, the_files[i].filename);
			strcpy(temppath, the_files[i].path);
			tempflags = the_files[i].flags;
			strcpy(the_files[i].filename, the_files[i+1].filename);
			strcpy(the_files[i].path, the_files[i+1].path);
			the_files[i].flags = the_files[i+1].flags;
			strcpy(the_files[i+1].filename, tempfilename);
			strcpy(the_files[i+1].path, temppath);
			the_files[i+1].flags = tempflags;
			i = -1;
		}
	}

	return maxfiles;
}

/****************************************************************************
 * ShowFiles
 *
 * Support function for FileSelector
 ****************************************************************************/

extern void gui_PrePrint(void);
extern void gui_PostPrint(void);
extern int gui_PrintWidth(char *text);
extern void gui_Print(char *text, u32 fc, u32 bc, int x, int y);

void ShowFiles( int offset, int selection )
{
	int i,j;
	char text[80];

	gui_PrePrint();

	j = 0;
	for ( i = offset; i < ( offset + PAGESIZE ) && i < maxfiles ; i++ )
	{
		if ( thefiles[i].flags )
		{
			strcpy(text,"[");
			strncat(text, thefiles[i].filename,66);
			strcat(text,"]");
		}
		else
			strncpy(text, thefiles[i].filename, 68);
		text[68]=0;

		gui_Print(text, j == (selection-offset) ? 0xFFFFFFFF : 0xFFAAAAAA, 0xFF000000, 240 - gui_PrintWidth(text)/2, (i - offset + 1)*16);

		j++;
	}

	gui_PostPrint();
}

/****************************************************************************
 * FileSelector
 *
 * Press X to select, O to cancel, and Triangle to go back a level
 ****************************************************************************/

int FileSelector()
{
	int offset = 0;
	int selection = 0;
	int havefile = 0;
	int redraw = 1;
	unsigned int p = get_buttons();

	while ( havefile == 0 && !(p & PSP_CTRL_CIRCLE) )
	{
		if ( redraw )
			ShowFiles( offset, selection );
		redraw = 0;

		while (!(p = get_buttons()))
			sceKernelDelayThread(10000);
		while (p == get_buttons())
			sceKernelDelayThread(10000);

		if ( p & PSP_CTRL_DOWN )
		{
			selection++;
			if ( selection == maxfiles )
				selection = offset = 0;	// wrap around to top

			if ( ( selection - offset ) == PAGESIZE )
				offset += PAGESIZE; // next "page" of entries

			redraw = 1;
		}

		if ( p & PSP_CTRL_UP )
		{
			selection--;
			if ( selection < 0 )
			{
				selection = maxfiles - 1;
				offset = maxfiles > PAGESIZE ? selection - PAGESIZE + 1 : 0; // wrap around to bottom
			}

			if ( selection < offset )
			{
				offset -= PAGESIZE; // previous "page" of entries
				if ( offset < 0 )
					offset = 0;
			}

			redraw = 1;
		}

		if ( p & PSP_CTRL_RIGHT )
		{
			selection += PAGESIZE;
			if ( selection >= maxfiles )
				selection = offset = 0;	// wrap around to top

			if ( ( selection - offset ) >= PAGESIZE )
				offset += PAGESIZE; // next "page" of entries

			redraw = 1;
		}

		if ( p & PSP_CTRL_LEFT )
		{
			selection -= PAGESIZE;
			if ( selection < 0 )
			{
				selection = maxfiles - 1;
				offset = maxfiles > PAGESIZE ? selection - PAGESIZE + 1 : 0; // wrap around to bottom
			}

			if ( selection < offset )
			{
				offset -= PAGESIZE; // previous "page" of entries
				if ( offset < 0 )
					offset = 0;
			}

			redraw = 1;
		}

		if ( p & PSP_CTRL_CROSS )
		{
			if ( thefiles[selection].flags )	/*** This is directory ***/
			{
				char fname[FILENAME_MAX+FILENAME_MAX];

				strncpy(fname, thefiles[selection].path, FILENAME_MAX);
				fname[FILENAME_MAX-1] = 0;
				strncat(fname, thefiles[selection].filename, FILENAME_MAX);
				fname[FILENAME_MAX+FILENAME_MAX-2] = 0;
				strcat(fname, "/");
				offset = selection = 0;
				maxfiles = parse_dir(fname, thefiles, MAXFILES);
			}
			else
				return selection;

			redraw = 1;
		}

		if ( p & PSP_CTRL_TRIANGLE )
		{
			char fname[FILENAME_MAX];
			int pathpos = strlen(thefiles[1].path) - 2;

			while (pathpos > 5)
			{
				if (thefiles[1].path[pathpos] == '/') break;
				pathpos--;
			}
			if (pathpos < 5) pathpos = 5; /** handle root case */
			strncpy(fname, thefiles[1].path, pathpos+1);
			fname[pathpos+1] = 0;
			offset = selection = 0;
			maxfiles = parse_dir(fname, thefiles, MAXFILES);

			redraw = 1;
		}
	}

	return -1; // no file selected
}

/****************************************************************************
 * RequestFile
 *
 * return pointer to filename selected
 ****************************************************************************/

char *RequestFile (char *initialPath)
{
	int selection;
	static char fname[FILENAME_MAX+FILENAME_MAX];

    maxfiles = parse_dir(initialPath, thefiles, MAXFILES);
	if (!maxfiles)
		return 0;

	selection = FileSelector ();
	if (selection < 0)
		return 0;

	strncpy (fname, thefiles[selection].path, FILENAME_MAX);
	fname[FILENAME_MAX-1] = 0;
	strncat (fname, thefiles[selection].filename, FILENAME_MAX);
	fname[FILENAME_MAX+FILENAME_MAX-1] = 0;

	return fname;
}
