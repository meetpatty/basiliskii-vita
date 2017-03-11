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


#include <pspkernel.h>
#include <pspctrl.h>
#include <pspdebug.h>
#include <pspmoduleinfo.h>
#include <psputility.h>
#include <psputility_osk.h>
#include <psppower.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <pspgum.h>
#include <kubridge.h>
#include <malloc.h>

#include "intraFont.h"
#include "danzeff/danzeff.h"

#include "sysdeps.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <vector>

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


// Supported video modes
using std::vector;
static vector<video_mode> VideoModes;


// Constants
const char KEYCODE_FILE_NAME[] = "BasiliskII_keycodes";


// Global variables
static unsigned int __attribute__((aligned(16))) clut256[256];

static uint8 __attribute__((aligned(64))) frame_buffer[768*576*4];

static int32 frame_skip;							// Prefs items

static bool refresh_okay = false;

static uint8 *the_buffer = NULL;					// Mac frame buffer (where MacOS draws into)
static uint32 the_buffer_size;						// Size of allocated the_buffer

static uint32 psp_screen_x = 640;
static uint32 psp_screen_y = 480;
static uint32 psp_screen_d = VDEPTH_8BIT;


extern bool emerg_quit;     						// Flag: emergency quit requested


extern char *psp_floppy_inserted;                   // String: filename of floppy inserted
extern char *psp_cdrom_inserted;                    // String: filename of cdrom inserted
extern bool psp_cdrom_locked;                       // Flag: cdrom lock

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


/*
 * refresh subroutines
 */

#define PSP_SLICE_SIZE 32

static unsigned int list[16384] __attribute__((aligned(16)));

struct texVertex
{
	unsigned short u, v;
	short x, y, z;
};

static inline int roundUpToPowerOfTwo (int x)
{
	return 1 << (32 - __builtin_allegrex_clz(x - 1));
}

// based on PSP_GuStretchBlit() in SDL
static void refresh4(void *src, int sp, int sx, int sy, int sw, int sh, int dx, int dy, int dw, int dh)
{
	unsigned short old_slice = 0; /* set when we load 2nd tex */
	unsigned int slice, num_slices, width, height, tbw, off_x, off_bytes;
	struct texVertex *vertices;
	int pixels;

	off_bytes = ((int)src + sx / 2) & 0xf;
	off_x = off_bytes * 2;
	width = roundUpToPowerOfTwo(sw + off_x);
	height = roundUpToPowerOfTwo(sh);
	tbw = sp * 2;

    // four bit video hack
    uint32 *srp = (uint32 *)src;
    uint32 *dp = (uint32 *)((int)src + 768*576/2);
    int i,j;
    for (j=0; j<576; j++)
        for (i=0; i<768/8; i++)
        {
            uint32 v = srp[j*768/8 + i];
            uint32 vv1 = (v>>4)&0x0F0F0F0F | (v<<4)&0xF0F0F0F0;
            dp[j*768/8 + i] = vv1;
        }
    src = (void *)((int)src + 768*576/2);

	/* Align the texture prior to sx */
	pixels = (int)src + (sx - off_x) / 2 + sp * sy;
	num_slices = (sw + (PSP_SLICE_SIZE - 1)) / PSP_SLICE_SIZE;

	/* GE doesn't appreciate textures wider than 512 */
	if (width > 512)
		width = 512;

	sceGuStart(GU_DIRECT,list);
	sceGuClutMode(GU_PSM_8888,0,0x0F,0);	        // 32-bit palette, shift = 0, mask = 0x0F, start = 0
	sceGuClutLoad((16/(32/4)),clut256);			    // upload 2 blocks (each block is 32 bytes / bytesPerColorEntry)
	sceGuEnable(GU_TEXTURE_2D);
	sceGuTexMode(GU_PSM_T4,0,0,0);
	sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGB);
	sceGuTexFilter(GU_LINEAR, GU_LINEAR);
	sceGuTexImage(0, width, height, tbw, (void *)pixels);
	sceGuTexSync();

	for (slice = 0; slice < num_slices; slice++)
	{
		vertices = (struct texVertex*)sceGuGetMemory(2 * sizeof(struct texVertex));

		if ((slice * PSP_SLICE_SIZE) < width)
		{
			vertices[0].u = slice * PSP_SLICE_SIZE + off_x;
		}
		else
		{
			if (!old_slice)
			{
				/* load another texture (src width > 512) */
				pixels += width / 2;
				sceGuTexImage(0, roundUpToPowerOfTwo(sw - width), height, tbw, (void *)pixels);
				sceGuTexSync();
				old_slice = slice;
			}
			vertices[0].u = (slice - old_slice) * PSP_SLICE_SIZE + off_x;
		}
		vertices[1].u = vertices[0].u + PSP_SLICE_SIZE;
		if (vertices[1].u > (off_x + sw))
			vertices[1].u = off_x + sw;

		vertices[0].v = 0;
		vertices[1].v = vertices[0].v + sh;

		vertices[0].x = dx + slice * PSP_SLICE_SIZE * dw / sw;
		vertices[1].x = vertices[0].x + PSP_SLICE_SIZE * dw / sw;

		if (vertices[1].x > (dx + dw))
			vertices[1].x = dx + dw;

		vertices[0].y = dy;
		vertices[1].y = vertices[0].y + dh;

		vertices[0].z = 0;
		vertices[1].z = 0;

		sceGuDrawArray(GU_SPRITES,GU_TEXTURE_16BIT|GU_VERTEX_16BIT|GU_TRANSFORM_2D, 2,0,vertices);
	}

//	sceGuFinish();
//	sceGuSync(0,0);
//	sceDisplayWaitVblankStart();
//	sceGuSwapBuffers();
}

// based on PSP_GuStretchBlit() in SDL
static void refresh8(void *src, int sp, int sx, int sy, int sw, int sh, int dx, int dy, int dw, int dh)
{
	unsigned short old_slice = 0; /* set when we load 2nd tex */
	unsigned int slice, num_slices, width, height, tbw, off_x, off_bytes;
	struct texVertex *vertices;
	int pixels;

	off_bytes = ((int)src + sx) & 0xf;
	off_x = off_bytes;
	width = roundUpToPowerOfTwo(sw + off_x);
	height = roundUpToPowerOfTwo(sh);
	tbw = sp;

	/* Align the texture prior to sx */
	pixels = (int)src + (sx - off_x) + sp * sy;
	num_slices = (sw + (PSP_SLICE_SIZE - 1)) / PSP_SLICE_SIZE;

	/* GE doesn't appreciate textures wider than 512 */
	if (width > 512)
		width = 512;

	sceGuStart(GU_DIRECT,list);
	sceGuClutMode(GU_PSM_8888,0,0xFF,0);        // 32-bit palette, shift = 0, mask = 0xFF, start = 0
	sceGuClutLoad((256/(32/4)),clut256);		// upload 32 blocks (each block is 32 bytes / bytesPerColorEntry)
	sceGuEnable(GU_TEXTURE_2D);
	sceGuTexMode(GU_PSM_T8,0,0,0);
	sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGB);
	sceGuTexFilter(GU_LINEAR, GU_LINEAR);
	sceGuTexImage(0, width, height, tbw, (void *)pixels);
	sceGuTexSync();

	for (slice = 0; slice < num_slices; slice++)
	{
		vertices = (struct texVertex*)sceGuGetMemory(2 * sizeof(struct texVertex));

		if ((slice * PSP_SLICE_SIZE) < width)
		{
			vertices[0].u = slice * PSP_SLICE_SIZE + off_x;
		}
		else
		{
			if (!old_slice)
			{
				/* load another texture (src width > 512) */
				pixels += width;
				sceGuTexImage(0, roundUpToPowerOfTwo(sw - width), height, tbw, (void *)pixels);
				sceGuTexSync();
				old_slice = slice;
			}
			vertices[0].u = (slice - old_slice) * PSP_SLICE_SIZE + off_x;
		}
		vertices[1].u = vertices[0].u + PSP_SLICE_SIZE;
		if (vertices[1].u > (off_x + sw))
			vertices[1].u = off_x + sw;

		vertices[0].v = 0;
		vertices[1].v = vertices[0].v + sh;

		vertices[0].x = dx + slice * PSP_SLICE_SIZE * dw / sw;
		vertices[1].x = vertices[0].x + PSP_SLICE_SIZE * dw / sw + 1;

		if (vertices[1].x > (dx + dw))
			vertices[1].x = dx + dw;

		vertices[0].y = dy;
		vertices[1].y = vertices[0].y + dh;

		vertices[0].z = 0;
		vertices[1].z = 0;

		sceGuDrawArray(GU_SPRITES,GU_TEXTURE_16BIT|GU_VERTEX_16BIT|GU_TRANSFORM_2D, 2,0,vertices);
	}

//	sceGuFinish();
//	sceGuSync(0,0);
//	sceDisplayWaitVblankStart();
//	sceGuSwapBuffers();
}

// based on PSP_GuStretchBlit() in SDL
static void refresh15(void *src, int sp, int sx, int sy, int sw, int sh, int dx, int dy, int dw, int dh)
{
	unsigned short old_slice = 0; /* set when we load 2nd tex */
	unsigned int slice, num_slices, width, height, tbw, off_x, off_bytes;
	struct texVertex *vertices;
	int pixels;

	off_bytes = ((int)src + sx * 2) & 0xf;
	off_x = off_bytes / 2;
	width = roundUpToPowerOfTwo(sw + off_x);
	height = roundUpToPowerOfTwo(sh);
	tbw = sp / 2;

	/* Align the texture prior to sx */
	pixels = (int)src + (sx - off_x) * 2 + sp * sy;
	num_slices = (sw + (PSP_SLICE_SIZE - 1)) / PSP_SLICE_SIZE;

	/* GE doesn't appreciate textures wider than 512 */
	if (width > 512)
		width = 512;

	sceGuStart(GU_DIRECT,list);
	sceGuEnable(GU_TEXTURE_2D);
	sceGuTexMode(GU_PSM_5551,0,0,0);
	sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGB);
	sceGuTexFilter(GU_LINEAR, GU_LINEAR);
	sceGuTexImage(0, width, height, tbw, (void *)pixels);
	sceGuTexSync();

	for (slice = 0; slice < num_slices; slice++)
	{
		vertices = (struct texVertex*)sceGuGetMemory(2 * sizeof(struct texVertex));

		if ((slice * PSP_SLICE_SIZE) < width)
		{
			vertices[0].u = slice * PSP_SLICE_SIZE + off_x;
		}
		else
		{
			if (!old_slice)
			{
				/* load another texture (src width > 512) */
				pixels += width * 2;
				sceGuTexImage(0, roundUpToPowerOfTwo(sw - width), height, tbw, (void *)pixels);
				sceGuTexSync();
				old_slice = slice;
			}
			vertices[0].u = (slice - old_slice) * PSP_SLICE_SIZE + off_x;
		}
		vertices[1].u = vertices[0].u + PSP_SLICE_SIZE;
		if (vertices[1].u > (off_x + sw))
			vertices[1].u = off_x + sw;

		vertices[0].v = 0;
		vertices[1].v = vertices[0].v + sh;

		vertices[0].x = dx + slice * PSP_SLICE_SIZE * dw / sw;
		vertices[1].x = vertices[0].x + PSP_SLICE_SIZE * dw / sw + 1;

		if (vertices[1].x > (dx + dw))
			vertices[1].x = dx + dw;

		vertices[0].y = dy;
		vertices[1].y = vertices[0].y + dh;

		vertices[0].z = 0;
		vertices[1].z = 0;

		sceGuDrawArray(GU_SPRITES,GU_TEXTURE_16BIT|GU_VERTEX_16BIT|GU_TRANSFORM_2D, 2,0,vertices);
	}

//	sceGuFinish();
//	sceGuSync(0,0);
//	sceDisplayWaitVblankStart();
//	sceGuSwapBuffers();
}

// based on PSP_GuStretchBlit() in SDL
static void refresh24(void *src, int sp, int sx, int sy, int sw, int sh, int dx, int dy, int dw, int dh)
{
	unsigned short old_slice = 0; /* set when we load 2nd tex */
	unsigned int slice, num_slices, width, height, tbw, off_x, off_bytes;
	struct texVertex *vertices;
	int pixels;

	off_bytes = ((int)src + sx * 4) & 0xf;
	off_x = off_bytes / 4;
	width = roundUpToPowerOfTwo(sw + off_x);
	height = roundUpToPowerOfTwo(sh);
	tbw = sp / 4;

	/* Align the texture prior to sx */
	pixels = (int)src + (sx - off_x) * 4 + sp * sy;
	num_slices = (sw + (PSP_SLICE_SIZE - 1)) / PSP_SLICE_SIZE;

	/* GE doesn't appreciate textures wider than 512 */
	if (width > 512)
		width = 512;

	sceGuStart(GU_DIRECT,list);
	sceGuEnable(GU_TEXTURE_2D);
	sceGuTexMode(GU_PSM_8888,0,0,0);
	sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGB);
	sceGuTexFilter(GU_LINEAR, GU_LINEAR);
	sceGuTexImage(0, width, height, tbw, (void *)pixels);
	sceGuTexSync();

	for (slice = 0; slice < num_slices; slice++)
	{
		vertices = (struct texVertex*)sceGuGetMemory(2 * sizeof(struct texVertex));

		if ((slice * PSP_SLICE_SIZE) < width)
		{
			vertices[0].u = slice * PSP_SLICE_SIZE + off_x;
		}
		else
		{
			if (!old_slice)
			{
				/* load another texture (src width > 512) */
				pixels += width * 4;
				sceGuTexImage(0, roundUpToPowerOfTwo(sw - width), height, tbw, (void *)pixels);
				sceGuTexSync();
				old_slice = slice;
			}
			vertices[0].u = (slice - old_slice) * PSP_SLICE_SIZE + off_x;
		}
		vertices[1].u = vertices[0].u + PSP_SLICE_SIZE;
		if (vertices[1].u > (off_x + sw))
			vertices[1].u = off_x + sw;

		vertices[0].v = 0;
		vertices[1].v = vertices[0].v + sh;

		vertices[0].x = dx + slice * PSP_SLICE_SIZE * dw / sw;
		vertices[1].x = vertices[0].x + PSP_SLICE_SIZE * dw / sw + 1;

		if (vertices[1].x > (dx + dw))
			vertices[1].x = dx + dw;

		vertices[0].y = dy;
		vertices[1].y = vertices[0].y + dh;

		vertices[0].z = 0;
		vertices[1].z = 0;

		sceGuDrawArray(GU_SPRITES,GU_TEXTURE_16BIT|GU_VERTEX_16BIT|GU_TRANSFORM_2D, 2,0,vertices);
	}

//	sceGuFinish();
//	sceGuSync(0,0);
//	sceDisplayWaitVblankStart();
//	sceGuSwapBuffers();
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
static void add_mode(int width, int height, int resolution_id, int bytes_per_row, int depth)
{
	// Fill in VideoMode entry
	video_mode mode;
	mode.x = width;
	mode.y = height;
	mode.resolution_id = resolution_id;
	mode.bytes_per_row = bytes_per_row;
	mode.depth = (video_depth)depth;
	VideoModes.push_back(mode);
}

// Add standard list of windowed modes for given color depth
static void add_video_modes(int depth)
{
	video_depth vdepth = (video_depth)depth;
	add_mode(512, 384, 0x80, PSPBytesPerRow(512, vdepth), depth);
	add_mode(640, 480, 0x81, PSPBytesPerRow(640, vdepth), depth);
	add_mode(768, 576, 0x82, PSPBytesPerRow(768, vdepth), depth);
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
	the_buffer = (uint8 *)((uint32)frame_buffer | 0x40000000);    // ((uint32)memalign(64, the_buffer_size) | 0x40000000);
	sceKernelDcacheWritebackInvalidateAll();
	D(bug("the_buffer = %p\n", the_buffer));

    psp_screen_x = width;
    psp_screen_y = height;
    psp_screen_d = mode.depth;

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


#define BUF_WIDTH (512)
#define SCR_WIDTH (480)
#define SCR_HEIGHT (272)
#define PIXEL_SIZE (4) /* change this if you change to another screenmode */
#define FRAME_SIZE (BUF_WIDTH * SCR_HEIGHT * PIXEL_SIZE)

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

	// setup GU
	sceGuInit();
	sceGuStart(GU_DIRECT,list);
	sceGuDrawBuffer(GU_PSM_8888,(void*)0,BUF_WIDTH);
	sceGuDispBuffer(SCR_WIDTH,SCR_HEIGHT,(void*)FRAME_SIZE,BUF_WIDTH);
	sceGuDepthBuffer((void*)(FRAME_SIZE*2),BUF_WIDTH);
	sceGuOffset(2048 - (SCR_WIDTH/2),2048 - (SCR_HEIGHT/2));
	sceGuViewport(2048,2048,SCR_WIDTH,SCR_HEIGHT);
	sceGuDepthRange(65535,0);
	sceGuDepthMask(GU_TRUE);
	sceGuDisable(GU_DEPTH_TEST);
	sceGuScissor(0,0,SCR_WIDTH,SCR_HEIGHT);
	sceGuEnable(GU_SCISSOR_TEST);
	sceGuFrontFace(GU_CW);
	sceGuEnable(GU_TEXTURE_2D);
	sceGuClear(GU_COLOR_BUFFER_BIT);
	sceGuFinish();
	sceGuSync(0,0);
	sceDisplayWaitVblankStart();
	sceGuDisplay(GU_TRUE);

    refresh_okay = true;

	return true;
}


bool VideoInit(bool classic)
{
    if (classic)
        return false; // not supported by PSP

	// Init keycode translation
	keycode_init();

	// Read prefs
	frame_skip = PrefsFindInt32("frameskip");

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
	else if ((default_width == 640) && (default_height == 480))
		default_mode = 0x81;
	else if ((default_width == 768) && (default_height == 576))
		default_mode = 0x82;
	else {
			default_width = 640;
			default_height = 480;
			default_depth = 8;
			default_mode = 0x81;
	}

	int default_vdepth;
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
	PSP_monitor_desc *monitor = new PSP_monitor_desc(VideoModes, (video_depth)default_vdepth, default_mode);
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
#ifndef PSP
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

static struct fileentries cdroms[MAXCDROMS];
static struct fileentries floppies[MAXFLOPPIES];

static int numcdroms = -1;
static int numfloppies = -1;

extern char psp_home[];

extern int parse_dir (char *path, struct fileentries *thefiles, int maxentries);

void handle_menu(SceCtrlData pad, char *message)
{
    uint32 buttons;
    static uint32 oldButtons = 0;
    static uint32 sel = 0;
    static uint32 max = 0;
    static bool cds = false;
    char temp[256];

    if (numcdroms == -1)
    {
        strcpy(temp, psp_home);
        strcat(temp, "cdroms");
        numcdroms = parse_dir(temp, cdroms, MAXCDROMS);
    }

    if (numfloppies == -1)
    {
        strcpy(temp, psp_home);
        strcat(temp, "disks");
        numfloppies = parse_dir(temp, floppies, MAXFLOPPIES);
    }

    // safety check
    if (numcdroms == 0 && numfloppies == 0)
        return;

    buttons = pad.Buttons ^ oldButtons; // set if button changed
    oldButtons = pad.Buttons;

	if (buttons & (PSP_CTRL_LEFT | PSP_CTRL_RIGHT))
        if (pad.Buttons & (PSP_CTRL_LEFT | PSP_CTRL_RIGHT))
        {
            // swap between cdroms and floppies
            cds = cds ? false : true;
            sel = 0;
        }

    max = cds ? numcdroms : numfloppies;
    // safety check
    if (max == 0)
    {
        // swap back because if we're here, we KNOW the other dir has entries
        cds = cds ? false : true;
        sel = 0;
        max = cds ? numcdroms : numfloppies;
    }

	if (buttons & PSP_CTRL_UP)
        if (pad.Buttons & PSP_CTRL_UP)
            sel = sel>0 ? sel-1 : max-1;

	if (buttons & PSP_CTRL_DOWN)
        if (pad.Buttons & PSP_CTRL_DOWN)
            sel = sel<max-1 ? sel+1 : 0;

    if (cds)
    {
        // doing cdroms
        strcpy(temp, " ");
        strcat(temp, cdroms[sel].filename);
        strcat(temp, " ");
    }
    else
    {
        // doing floppies
        strcpy(temp, " ");
        strcat(temp, floppies[sel].filename);
        strcat(temp, " ");
    }

    strncpy(message, temp, 67);


   	if (buttons & PSP_CTRL_CROSS)
        if (pad.Buttons & PSP_CTRL_CROSS)
        {
            // mount disk/cdrom
            if (cds)
            {
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
                        psp_cdrom_inserted = cdrom_fh->name;
                        MountVolume(cdrom_fh);
                    }
                    else
                    {
                        cdrom_fh->fd = -1;
                    }
                }
            }
            else
            {
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
                        psp_floppy_inserted = floppy_fh->name;
                        MountVolume(floppy_fh);
                    }
                    else
                    {
                        floppy_fh->fd = -1;
                    }
                }
            }
        }
}

void show_msg(char *message, uint32 fc, uint32 bc)
{
	struct texVertex *vertices;

    pspDebugScreenSetOffset(FRAME_SIZE*2); // change for vram buffers in new code
	pspDebugScreenSetBackColor(bc);
	pspDebugScreenSetTextColor(fc);
	pspDebugScreenSetXY(0, 0);
	pspDebugScreenPrintf("%s", message);

	sceGuEnable(GU_TEXTURE_2D);
	sceGuTexMode(GU_PSM_8888,0,0,0);
	sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGB);
	sceGuTexFilter(GU_LINEAR, GU_LINEAR);
	sceGuTexImage(0, 512, 8, 512, (void *)(0x40000000 | (u32) sceGeEdramGetAddr() + FRAME_SIZE*2));
	sceGuTexSync();
	vertices = (struct texVertex*)sceGuGetMemory(2 * sizeof(struct texVertex));
	vertices[0].u = 0;
	vertices[1].u = strlen(message)*7;
	vertices[0].v = 0;
	vertices[1].v = 8;
	vertices[0].x = (480-strlen(message)*7)/2; // change for screenwidth in new code
	vertices[1].x = vertices[0].x + strlen(message)*7;
	vertices[0].y = 28*8; // change for screenheight in new code
	vertices[1].y = vertices[0].y + 8;
    vertices[0].z = 0;
	vertices[1].z = 0;
	sceGuDrawArray(GU_SPRITES,GU_TEXTURE_16BIT|GU_VERTEX_16BIT|GU_TRANSFORM_2D, 2,0,vertices);
}

/*
 *  Mac VBL interrupt
 */

void VideoInterrupt(void)
{
	SceCtrlData pad;
	uint32 buttons;
	static uint32 oldButtons = 0;
	static uint32 qualifiers = 0;
	static bool input_mode = false;
	static bool show_menu = false;
    static bool show_on_right = true;
	static int frame_cnt = 0;
	static char msgtxt[68] = { '\0' };
    uint32 fc = 0xFF000000;
    uint32 bc = 0xFF8888FF;

	sceCtrlReadBufferPositive(&pad, 1);
    buttons = pad.Buttons ^ oldButtons; // set if button changed
    oldButtons = pad.Buttons;

	if (buttons & PSP_CTRL_START)
        if (pad.Buttons & PSP_CTRL_START)
            input_mode = input_mode ? false : true; // toggle input mode
    if (!danzeff_isinitialized())
        input_mode = false; // danzeff not loaded

	if (buttons & PSP_CTRL_SELECT)
        if (pad.Buttons & PSP_CTRL_SELECT)
            show_menu = show_menu ? false : true; // toggle floppy/cdrom menu
    if (input_mode)
        show_menu = false; // select used for qualifiers in OSK
    else
    {
        qualifiers = 0; // clear qualifiers when exit OSK
        msgtxt[0] = 0; // clear onscreen message
    }

	// refresh video
	if (refresh_okay)
	{
        --frame_cnt;
        if (frame_cnt < 0)
        {
            frame_cnt = frame_skip - 1;
            if (psp_screen_d == VDEPTH_4BIT)
                refresh4(the_buffer, 768/2, 0, 0, psp_screen_x, psp_screen_y, 0, 0, 480, 272);
            else if (psp_screen_d == VDEPTH_8BIT)
                refresh8(the_buffer, 768, 0, 0, psp_screen_x, psp_screen_y, 0, 0, 480, 272);
            else if (psp_screen_d == VDEPTH_16BIT)
                refresh15(the_buffer, 768*2, 0, 0, psp_screen_x, psp_screen_y, 0, 0, 480, 272);
            else if (psp_screen_d == VDEPTH_32BIT)
                refresh24(the_buffer, 768*4, 0, 0, psp_screen_x, psp_screen_y, 0, 0, 480, 272);

            if (input_mode)
            {
                danzeff_moveTo(show_on_right ? 480-150 : 0, 272-150);
                danzeff_render();
            }
            else if (show_menu)
                handle_menu(pad, msgtxt);

            if (msgtxt[0] != 0)
                show_msg(msgtxt, fc, bc);

            sceGuFinish();
            sceGuSync(0,0);

            //sceDisplayWaitVblankStart();
            sceGuSwapBuffers();
        }

	// process inputs
    if (!input_mode && !show_menu)
    {
        // mouse
        if (abs(pad.Lx - 128) >= 32 || abs(pad.Ly - 128) >= 32)
        {
            int dx = (pad.Lx - 128) >> 5;
            int dy = (pad.Ly - 128) >> 5;
            if (dx > 1)
                dx--;
            else if (dx < -1)
                dx++;
            if (dy > 1)
                dy--;
            else if (dy < -1)
                dy++;
            ADBMouseMoved(dx, dy);
        }
        if (buttons & PSP_CTRL_LTRIGGER)
        {
            if (pad.Buttons & PSP_CTRL_LTRIGGER)
                ADBMouseDown(0);
            else
                ADBMouseUp(0);
        }
        if (buttons & PSP_CTRL_RTRIGGER)
        {
            if (pad.Buttons & PSP_CTRL_RTRIGGER)
                ADBMouseDown(1);
            else
                ADBMouseUp(1);
        }

        // keyboard
        if (buttons & PSP_CTRL_UP)
        {
            if (pad.Buttons & PSP_CTRL_UP)
                ADBKeyDown(0x3e);
            else
                ADBKeyUp(0x3e);
        }
        else if (buttons & PSP_CTRL_DOWN)
        {
            if (pad.Buttons & PSP_CTRL_DOWN)
                ADBKeyDown(0x3d);
            else
                ADBKeyUp(0x3d);
        }
        if (buttons & PSP_CTRL_RIGHT)
        {
            if (pad.Buttons & PSP_CTRL_RIGHT)
                ADBKeyDown(0x3c);
            else
                ADBKeyUp(0x3c);
        }
        else if (buttons & PSP_CTRL_LEFT)
        {
            if (pad.Buttons & PSP_CTRL_LEFT)
                ADBKeyDown(0x3b);
            else
                ADBKeyUp(0x3b);
        }
        if (buttons & PSP_CTRL_CROSS)
        {
            if (pad.Buttons & PSP_CTRL_CROSS)
                ADBKeyDown(0x4c);
            else
                ADBKeyUp(0x4c);
        }
    }
    else if (input_mode)
    {
        unsigned int key;
        uint8 xlate_danzeff_us[128] = {
            0, 0, 0, 0, 0, 0, 0, 0,
            51, 0, 36, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 53, 0, 0, 0, 0,
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
        char qual_msg[5][12] = {
            "\0",
            " CMD ",
            " OPT ",
            " CTRL ",
            " CMD+OPT "
        };

        if (buttons & PSP_CTRL_SELECT)
            if (pad.Buttons & PSP_CTRL_SELECT)
                qualifiers = (qualifiers + 1) % 5;
        strcpy(msgtxt, qual_msg[qualifiers]);

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
            ADBKeyDown(59);
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
            show_on_right = false;
            break;
            case 2: // move right
            show_on_right = true;
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
            ADBKeyUp(56);
            break;
            case 2:
            ADBKeyUp(58);
            break;
            case 3:
            ADBKeyUp(59);
            break;
            case 4:
            ADBKeyUp(56);
            ADBKeyUp(58);
        }
    }

	// Emergency quit requested? Then quit
	if (emerg_quit)
		QuitEmulator();

	}

	sceKernelDelayThread(10);
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
	// Convert colors to CLUT
	unsigned int *clut = (unsigned int *)(((unsigned int)clut256)|0x40000000);
	for (int i = 0; i < 256; ++i)
		*(clut++) = (0xFF << 24)|(pal[i*3 + 2] << 16)|(pal[i*3 + 1] << 8)|(pal[i*3]);
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
