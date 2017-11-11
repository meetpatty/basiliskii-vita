#include "danzeff.h"

#include <malloc.h>
#include <vita2d.h>
#include <psp2/ctrl.h>

#define false 0
#define true 1

/*bool*/ int holding = false;     //user is holding a button
/*bool*/ int dirty = true;        //keyboard needs redrawing
/*bool*/ int shifted = false;     //user is holding shift
int mode = 0;             //charset selected. (0 - letters or 1 - numbers)
/*bool*/ int initialized = false; //keyboard is initialized

//Position on the 3-3 grid the user has selected (range 0-2)
int selected_x = 1;
int selected_y = 1;

float moved_x = 0, moved_y = 0; // location that we are moved to
float scale = 1.0;

//Variable describing where each of the images is
#define guiStringsSize 12 /* size of guistrings array */
#define PICS_BASEDIR "app0:graphics/"
char *guiStrings[] =
{
	PICS_BASEDIR "keys.png", PICS_BASEDIR "keys_t.png", PICS_BASEDIR "keys_s.png",
	PICS_BASEDIR "keys_c.png", PICS_BASEDIR "keys_c_t.png", PICS_BASEDIR "keys_s_c.png",
	PICS_BASEDIR "nums.png", PICS_BASEDIR "nums_t.png", PICS_BASEDIR "nums_s.png",
	PICS_BASEDIR "nums_c.png", PICS_BASEDIR "nums_c_t.png", PICS_BASEDIR "nums_s_c.png"
};

vita2d_texture* keyTextures[guiStringsSize];

//amount of modes (non shifted), each of these should have a corresponding shifted mode.
#define MODE_COUNT 2
//this is the layout of the keyboard
char modeChar[MODE_COUNT*2][3][3][5] =
{
	{	//standard letters
		{ ",abc",  ".def","!ghi" },
		{ "-jkl","\010m n", "?opq" },
		{ "(rst",  ":uvw",")xyz" }
	},

	{	//capital letters
		{ "^ABC",  "@DEF","*GHI" },
		{ "_JKL","\010M N", "\"OPQ" },
		{ "=RST",  ";UVW","/XYZ" }
	},

	{	//numbers and function keys etc.
		{ "\025\013\0121","\026\014\0022","\033\015\0273" },
		{ "\030\016\0314",  "\010\017 5","\032\020\0346" },
		{ "\035\021\0367","\037\022\0018", "\024\02309" }
	},

	{	//special characters
		{ "'(.)",  "\"<'>","-[_]" },
		{ "!{?}","\010\0 \0", "+\\=/" },
		{ ":@;#",  "~$`%","*^|&" }
	}
};

/*bool*/ int danzeff_isinitialized()
{
	return initialized;
}

/*bool*/ int danzeff_dirty()
{
	return dirty;
}

/** Attempts to read a character from the controller
* If no character is pressed then we return 0
* Other special values: 1 = move left, 2 = move right, 3 = select, 4 = start
* Every other value should be a standard ascii value.
* An unsigned int is returned so in the future we can support unicode input
*/
unsigned int danzeff_readInput(SceCtrlData pad)
{
	//Work out where the analog stick is selecting
	int x = 1;
	int y = 1;
	if (pad.lx < 85)     x -= 1;
	else if (pad.lx > 170) x += 1;

	if (pad.ly < 85)     y -= 1;
	else if (pad.ly > 170) y += 1;

	if (selected_x != x || selected_y != y) //If they've moved, update dirty
	{
		dirty = true;
		selected_x = x;
		selected_y = y;
	}
	//if they are changing shift then that makes it dirty too
	if ((!shifted && (pad.buttons & SCE_CTRL_RTRIGGER)) || (shifted && !(pad.buttons & SCE_CTRL_RTRIGGER)))
		dirty = true;

	unsigned int pressed = 0; //character they have entered, 0 as that means 'nothing'
	shifted = (pad.buttons & SCE_CTRL_RTRIGGER)?true:false;

	if (!holding)
	{
		if (pad.buttons& (SCE_CTRL_CROSS|SCE_CTRL_CIRCLE|SCE_CTRL_TRIANGLE|SCE_CTRL_SQUARE)) //pressing a char select button
		{
			int innerChoice = 0;
			if      (pad.buttons & SCE_CTRL_TRIANGLE)
				innerChoice = 0;
			else if (pad.buttons & SCE_CTRL_SQUARE)
				innerChoice = 1;
			else if (pad.buttons & SCE_CTRL_CROSS)
				innerChoice = 2;
			else //if (pad.buttons & SCE_CTRL_CIRCLE)
				innerChoice = 3;

			//Now grab the value out of the array
			pressed = modeChar[ mode*2 + shifted][y][x][innerChoice];
		}
		else if (pad.buttons& SCE_CTRL_LTRIGGER) //toggle mode
		{
			dirty = true;
			mode++;
			mode %= MODE_COUNT;
		}
		else if (pad.buttons& SCE_CTRL_DOWN)
		{
			pressed = '\n';
		}
		else if (pad.buttons& SCE_CTRL_UP)
		{
			pressed = 8; //backspace
		}
		else if (pad.buttons& SCE_CTRL_LEFT)
		{
			pressed = DANZEFF_LEFT; //LEFT
		}
		else if (pad.buttons& SCE_CTRL_RIGHT)
		{
			pressed = DANZEFF_RIGHT; //RIGHT
		}
		else if (pad.buttons& SCE_CTRL_SELECT)
		{
			pressed = DANZEFF_SELECT; //SELECT
		}
		else if (pad.buttons& SCE_CTRL_START)
		{
			pressed = DANZEFF_START; //START
		}
	}

	holding = pad.buttons & ~SCE_CTRL_RTRIGGER; //RTRIGGER doesn't set holding

	return pressed;
}

/* load all the guibits that make up the OSK */
void danzeff_load()
{
	if (initialized) return;

	int a;
	for (a = 0; a < guiStringsSize; a++)
	{
		SceUInt32 height, width;

		keyTextures[a] = vita2d_load_PNG_file(guiStrings[a]);
	}
	initialized = true;
}

/* remove all the guibits from memory */
void danzeff_free()
{
	if (!initialized) return;

	int a;
	for (a = 0; a < guiStringsSize; a++)
	{
		vita2d_free_texture(keyTextures[a]);
	}
	initialized = false;
}

/* move the position the keyboard is currently drawn at */
void danzeff_moveTo(float newX, float newY)
{
	moved_x = newX;
	moved_y = newY;
}

void danzeff_scale(float newScale)
{
	scale = newScale;
}

/* draw the keyboard at the current position */
void danzeff_render()
{
	dirty = false;

	///Draw the background for the selected keyboard either transparent or opaque
	///this is the whole background image, not including the special highlighted area
	//if center is selected then draw the whole thing opaque
	if (selected_x == 1 && selected_y == 1)
		vita2d_draw_texture_scale(keyTextures[6*mode + shifted*3], moved_x, moved_y, scale, scale);
	else
		vita2d_draw_texture_scale(keyTextures[6*mode + shifted*3 + 1], moved_x, moved_y, scale, scale);

	///Draw the current Highlighted Selector (orange bit)
	vita2d_draw_texture_part_scale(keyTextures[6*mode + shifted*3 + 2],
	//Offset from the current draw position to render at
	moved_x + selected_x*43*scale, moved_y + selected_y*43*scale,
	//internal offset of the image
	selected_x*64,selected_y*64,
	64, 64,
	scale, scale);
}
