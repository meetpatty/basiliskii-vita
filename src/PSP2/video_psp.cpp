/*
 *  video_psp.cpp - Video/graphics emulation, PSP specific stuff
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

#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <psp2/hid.h>
#include <vita2d.h>

#include <malloc.h>

#include "sysdeps.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <vector>

#include "danzeff/danzeff.h"
#include "cpu_emulation.h"
#include "main.h"
#include "adb.h"
#include "macos_util.h"
#include "prefs.h"
#include "user_strings.h"
#include "video.h"
#include "video_defs.h"

#define DEBUG 0
#include "debug.h"

#define KERNELMODE          0       /* 1 is untested but required for some keyboards to change baudrate */

// Supported video modes
using std::vector;
static vector<video_mode> VideoModes;


// Constants
const char KEYCODE_FILE_NAME[] = "BasiliskII_keycodes";
const char QUAL_MSG[5][12] = {
    "\0",
    " CMD ",
    " OPT ",
    " CTRL ",
    " CMD+OPT "
};

// Global variables
static unsigned int __attribute__((aligned(16))) clut256[256];
static unsigned int __attribute__((aligned(16))) clut0_15[256];
static unsigned int __attribute__((aligned(16))) clut1_15[256];
static unsigned int __attribute__((aligned(16))) clut2_15[256];
static unsigned int __attribute__((aligned(16))) clut0_24[256];
static unsigned int __attribute__((aligned(16))) clut1_24[256];
static unsigned int __attribute__((aligned(16))) clut2_24[256];
static unsigned int clut_15[256];

static uint8 __attribute__((aligned(64))) frame_buffer[768*576*4];
vita2d_texture *screen, *screen_16;
unsigned int *screen_data;
unsigned short *screen_16_data;

static int32 frame_skip;							// Prefs items

bool refresh_okay = false;

static uint32 BUF_WIDTH = 0;
static uint32 SCR_WIDTH = 0;
static uint32 SCR_HEIGHT = 0;
#define PIXEL_SIZE (4) // using 8888 mode for screen
static uint32 FRAME_SIZE = 0;
static uint32 DISP_BUF = 0;

static uint8 *the_buffer = NULL;					// Mac frame buffer (where MacOS draws into)
static uint32 the_buffer_size;						// Size of allocated the_buffer

static int32 psp_screen_x = 640;
static int32 psp_screen_y = 480;
static int32 psp_screen_d = VDEPTH_8BIT;

static int32 d_x, d_y, d_w, d_h;
double scale_x, scale_y;

static int psp_lcd_aspect = 0; // 4:3

extern bool emerg_quit;     						// Flag: emergency quit requested
extern int FONT_SIZE;
extern vita2d_font *font;

extern char *psp_floppy_inserted;                   // String: filename of floppy inserted
extern char *psp_cdrom_inserted;                    // String: filename of cdrom inserted
extern bool psp_cdrom_locked;                       // Flag: cdrom lock

static int DANZEFF_SCALE_COUNT = 2;
static int danzeff_cur_scale = 0;
static float danzeff_scales[] = { 1.0, 2.0 };

// File handles are pointers to these structures
struct file_handle {
	char *name;		// Copy of device/file name
	int fd;
	bool is_file;		// Flag: plain file or /dev/something?
	bool is_floppy;		// Flag: floppy device
	bool is_cdrom;		// Flag: CD-ROM device
	bool read_only;		// Copy of Sys_open() flag
	loff_t start_byte;	// Size of file header (if any)
	loff_t file_size;	// Size of file data (only valid if is_file is true)
	int toc_fd;         // filedesc for cdrom TOC file
};

extern file_handle *floppy_fh;
extern file_handle *cdrom_fh;


static bool psp_int_enable = false;
static bool use_keycodes = false;					// Flag: Use keycodes rather than keysyms
static int keycode_table[256];						// X keycode -> Mac keycode translation table

SceHidKeyboardReport k_reports[SCE_HID_MAX_REPORT];
SceHidMouseReport m_reports[SCE_HID_MAX_REPORT];
static int keyboard_hid_handle = 0;
static int mouse_hid_handle = 0;

/*
 * refresh subroutines
 */

static void refresh4(void)
{
    int i, j;

    for (j=0; j<psp_screen_y; j++)
    {
        for (i=0; i<psp_screen_x/2; i++)
        {
            screen_data[i*2+768*j] = clut256[the_buffer[i+768/2*j]>>4];
            screen_data[i*2+768*j+1] = clut256[the_buffer[i+768/2*j]&0xF];
        }
    }
    vita2d_draw_texture_scale(screen, d_x, d_y, scale_x, scale_y);
}

static void refresh8(void)
{
    int i, j;

    for (j=0; j<psp_screen_y; j++)
    {
        for (i=0; i<psp_screen_x; i++)
        {
            screen_data[i+768*j] = clut256[the_buffer[i+768*j]];
        }
    }
    vita2d_draw_texture_scale(screen, d_x, d_y, scale_x, scale_y);
}

static void refresh15(void)
{
    int i, j;
    for (j=0; j<psp_screen_y; j++)
    {
        for (i=0; i<psp_screen_x; i++)
        {
            screen_data[i+768*j] = clut0_15[the_buffer[i*2+768*j*2]] | clut1_15[the_buffer[i*2+1+768*j*2]];
        }
    }
    vita2d_draw_texture_scale(screen, d_x, d_y, scale_x, scale_y);
}

static void refresh24(void)
{
    int i, j;
    for (j=0; j<psp_screen_y; j++)
    {
        for (i=0; i<psp_screen_x; i++)
        {
            screen_data[i+768*j] = clut0_24[the_buffer[i*4+1+768*j*4]] | clut1_24[the_buffer[i*4+2+768*j*4]] | clut2_24[the_buffer[i*4+3+768*j*4]];
        }
    }
    vita2d_draw_texture_scale(screen, d_x, d_y, scale_x, scale_y);
}

/*
 *  monitor_desc subclass for PSP display
 */

class PSP_monitor_desc : public monitor_desc {
public:
	PSP_monitor_desc(const vector<video_mode> &available_modes, video_depth default_depth, uint32 default_id) : monitor_desc(available_modes, default_depth, default_id) {}
	~PSP_monitor_desc() {}

	virtual void switch_to_current_mode(void);
	virtual void set_palette(uint8 *pal, int num);
	virtual void set_interrupts_enable(bool enable);

	bool video_open(void);
	void video_close(void);
};


/*
 *  Utility functions
 */

// Return bytes per row for requested vdepth
static int PSPBytesPerRow(int width, int vdepth)
{
	switch (vdepth) {
	case VDEPTH_1BIT: return 768>>3;
	case VDEPTH_2BIT: return 768>>2;
	case VDEPTH_4BIT: return 768>>1;
	case VDEPTH_8BIT: return 768;
	case VDEPTH_16BIT: return 768<<1;
	case VDEPTH_32BIT: return 768<<2;
	}
	return 768;
}

// Add mode to list of supported modes
static void add_mode(int width, int height, int resolution_id, int bytes_per_row, video_depth vdepth)
{
	// Fill in VideoMode entry
	video_mode mode;
	mode.x = width;
	mode.y = height;
	mode.resolution_id = resolution_id;
	mode.bytes_per_row = bytes_per_row;
	mode.depth = vdepth;
	VideoModes.push_back(mode);
}

// Add standard list of windowed modes for given color depth
static void add_video_modes(video_depth vdepth)
{
	add_mode(512, 384, 0x80, PSPBytesPerRow(512, vdepth), vdepth);
	add_mode(640, 360, 0x81, PSPBytesPerRow(640, vdepth), vdepth);
	add_mode(640, 400, 0x82, PSPBytesPerRow(640, vdepth), vdepth);
	add_mode(640, 480, 0x83, PSPBytesPerRow(640, vdepth), vdepth);
	add_mode(726, 544, 0x84, PSPBytesPerRow(726, vdepth), vdepth);
	add_mode(768, 432, 0x85, PSPBytesPerRow(768, vdepth), vdepth);
	add_mode(768, 576, 0x86, PSPBytesPerRow(768, vdepth), vdepth);

}

// Set Mac frame layout and base address (uses the_buffer/MacFrameBaseMac)
static void set_mac_frame_buffer(PSP_monitor_desc &monitor, int depth, bool native_byte_order)
{
#if !REAL_ADDRESSING && !DIRECT_ADDRESSING
	int layout = FLAYOUT_DIRECT;
	if (depth == VDEPTH_16BIT)
		layout = FLAYOUT_HOST_565;
	else if (depth == VDEPTH_32BIT)
		layout = FLAYOUT_DIRECT;
	if (native_byte_order)
		MacFrameLayout = layout;
	else
		MacFrameLayout = FLAYOUT_DIRECT;
	monitor.set_mac_frame_base(MacFrameBaseMac);

	// Set variables used by UAE memory banking
	const video_mode &mode = monitor.get_current_mode();
	MacFrameBaseHost = the_buffer;
	MacFrameSize = mode.bytes_per_row * mode.y;
	InitFrameBufferMapping();
#else
	monitor.set_mac_frame_base(Host2MacAddr(the_buffer));
#endif
	D(bug("monitor.mac_frame_base = %08x\n", monitor.get_mac_frame_base()));
}

/*
 *  Display "driver" classes
 */

class driver_base {
public:
	driver_base(PSP_monitor_desc &m);
	virtual ~driver_base();

public:
	PSP_monitor_desc &monitor; // Associated video monitor
	const video_mode &mode;    // Video mode handled by the driver

	bool init_ok;	// Initialization succeeded (we can't use exceptions because of -fomit-frame-pointer)
};

class driver_fullscreen : public driver_base {
public:
	driver_fullscreen(PSP_monitor_desc &monitor);
	~driver_fullscreen();
};

static driver_base *drv = NULL;	// Pointer to currently used driver object

driver_base::driver_base(PSP_monitor_desc &m)
	: monitor(m), mode(m.get_current_mode()), init_ok(false)
{
	//the_buffer = NULL;
}

driver_base::~driver_base()
{
//	if (the_buffer != NULL) {
//		D(bug(" releasing the_buffer at %p (%d bytes)\n", the_buffer, the_buffer_size));
//		free(the_buffer);
//		the_buffer = NULL;
//	}
}


/*
 *  Full-screen display driver
 */

// Open display
driver_fullscreen::driver_fullscreen(PSP_monitor_desc &m)
	: driver_base(m)
{
	int width = mode.x, height = mode.y;

	// Set relative mouse mode
	ADBSetRelMouseMode(true);

	// Allocate memory for frame buffer
	the_buffer_size = 768*576*4; //height * PSPBytesPerRow(width, mode.depth);
	//the_buffer = (uint8 *)((uint32)frame_buffer | 0x40000000);    // ((uint32)memalign(64, the_buffer_size) | 0x40000000);
	the_buffer = (uint8 *)((uint32)memalign(64, the_buffer_size));
	//sceKernelDcacheWritebackInvalidateAll();
	D(bug("the_buffer = %p\n", the_buffer));

    psp_screen_x = width;
    psp_screen_y = height;
    psp_screen_d = mode.depth;
    if (psp_lcd_aspect == 2 ) {
        //square-pixels: fill the screen vertically
        //and adjust width by same scaling ratio
        d_w = psp_screen_x*d_h*1.0/psp_screen_y;
        d_x = (960-d_w)/2.0;
    }
    scale_x = d_w*1.0/psp_screen_x;
    scale_y = d_h*1.0/psp_screen_y;

	// Init blitting routines

	// Set frame buffer base
	set_mac_frame_buffer(monitor, mode.depth, true);

	// Everything went well
	init_ok = true;
}


// Close display
driver_fullscreen::~driver_fullscreen()
{
}


/*
 *  Initialization
 */

// Init keycode translation table
static void keycode_init(void)
{
	bool use_kc = PrefsFindBool("keycodes");
	if (use_kc) {

		// Get keycode file path from preferences
		const char *kc_path = PrefsFindString("keycodefile");

		// Open keycode table
		FILE *f = fopen(kc_path ? kc_path : KEYCODE_FILE_NAME, "r");
		if (f == NULL) {
			//char str[256];
			//sprintf(str, GetString(STR_KEYCODE_FILE_WARN), kc_path ? kc_path : KEYCODE_FILE_NAME, strerror(errno));
			//WarningAlert(str);
			return;
		}

		// Default translation table
		for (int i=0; i<256; i++)
			keycode_table[i] = -1;

		char line[256];
		int n_keys = 0;
		while (fgets(line, sizeof(line) - 1, f)) {
			// Read line
			int len = strlen(line);
			if (len == 0)
				continue;
			line[len-1] = 0;

			// Comments begin with "#" or ";"
			if (line[0] == '#' || line[0] == ';' || line[0] == 0)
				continue;

			// Read keycode
			int x_code, mac_code;
			if (sscanf(line, "%d %d", &x_code, &mac_code) == 2)
				keycode_table[x_code & 0xff] = mac_code, n_keys++;
			else
				break;
		}

		// Keycode file completely read
		fclose(f);
		use_keycodes = n_keys > 0;

		D(bug("Using keycodes table, %d key mappings\n", n_keys));
	}
}

void psp_video_setup(void)
{
    screen = vita2d_create_empty_texture_format(768, 576, SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR);
    // set filters to improve image quality in case the mac pixel to screen pixel mapping is not 1:1
    vita2d_texture_set_filters(screen, SCE_GXM_TEXTURE_FILTER_LINEAR, SCE_GXM_TEXTURE_FILTER_LINEAR);
    screen_data = (unsigned int*)vita2d_texture_get_datap(screen);

    vita2d_start_drawing();
    vita2d_clear_screen();
    vita2d_end_drawing();
    vita2d_swap_buffers();
    vita2d_start_drawing();
    vita2d_clear_screen();
    vita2d_end_drawing();
    vita2d_swap_buffers();

    refresh_okay = true;
}

// Open display for current mode
bool PSP_monitor_desc::video_open(void)
{
	D(bug("video_open()\n"));
#if DEBUG
	const video_mode &mode = get_current_mode();
	D(bug("Current video mode:\n"));
	D(bug(" %dx%d (ID %02x), %d bpp\n", mode.x, mode.y, mode.resolution_id, 1 << (mode.depth & 0x0f)));
#endif

    if (drv)
		delete drv;
	drv = new driver_fullscreen(*this);
	if (drv == NULL)
		return false;
	if (!drv->init_ok) {
		delete drv;
		drv = NULL;
		return false;
	}

	psp_video_setup();

	return true;
}


bool VideoInit(bool classic)
{
    if (classic)
        return false; // not supported by PSP

	sceHidKeyboardEnumerate(&keyboard_hid_handle, 1);
	sceHidMouseEnumerate(&mouse_hid_handle, 1);

	// Init keycode translation
	keycode_init();

	// Read prefs
	frame_skip = PrefsFindInt32("frameskip");
    psp_lcd_aspect = PrefsFindInt32("pspdar");
    // set PSP screen variables
    BUF_WIDTH = 768;
    SCR_WIDTH = 960;
    SCR_HEIGHT = 544;
    DISP_BUF = 0;

    switch (psp_lcd_aspect)
    {
    case 1:
        //fullscreen stretch
        d_w = 960;
        d_h = 544;
        d_x = 0;
        d_y = 0;
        break;
    case 2:
        //square pixels: d_w and d_x will be adjusted later
        d_w = 960;
        d_h = 544;
        d_x = 0;
        d_y = 0;
        break;
    default: 
        //4:3
        d_w = 726;
        d_h = 544;
        d_x = (960-726)/2;
        d_y = 0;
    }

	// Get screen mode from preferences
	const char *mode_str = NULL;
	mode_str = PrefsFindString("screen");

	// Determine display default dimensions
	int default_width, default_height, default_depth;
	default_width = default_height = default_depth = 0;
	if (mode_str)
		if (sscanf(mode_str, "%d/%d/%d", &default_width, &default_height, &default_depth) != 3)
			default_width = default_height = default_depth = 0;

	int default_mode;
	if ((default_width == 512) && (default_height == 384))
		default_mode = 0x80;
	else if ((default_width == 640) && (default_height == 360))
		default_mode = 0x81;
	else if ((default_width == 640) && (default_height == 400))
		default_mode = 0x82;
	else if ((default_width == 640) && (default_height == 480))
		default_mode = 0x83;
    else if ((default_width == 726) && (default_height == 544))
		default_mode = 0x84;
	else if ((default_width == 768) && (default_height == 432))
		default_mode = 0x85;
	else if ((default_width == 768) && (default_height == 576))
		default_mode = 0x86;
	else {
			default_width = 726;
			default_height = 544;
			default_depth = 8;
			default_mode = 0x84;
	}

	video_depth default_vdepth;
	switch (default_depth) {
	case 4:
		default_vdepth = VDEPTH_4BIT;
		break;
	case 8:
		default_vdepth = VDEPTH_8BIT;
		break;
	case 15: case 16:
		default_vdepth = VDEPTH_16BIT;
		break;
	case 24: case 32:
		default_vdepth = VDEPTH_32BIT;
		break;
	default:
		default_vdepth =  VDEPTH_8BIT;
		default_depth = 8;
		break;
	}

	// Construct list of supported modes
	add_video_modes(VDEPTH_4BIT);
	add_video_modes(VDEPTH_8BIT);
	add_video_modes(VDEPTH_16BIT);
	add_video_modes(VDEPTH_32BIT);

#if DEBUG
	D(bug("Available video modes:\n"));
	std::vector<video_mode>::const_iterator i, end = VideoModes.end();
	for (i = VideoModes.begin(); i != end; ++i) {
		const video_mode &mode = (*i);
		int bits = 1 << (mode.depth & 0x0f);
		if (bits == 16)
			bits = 15;
		else if (bits == 32)
			bits = 24;
		D(bug(" %dx%d (ID %02x), %d colors\n", mode.x, mode.y, mode.resolution_id, 1 << bits));
	}
#endif

	// Create PSP_monitor_desc for this (the only) display
	PSP_monitor_desc *monitor = new PSP_monitor_desc(VideoModes, default_vdepth, default_mode);
	VideoMonitors.push_back(monitor);



	// Open display
	return monitor->video_open();
}


/*
 *  Deinitialization
 */

// Close display
void PSP_monitor_desc::video_close(void)
{
	D(bug("video_close()\n"));

    refresh_okay = false;

	//sceGuTerm();

	// Close display
//	if (drv)
//	{
//        delete drv;
//        drv = NULL;
//	}
}


void VideoExit(void)
{
    // crashes PSP, so we'll just skip it for now ;)
#if 0
	// Close displays
	vector<monitor_desc *>::iterator i, end = VideoMonitors.end();
	for (i = VideoMonitors.begin(); i != end; ++i)
		dynamic_cast<PSP_monitor_desc *>(*i)->video_close();
#endif
}


/*
 *  Close down full-screen mode (if bringing up error alerts is unsafe while in full-screen mode)
 */

void VideoQuitFullScreen(void)
{
	D(bug("VideoQuitFullScreen()\n"));
}

// removeable media menu support

struct fileentries {
	char filename[FILENAME_MAX];
	char path[FILENAME_MAX];
	int flags;
};

#define MAXCDROMS 64
#define MAXFLOPPIES 64
#define MAXIMAPS 64

static struct fileentries cdroms[MAXCDROMS];
static struct fileentries floppies[MAXFLOPPIES];
static struct fileentries imaps[MAXIMAPS];

static int numcdroms = -1;
static int numfloppies = -1;
static int numimaps = -1;

static bool caps_lock = false;

extern int parse_dir (char *path, struct fileentries *thefiles, int maxentries);

#define NUM_MAPS 64

static uint32 button_map[NUM_MAPS][7] = {
    { 0x0010, 0x0000, 62, 255, 255, 255, 0 },
    { 0x0020, 0x0000, 60, 255, 255, 255, 0 },
    { 0x0040, 0x0000, 61, 255, 255, 255, 0 },
    { 0x0080, 0x0000, 59, 255, 255, 255, 0 },
    { 0x0100, 0x0000, 58, 255, 255, 255, 0 },
    { 0x0200, 0x0000, 54, 255, 255, 255, 0 },
    { 0x1000, 0x0000, 55, 12, 255, 255, 0 },
    { 0x2000, 0x0000, 55, 13, 255, 255, 0 },
    { 0x4000, 0x0000, 256, 255, 255, 255, 0 },
    { 0x8000, 0x0000, 36, 255, 255, 255, 0 },
    { 0, 0, 0, 0, 0, 0, 0 }
};

void parse_imap(FILE *fd)
{
    char temp[128];
    uint32 i, j, k, l, m, n, o;

    memset((void *)button_map, 0, NUM_MAPS*7*4);

    for (i=0; i<NUM_MAPS; )
    {
        temp[0] = 0;
		fgets(temp, 127, fd);
		if (temp[0] == 0)
            break;
        if (temp[0] == '#')
            continue; // skip comment line
        if (sscanf(temp, "%x %x %u %u %u %u", &j, &k, &l, &m, &n, &o) != 6)
            break;
        button_map[i][0] = j; // pressed
        button_map[i][1] = k; // not pressed
        button_map[i][2] = l; // key 1
        button_map[i][3] = m; // key 2
        button_map[i][4] = n; // key 3
        button_map[i][5] = o; // key 4
        button_map[i][6] = 0; // flag = not down
        i++;
    }
}

void key_down(int i)
{
    // look for mouse move first - it can be sent every call
    if (button_map[i][2] & 0x200)
    {
        ADBMouseMoved((int32)((button_map[i][2] & 0x0F0) >> 4) - 8, (int32)(button_map[i][2] & 0x00F) - 8);
        return;
    }

    if (button_map[i][6])
        return; // already sent key down

    button_map[i][6] = 1;

    if (button_map[i][2] == 57)
    {
        // caps lock
        caps_lock = caps_lock ? false : true;
        if (caps_lock)
            ADBKeyDown(57);
        else
            ADBKeyUp(57);
        return;
    }

    if (button_map[i][2] & 0x100)
        ADBMouseDown(button_map[i][2] & 7);
    else
        ADBKeyDown(button_map[i][2]);

    if (button_map[i][3] & 0x100)
        ADBMouseDown(button_map[i][3] & 7);
    else if (button_map[i][3] != 0xFF)
        ADBKeyDown(button_map[i][3]);

    if (button_map[i][4] & 0x100)
        ADBMouseDown(button_map[i][4] & 7);
    else if (button_map[i][4] != 0xFF)
        ADBKeyDown(button_map[i][4]);

    if (button_map[i][5] & 0x100)
        ADBMouseDown(button_map[i][5] & 7);
    else if (button_map[i][5] != 0xFF)
        ADBKeyDown(button_map[i][5]);
}

void key_up(int i)
{
    if (!button_map[i][6])
        return; // already sent key up

    button_map[i][6] = 0;

    if (button_map[i][2] == 57)
        return; // ignore caps lock key up

    if (button_map[i][5] & 0x100)
        ADBMouseUp(button_map[i][5] & 7);
    else if (button_map[i][5] != 0xFF)
        ADBKeyUp(button_map[i][5]);

    if (button_map[i][4] & 0x100)
        ADBMouseUp(button_map[i][4] & 7);
    else if (button_map[i][4] != 0xFF)
        ADBKeyUp(button_map[i][4]);

    if (button_map[i][3] & 0x100)
        ADBMouseUp(button_map[i][3] & 7);
    else if (button_map[i][3] != 0xFF)
        ADBKeyUp(button_map[i][3]);

    if (button_map[i][2] & 0x100)
        ADBMouseUp(button_map[i][2] & 7);
    else
        ADBKeyUp(button_map[i][2]);
}

void handle_keyboard(void)
{
	static uint8 mac_key[0xE8] = {
		255, 255, 255, 255, 0, 11, 8, 2,
		14, 3, 5, 4, 34, 38, 40, 37,
		46, 45, 31, 35, 12, 15, 1, 17,
		32, 9, 13, 7, 16, 6, 18, 19,
		20, 21, 23, 22, 26, 28, 25, 29,
		36, 53, 51, 48, 49, 27, 24, 33,
		30, 42, 255, 41, 39, 50, 43, 47,
		44, 57, 122, 120, 99, 118, 96, 97,
		98, 100, 101, 109, 103, 111, 105, 107,
		113, 114, 115, 116, 117, 119, 121, 60,
		59, 61, 62, 71, 75, 67, 78, 69,
		76, 83, 84, 85, 86, 87, 88, 90,
		91, 92, 82, 65, 255, 255, 255, 255,
		255, 255, 255, 255, 255, 255, 255, 255,
		255, 255, 255, 255, 255, 255, 255, 255,
		255, 255, 255, 255, 255, 255, 255, 255,
		255, 255, 255, 255, 255, 255, 255, 255,
		255, 255, 255, 255, 255, 255, 255, 255,
		255, 255, 255, 255, 255, 255, 255, 255,
		255, 255, 255, 255, 255, 255, 255, 255,
		255, 255, 255, 255, 255, 255, 255, 255,
		255, 255, 255, 255, 255, 255, 255, 255,
		255, 255, 255, 255, 255, 255, 255, 255,
		255, 255, 255, 255, 255, 255, 255, 255,
		255, 255, 255, 255, 255, 255, 255, 255,
		255, 255, 255, 255, 255, 255, 255, 255,
		255, 255, 255, 255, 255, 255, 255, 255,
		255, 255, 255, 255, 255, 255, 255, 255,
		255, 255, 255, 255, 255, 255, 255, 255
	};

	static uint8 prev_keys[6] = {0};
	static uint8 prev_modifiers = 0;

	if (keyboard_hid_handle > 0)
	{
		int numReports = sceHidKeyboardRead(keyboard_hid_handle, (SceHidKeyboardReport**)&k_reports, SCE_HID_MAX_REPORT);

		if (numReports < 0) {
			keyboard_hid_handle = 0;
		}
		else if (numReports) {

			if (k_reports[numReports-1].modifiers[1] & 0x2) //Caps Lock
			{
				if (!caps_lock) {
					caps_lock = true;
					ADBKeyDown(57);
				}
			}
			else
			{
				if (caps_lock) {
					caps_lock = false;
					ADBKeyUp(57);
				}
			}

			int l_ctrl_pressed = k_reports[numReports-1].modifiers[0] & 0x1 || prev_modifiers & 0x10;
			int l_ctrl_prev_pressed = prev_modifiers & 0x1 || prev_modifiers & 0x10;
			if (l_ctrl_pressed)
			{
				if (!l_ctrl_prev_pressed) {
					ADBKeyDown(54);
				}
			}
			else
			{
				if (l_ctrl_prev_pressed) {
					ADBKeyUp(54);
				}
			}

			int l_shift_pressed = k_reports[numReports-1].modifiers[0] & 0x2 || prev_modifiers & 0x20;
			int l_shift_prev_pressed = prev_modifiers & 0x2 || prev_modifiers & 0x20;
			if (l_shift_pressed)
			{
				if (!l_shift_prev_pressed) {
					ADBKeyDown(56);
				}
			}
			else
			{
				if (l_shift_prev_pressed) {
					ADBKeyUp(56);
				}
			}

			int l_alt_pressed = k_reports[numReports-1].modifiers[0] & 0x4 || prev_modifiers & 0x40;
			int l_alt_prev_pressed = prev_modifiers & 0x4 || prev_modifiers & 0x4;
			if (l_alt_pressed)
			{
				if (!l_alt_prev_pressed) {
					ADBKeyDown(55);
				}
			}
			else
			{
				if (l_alt_prev_pressed) {
					ADBKeyUp(55);
				}
			}

			int l_logo_pressed = k_reports[numReports-1].modifiers[0] & 0x8 || prev_modifiers & 0x80;
			int l_logo_prev_pressed = prev_modifiers & 0x8 || prev_modifiers & 0x80;
			if (l_logo_pressed)
			{
				if (!l_logo_prev_pressed) {
					ADBKeyDown(58);
				}
			}
			else
			{
				if (l_logo_prev_pressed) {
					ADBKeyUp(58);
				}
			}

			prev_modifiers = k_reports[numReports-1].modifiers[0];

			int i, j;

			for (i = 0; i < 6; i++) {

				int keyCode = k_reports[numReports-1].keycodes[i];

				if (keyCode != prev_keys[i]) {

					ADBKeyUp(mac_key[prev_keys[i]]);

					if (keyCode) {
						ADBKeyDown(mac_key[keyCode]);
					}

					prev_keys[i] = keyCode;
				}
			}
		}
	}
}

void handle_menu(SceCtrlData pad)
{
    uint32 buttons;
    static uint32 oldButtons = 0;
    static uint32 sel = 0;
    static uint32 max = 0;
    static uint32 idx = 0; // 0 = input maps, 1 = floppies, 2 = cdroms
    char temp[256];
    uint32 fc = 0xFF8888FF;

    if (numcdroms == -1)
    {
        strcpy(temp, HOME_DIR);
        strcat(temp, "cdroms");
        numcdroms = parse_dir(temp, cdroms, MAXCDROMS);
    }
    if (numfloppies == -1)
    {
        strcpy(temp, HOME_DIR);
        strcat(temp, "disks");
        numfloppies = parse_dir(temp, floppies, MAXFLOPPIES);
    }
    if (numimaps == -1)
    {
        strcpy(temp, HOME_DIR);
        strcat(temp, "imaps");
        numimaps = parse_dir(temp, imaps, MAXIMAPS);
    }

    // safety check
    if (numcdroms == 0 && numfloppies == 0 && numimaps == 0)
        return;

    // take care of initially clear max
    while (max == 0)
    {
        max = (idx == 0) ? numimaps : (idx == 1) ? numfloppies : numcdroms;
        if (max)
            break;
        idx = idx<2 ? idx+1 : 0;
    }

    buttons = pad.buttons ^ oldButtons; // set if button changed
    oldButtons = pad.buttons;

	if (buttons & (SCE_CTRL_UP | SCE_CTRL_DOWN))
        if (pad.buttons & SCE_CTRL_UP)
        {
            // dec index
            idx = idx>0 ? idx-1 : 2;
            sel = 0;
            max = 0;
            while (max == 0)
            {
                max = (idx == 0) ? numimaps : (idx == 1) ? numfloppies : numcdroms;
                if (max)
                    break;
                idx = idx>0 ? idx-1 : 2;
            }
        }
        else if (pad.buttons & SCE_CTRL_DOWN)
        {
            // inc index
            idx = idx<2 ? idx+1 : 0;
            sel = 0;
            max = 0;
            while (max == 0)
            {
                max = (idx == 0) ? numimaps : (idx == 1) ? numfloppies : numcdroms;
                if (max)
                    break;
                idx = idx<2 ? idx+1 : 0;
            }
        }

	if (buttons & SCE_CTRL_LEFT)
        if (pad.buttons & SCE_CTRL_LEFT)
            sel = sel>0 ? sel-1 : max-1;

	if (buttons & SCE_CTRL_RIGHT)
        if (pad.buttons & SCE_CTRL_RIGHT)
            sel = sel<max-1 ? sel+1 : 0;

    if (idx == 0)
    {
        // doing imaps
        strcpy(temp, "imap: ");
        strcat(temp, imaps[sel].filename);
        vita2d_font_draw_text(font, 14, 530, fc, FONT_SIZE, "Press X to load imap");
    }
    else if (idx == 1)
    {
        // doing floppies
        strcpy(temp, "floppy: ");
        strcat(temp, floppies[sel].filename);
        vita2d_font_draw_text(font, 14, 530, fc, FONT_SIZE, "Press X to mount floppy");
    }
    else if (idx == 2)
    {
        // doing cdroms
        strcpy(temp, "cdrom: ");
        strcat(temp, cdroms[sel].filename);
        vita2d_font_draw_text(font, 14, 530, fc, FONT_SIZE, "Press X to mount cdrom");
    }

    if (strlen(temp) > 0)
        vita2d_font_draw_text(font, 14, FONT_SIZE*1.5, fc, FONT_SIZE, temp);

   	if (buttons & SCE_CTRL_CROSS)
        if (pad.buttons & SCE_CTRL_CROSS)
        {
            if (idx == 0)
            {
                // load input map
                strcpy(temp, imaps[sel].path);
                strcat(temp, "/");
                strcat(temp, imaps[sel].filename);
                FILE *fd = fopen(temp, "r");
                if (fd != NULL)
                {
                    parse_imap(fd);
                    fclose(fd);
                }
            }
            else if (idx == 1)
            {
                // mount floppy
                if (psp_floppy_inserted == NULL)
                {
                    strcpy(temp, floppies[sel].path);
                    strcat(temp, "/");
                    strcat(temp, floppies[sel].filename);
                    if (floppy_fh->name)
                        free(floppy_fh->name);
                    floppy_fh->name = strdup(temp);
                    floppy_fh->fd = open(temp, floppy_fh->read_only ? O_RDONLY : O_RDWR);
                    if (floppy_fh->fd < 0 && !floppy_fh->read_only) {
                        // Read-write failed, try read-only
                        floppy_fh->read_only = true;
                        floppy_fh->fd = open(temp, O_RDONLY);
                    }
                    if (floppy_fh->fd >= 0)
                    {
                        floppy_fh->start_byte = 0;
                        // Detect disk image file layout
                        loff_t size = 0;
                        size = lseek(floppy_fh->fd, 0, SEEK_END);
                        uint8 data[256];
                        lseek(floppy_fh->fd, 0, SEEK_SET);
                        read(floppy_fh->fd, data, 256);
                        FileDiskLayout(size, data, floppy_fh->start_byte, floppy_fh->file_size);
                        psp_floppy_inserted = floppy_fh->name;
                        MountVolume(floppy_fh);
                    }
                    else
                    {
                        floppy_fh->fd = -1;
                    }
                }
            }
            else if (idx == 2)
            {
                // mount cdrom
                if (psp_cdrom_inserted == NULL)
                {
                    strcpy(temp, cdroms[sel].path);
                    strcat(temp, "/");
                    strcat(temp, cdroms[sel].filename);
                    if (cdrom_fh->name)
                        free(cdrom_fh->name);
                    cdrom_fh->name = strdup(temp);
                    cdrom_fh->fd = open(temp, O_RDONLY);
                    if (cdrom_fh->fd >= 0)
                    {
                        cdrom_fh->read_only = true;
                        cdrom_fh->start_byte = 0;
                        // Detect disk image file layout
                        loff_t size = 0;
                        size = lseek(cdrom_fh->fd, 0, SEEK_END);
                        uint8 data[256];
                        lseek(cdrom_fh->fd, 0, SEEK_SET);
                        read(cdrom_fh->fd, data, 256);
                        FileDiskLayout(size, data, cdrom_fh->start_byte, cdrom_fh->file_size);
                        psp_cdrom_inserted = cdrom_fh->name;
                        MountVolume(cdrom_fh);
                    }
                    else
                    {
                        cdrom_fh->fd = -1;
                    }
                }
            }
        }
}

/*
 *  Mac VBL interrupt
 */

void VideoInterrupt(void)
{
    SceCtrlData pad;
    SceTouchData touch;
	static bool mouseClickedTouch = false;
    static bool leftMouseClicked = false;
	static bool rightMouseClicked = false;
    static bool initialTouch = false;
	uint32 buttons;
	static uint32 oldButtons = 0;
	static uint32 qualifiers = 0;
	static bool input_mode = false;
	static bool show_menu = false;
    static bool show_on_right = true;
	static int frame_cnt = 0;
    uint32 fc = 0xFF8888FF;
    static int stick[16] = { -8, -8, -6, -4, -2, -1, -1, 0, 0, 0, 1, 1, 2, 4, 6, 8 };

	sceCtrlReadBufferPositive(0, &pad, 1);
	sceTouchPeek(0, &touch, 1);
    buttons = pad.buttons ^ oldButtons; // set if button changed
    oldButtons = pad.buttons;

    if (buttons & SCE_CTRL_START)
        if (pad.buttons & SCE_CTRL_START)
            input_mode = input_mode ? false : true;
    if (!danzeff_isinitialized())
        input_mode = false; // danzeff not loaded

	if (buttons & SCE_CTRL_SELECT)
        if (pad.buttons & SCE_CTRL_SELECT)
            show_menu = show_menu ? false : true; // toggle imap/floppy/cdrom menu
    if (input_mode)
        show_menu = false;
    else
    {
        qualifiers = 0; // clear qualifiers when exit OSK
    }

	// refresh video
	if (refresh_okay)
	{
        --frame_cnt;
        if (frame_cnt < 0)
        {
            vita2d_start_drawing();
            vita2d_clear_screen();

            frame_cnt = frame_skip - 1;
            if (psp_screen_d == VDEPTH_4BIT)
                refresh4();
            else if (psp_screen_d == VDEPTH_8BIT)
                refresh8();
            else if (psp_screen_d == VDEPTH_16BIT)
                refresh15();
            else if (psp_screen_d == VDEPTH_32BIT)
                refresh24();

            if (input_mode)
            {
                danzeff_moveTo(show_on_right ? d_x+d_w-(150*danzeff_scales[danzeff_cur_scale]) : d_x, d_y+d_h-(150*danzeff_scales[danzeff_cur_scale]));
                danzeff_render();
                if (qualifiers > 0 && qualifiers < 5)
                {
                    vita2d_font_draw_text(font, 14, 530, fc, FONT_SIZE, QUAL_MSG[qualifiers]);
                }

            }
            else if (show_menu)
                handle_menu(pad);

            vita2d_end_drawing();
            vita2d_swap_buffers();
        }

	// process inputs
    if (!input_mode && !show_menu)
    {
        if (touch.reportNum <= 0 && mouseClickedTouch)
        {
            mouseClickedTouch = false;
            initialTouch = true;
            ADBMouseUp(0);
        }
        else if (touch.reportNum > 0) {
            ADBSetRelMouseMode(false);

            int x = lerp(touch.report[0].x, 1920, 960);
            int y =lerp(touch.report[0].y, 1088, 544);

            //map to display
            int display_x = (x - ((960.0 - psp_screen_x*scale_x)/2)) / scale_x;
            int display_y = y / scale_y;

            ADBMouseMoved(display_x, display_y);

            if (!mouseClickedTouch && initialTouch)
            {
                initialTouch = false;
            }
            else if (!mouseClickedTouch)
            {
                mouseClickedTouch = true;
                ADBMouseDown(0);
            }
        }

        // mouse
        if (abs(pad.lx - 128) >= 32 || abs(pad.ly - 128) >= 32)
        {
            ADBSetRelMouseMode(true);
            ADBMouseMoved(stick[((pad.lx - 128) >> 4) + 8], stick[((pad.ly - 128) >> 4) + 8]);
        }

        for (int i=0; i<NUM_MAPS; i++)
        {
            if (button_map[i][0] == 0)
                break;
            if ((pad.buttons & button_map[i][0]) == button_map[i][0] && (~pad.buttons & button_map[i][1]) == button_map[i][1])
                key_down(i);
            else if (button_map[i][6])
                key_up(i);
        }

		//HID mouse
		int ret = sceHidMouseRead(mouse_hid_handle, (SceHidMouseReport**)&m_reports, 1);
		if (ret > 0)
        {
			ADBSetRelMouseMode(true);
			int i = 0;
			for (int i = 0; i < ret; i++)
			{
				ADBMouseMoved(m_reports[i].rel_x*2, m_reports[i].rel_y*2);
			}

			if (m_reports[i].buttons & 0x1) { //Left mouse button
				if (!leftMouseClicked) {
					leftMouseClicked = true;
					ADBMouseDown(0);
				}
			}
			else {
				if (leftMouseClicked) {
					leftMouseClicked = false;
					ADBMouseUp(0);
				}
			}

			if (m_reports[i].buttons & 0x2) { //Right mouse button
				if (!rightMouseClicked) {
					rightMouseClicked = true;
					ADBMouseDown(1);
				}
			}
			else {
				if (rightMouseClicked) {
					rightMouseClicked = false;
					ADBMouseUp(1);
				}
			}
        }

        if (keyboard_hid_handle) {
            handle_keyboard();
        }
    }
    else if (input_mode)
    {
        unsigned int key;
        uint8 xlate_danzeff_us[128] = {
            0, 0, 0, 0, 0, 0, 114, 117, // empty string, 1-4 are danzeff, insert, delete
            51, 0, 36, 122, 120, 99, 118, 96, // backspace, F1-F5 after 36 (CR)
            97, 98, 100, 101, 109, 103, 111, 48, // F6-F12, tab
            115, 119, 116, 53, 121, 105, 107, 113, // home, end, pgup, esc, pgdown, prtscr, scrlock, pause
            49, 128+18, 128+39, 128+20, 128+21, 128+23, 128+26, 39,
            128+25, 128+29, 128+28, 128+24, 43, 27, 47, 44,
            29, 18, 19, 20, 21, 23, 22, 26,
            28, 25, 128+41, 41, 128+43, 24, 128+47, 128+44,
            128+19, 128+0, 128+11, 128+8, 128+2, 128+14, 128+3, 128+5,
            128+4, 128+34, 128+38, 128+40, 128+37, 128+46, 128+45, 128+31,
            128+35, 128+12, 128+15, 128+1, 128+17, 128+32, 128+9, 128+13,
            128+7, 128+16, 128+6, 33, 42, 30, 128+22, 128+27,
            50, 0, 11, 8, 2, 14, 3, 5,
            4, 34, 38, 40, 37, 46, 45, 31,
            35, 12, 15, 1, 17, 32, 9, 13,
            7, 16, 6, 128+33, 128+42, 128+30, 128+50, 117
        };

        if (buttons & SCE_CTRL_SELECT)
            if (pad.buttons & SCE_CTRL_SELECT)
                qualifiers = (qualifiers + 1) % 5;

        // danzeff input
        key = danzeff_readInput(pad);
        switch (qualifiers)
        {
            case 1:
            ADBKeyDown(55);
            break;
            case 2:
            ADBKeyDown(58);
            break;
            case 3:
            ADBKeyDown(54);
            break;
            case 4:
            ADBKeyDown(55);
            ADBKeyDown(58);
        }
        switch (key)
        {
            case 0: // no input
            break;
            case 1: // move left
            if (!show_on_right) {
                danzeff_cur_scale = (danzeff_cur_scale+1) % DANZEFF_SCALE_COUNT;
                danzeff_scale(danzeff_scales[danzeff_cur_scale]);
            }
            else {
                show_on_right = false;
            }
            break;
            case 2: // move right
            if (show_on_right) {
                danzeff_cur_scale = (danzeff_cur_scale+1) % DANZEFF_SCALE_COUNT;
                danzeff_scale(danzeff_scales[danzeff_cur_scale]);
            }
            else {
                show_on_right = true;
            }
            break;
            case 3: // select
            break;
            case 4: // start
            break;
            case 8: // backspace
            ADBKeyDown(51);
            ADBKeyUp(51);
            break;
            case 10: // enter
            ADBKeyDown(36);
            ADBKeyUp(36);
            break;
            default: // ascii character
            if (xlate_danzeff_us[key] < 128)
            {
                ADBKeyDown(xlate_danzeff_us[key]);
                ADBKeyUp(xlate_danzeff_us[key]);
            }
            else
            {
                ADBKeyDown(56); // shift
                ADBKeyDown(xlate_danzeff_us[key]&127);
                ADBKeyUp(xlate_danzeff_us[key]&127);
                ADBKeyUp(56);
            }
        }
        switch (qualifiers)
        {
            case 1:
            ADBKeyUp(55);
            break;
            case 2:
            ADBKeyUp(58);
            break;
            case 3:
            ADBKeyUp(54);
            break;
            case 4:
            ADBKeyUp(55);
            ADBKeyUp(58);
        }
    }


	// Emergency quit requested? Then quit
	if (emerg_quit)
		QuitEmulator();

	}
}


/*
 *  Set interrupts enable
 */

void PSP_monitor_desc::set_interrupts_enable(bool enable)
{
    psp_int_enable = enable;
}


/*
 *  Set palette
 */

void PSP_monitor_desc::set_palette(uint8 *pal, int num_in)
{
    int i;
	// Convert colors to CLUT
	unsigned int *clut = (unsigned int *)(((unsigned int)clut256));
	for (i = 0; i < 256; ++i){
        clut[i] = (0xFF << 24)|(pal[i*3 + 2] << 16)|(pal[i*3 + 1] << 8)|(pal[i*3]);
	}


    if (psp_screen_d == VDEPTH_16BIT)
    {
        // convert xrrr rrgg
        unsigned int *dclut = (unsigned int *)(((unsigned int)clut0_15));
        for (i = 0; i < 256; ++i)
            dclut[i] = (0xFF << 24) | (clut[(i&3)<<3] & 0x0000FF00) | (clut[(i&0x7C)>>2] & 0x000000FF);

        // convert gggb bbbb
        dclut = (unsigned int *)(((unsigned int)clut1_15));
        for (i = 0; i < 256; ++i)
            dclut[i] = (0xFF << 24) | (clut[i&0x1F] & 0x00FF0000) | (clut[(i&0xE0)>>5] & 0x0000FF00);

    }
    if (psp_screen_d == VDEPTH_32BIT)
    {
        // convert red
        unsigned int *dclut = (unsigned int *)(((unsigned int)clut0_24));
        for (i = 0; i < 256; ++i)
            dclut[i] = (0xFF << 24) | (clut[i] & 0x000000FF);

        // convert green
        dclut = (unsigned int *)(((unsigned int)clut1_24));
        for (i = 0; i < 256; ++i)
            dclut[i] = (0xFF << 24) | (clut[i] & 0x0000FF00);

        // convert blue
        dclut = (unsigned int *)(((unsigned int)clut2_24));
        for (i = 0; i < 256; ++i)
            dclut[i] = (0xFF << 24) | (clut[i] & 0x00FF0000);
    }
}


/*
 *  Switch video mode
 */

void PSP_monitor_desc::switch_to_current_mode(void)
{
	// Close and reopen display
	video_close();
	video_open();

	if (drv == NULL) {
		ErrorAlert(STR_OPEN_WINDOW_ERR);
		QuitEmulator();
	}
}
