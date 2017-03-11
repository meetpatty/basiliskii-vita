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
#include <pspirkeyb.h>
#include <pspirkeyb_rawkeys.h>

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


//#define CONFIG_FILE         NULL    /* this will take ms0:/seplugins/pspirkeyb.ini */
extern char *CONFIG_FILE;
#define KERNELMODE          0       /* 1 is untested but required for some keyboards to change baudrate */


// Supported video modes
using std::vector;
static vector<video_mode> VideoModes;


// Constants
const char KEYCODE_FILE_NAME[] = "BasiliskII_keycodes";


// Global variables
static unsigned int __attribute__((aligned(16))) clut256[256];
static unsigned int __attribute__((aligned(16))) clut0_15[256];
static unsigned int __attribute__((aligned(16))) clut1_15[256];
static unsigned int __attribute__((aligned(16))) clut0_24[256];
static unsigned int __attribute__((aligned(16))) clut1_24[256];
static unsigned int __attribute__((aligned(16))) clut2_24[256];

static uint8 __attribute__((aligned(64))) frame_buffer[768*576*4];

int psp_irkeyb_init;

static int32 frame_skip;							// Prefs items

bool refresh_okay = false;

static uint32 BUF_WIDTH = 0;
static uint32 SCR_WIDTH = 0;
static uint32 SCR_HEIGHT = 0;
#define PIXEL_SIZE (4) // using 8888 mode for screen
static uint32 FRAME_SIZE = 0;
static uint32 DISP_BUF = 0;
static uint32 DRAW_BUF = 0;
static uint32 TEX_BUF = 0;
static uint32 DEBUG_BUF = 0;

static uint32 LACE_BUF = 0;                         // frame buffer for interlaced modes

static uint8 *the_buffer = NULL;					// Mac frame buffer (where MacOS draws into)
static uint32 the_buffer_size;						// Size of allocated the_buffer

static int32 psp_screen_x = 640;
static int32 psp_screen_y = 480;
static int32 psp_screen_d = VDEPTH_8BIT;

static int32 d_x, d_y, d_w, d_h;

static int psp_vidout_select = 0; // LCD

static int psp_lcd_aspect = 0; // 4:3

static int psp_mon_aspect = 0; // TV is physically 4:3
static int psp_tv_aspect = 0; // display as 4:3
static int psp_tv_overscan = 0; // don't draw in overscan region of TV
static int psp_tv_laced = 0; // default to progressive

extern int psp_tv_cable;


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


extern "C" {
int pspDveMgrCheckVideoOut();
int pspDveMgrSetVideoOut(int, int, int, int, int, int, int);
}


/*
 * refresh subroutines
 */

static unsigned int list[16384] __attribute__((aligned(16)));

struct texVertex
{
	unsigned short u, v;
	short x, y, z;
};

static void refresh4(void)
{
    int32 BLOCKH = 64;
    int32 BLOCKV = psp_screen_y <= 512 ? psp_screen_y : psp_screen_y / 2;
    int32 i, j;
	struct texVertex *vertices;

    // Mac 4bit chunky to PSP 4bit chunky
    uint32 *sp = (uint32 *)the_buffer;
    uint32 *dp = (uint32 *)((uint32)the_buffer + 768*576/2);
    for (j=0; j<576; j++)
        for (i=0; i<768/8; i++)
        {
            uint32 v = sp[j*768/8 + i];
            dp[j*768/8 + i] = (v>>4)&0x0F0F0F0F | (v<<4)&0xF0F0F0F0;
        }

	sceGuStart(GU_DIRECT,list);
	sceGuClutMode(GU_PSM_8888,0,0x0F,0);	        // 32-bit palette, shift = 0, mask = 0x0F, start = 0
	sceGuClutLoad((16/(32/4)),clut256);			    // upload 2 blocks (each block is 32 bytes / bytesPerColorEntry)
	sceGuEnable(GU_TEXTURE_2D);
	sceGuTexMode(GU_PSM_T4,0,0,0);
	sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGB);
	sceGuTexFilter(GU_LINEAR, GU_LINEAR);

    for (j=0; j<psp_screen_y/BLOCKV; j++)
        for (i=0; i<psp_screen_x/BLOCKH; i++)
        {
            // set the texture block
            uint32 ts = (uint32)the_buffer + 768*576/2 + j * BLOCKV * 768 / 2 + i * BLOCKH / 2;
            sceGuTexImage(0, BLOCKH, 512, 768, (void *)ts);
            sceGuTexSync();

            vertices = (struct texVertex*)sceGuGetMemory(2 * sizeof(struct texVertex));
            // now draw the block with scaling
			vertices[0].u = 0;
            vertices[1].u = BLOCKH;

            vertices[0].v = 0;
            vertices[1].v = BLOCKV;

            vertices[0].x = d_x + i * BLOCKH * d_w / psp_screen_x;
            vertices[1].x = d_x + (i+1) * BLOCKH * d_w / psp_screen_x;

            if (vertices[1].x > (d_x + d_w))
                vertices[1].x = (d_x + d_w);

            vertices[0].y = d_y + j * BLOCKV * d_h / psp_screen_y;
            vertices[1].y = d_y + (j+1) * BLOCKV * d_h / psp_screen_y;

            if (vertices[1].y > (d_y + d_h))
                vertices[1].y = (d_y + d_h);

            vertices[0].z = 0;
            vertices[1].z = 0;

            sceGuDrawArray(GU_SPRITES,GU_TEXTURE_16BIT|GU_VERTEX_16BIT|GU_TRANSFORM_2D, 2,0,vertices);
        }
}

static void refresh8(void)
{
    int32 BLOCKH = 64;
    int32 BLOCKV = psp_screen_y <= 512 ? psp_screen_y : psp_screen_y / 2;
    int32 i, j;
	struct texVertex *vertices;

	sceGuStart(GU_DIRECT,list);
	sceGuClutMode(GU_PSM_8888,0,0xFF,0);	        // 32-bit palette, shift = 0, mask = 0xFF, start = 0
	sceGuClutLoad((256/(32/4)),clut256);			// upload 32 blocks (each block is 32 bytes / bytesPerColorEntry)
	sceGuEnable(GU_TEXTURE_2D);
	sceGuTexMode(GU_PSM_T8,0,0,0);
	sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGB);
	sceGuTexFilter(GU_LINEAR, GU_LINEAR);

    for (j=0; j<psp_screen_y/BLOCKV; j++)
        for (i=0; i<psp_screen_x/BLOCKH; i++)
        {
            // set the texture block
            uint32 ts = (uint32)the_buffer + j * BLOCKV * 768 + i * BLOCKH;
            sceGuTexImage(0, BLOCKH, 512, 768, (void *)ts);
            sceGuTexSync();

            vertices = (struct texVertex*)sceGuGetMemory(2 * sizeof(struct texVertex));
            // now draw the block with scaling
			vertices[0].u = 0;
            vertices[1].u = BLOCKH;

            vertices[0].v = 0;
            vertices[1].v = BLOCKV;

            vertices[0].x = d_x + i * BLOCKH * d_w / psp_screen_x;
            vertices[1].x = d_x + (i+1) * BLOCKH * d_w / psp_screen_x;

            if (vertices[1].x > (d_x + d_w))
                vertices[1].x = (d_x + d_w);

            vertices[0].y = d_y + j * BLOCKV * d_h / psp_screen_y;
            vertices[1].y = d_y + (j+1) * BLOCKV * d_h / psp_screen_y;

            if (vertices[1].y > (d_y + d_h))
                vertices[1].y = (d_y + d_h);

            vertices[0].z = 0;
            vertices[1].z = 0;

            sceGuDrawArray(GU_SPRITES,GU_TEXTURE_16BIT|GU_VERTEX_16BIT|GU_TRANSFORM_2D, 2,0,vertices);
        }
}

static void refresh15(void)
{
    int32 BLOCKH = 64;
    int32 BLOCKV = psp_screen_y <= 512 ? psp_screen_y : psp_screen_y / 2;
    int32 i, j;
	struct texVertex *vertices;

#if 1
    // first pass - convert LSB (xrrr rrgg)
	sceGuStart(GU_DIRECT,list);
	sceGuClutMode(GU_PSM_8888,0,0xFF,0);	        // 32-bit palette, shift = 0, mask = 0xFF, start = 0
	sceGuClutLoad((256/(32/4)),clut0_15);			// upload 32 blocks (each block is 32 bytes / bytesPerColorEntry)
	sceGuEnable(GU_TEXTURE_2D);
	sceGuTexMode(GU_PSM_T16,0,0,0);
	sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGB);
	sceGuTexFilter(GU_LINEAR, GU_LINEAR);

    for (j=0; j<psp_screen_y/BLOCKV; j++)
        for (i=0; i<psp_screen_x/BLOCKH; i++)
        {
            // set the texture block
            uint32 ts = (uint32)the_buffer + j * BLOCKV * 768 * 2 + i * BLOCKH * 2;
            sceGuTexImage(0, BLOCKH, 512, 768, (void *)ts);
            sceGuTexSync();

            vertices = (struct texVertex*)sceGuGetMemory(2 * sizeof(struct texVertex));
            // now draw the block with scaling
			vertices[0].u = 0;
            vertices[1].u = BLOCKH;

            vertices[0].v = 0;
            vertices[1].v = BLOCKV;

            vertices[0].x = d_x + i * BLOCKH * d_w / psp_screen_x;
            vertices[1].x = d_x + (i+1) * BLOCKH * d_w / psp_screen_x;

            if (vertices[1].x > (d_x + d_w))
                vertices[1].x = (d_x + d_w);

            vertices[0].y = d_y + j * BLOCKV * d_h / psp_screen_y;
            vertices[1].y = d_y + (j+1) * BLOCKV * d_h / psp_screen_y;

            if (vertices[1].y > (d_y + d_h))
                vertices[1].y = (d_y + d_h);

            vertices[0].z = 0;
            vertices[1].z = 0;

            sceGuDrawArray(GU_SPRITES,GU_TEXTURE_16BIT|GU_VERTEX_16BIT|GU_TRANSFORM_2D, 2,0,vertices);
        }

    // second pass - convert MSB (gggb bbbb)
	sceGuClutMode(GU_PSM_8888,8,0xFF,0);	        // 32-bit palette, shift = 8, mask = 0xFF, start = 0
	sceGuClutLoad((256/(32/4)),clut1_15);			// upload 32 blocks (each block is 32 bytes / bytesPerColorEntry)
	sceGuTexFunc(GU_TFX_ADD, GU_TCC_RGB);
	sceGuBlendFunc(GU_ADD, GU_FIX, GU_FIX, 0x00FFFFFF, 0x00FFFFFF);
	sceGuEnable(GU_BLEND);

    for (j=0; j<psp_screen_y/BLOCKV; j++)
        for (i=0; i<psp_screen_x/BLOCKH; i++)
        {
            // set the texture block
            uint32 ts = (uint32)the_buffer + j * BLOCKV * 768 * 2 + i * BLOCKH * 2;
            sceGuTexImage(0, BLOCKH, 512, 768, (void *)ts);
            sceGuTexSync();

            vertices = (struct texVertex*)sceGuGetMemory(2 * sizeof(struct texVertex));
            // now draw the block with scaling
			vertices[0].u = 0;
            vertices[1].u = BLOCKH;

            vertices[0].v = 0;
            vertices[1].v = BLOCKV;

            vertices[0].x = d_x + i * BLOCKH * d_w / psp_screen_x;
            vertices[1].x = d_x + (i+1) * BLOCKH * d_w / psp_screen_x;

            if (vertices[1].x > (d_x + d_w))
                vertices[1].x = (d_x + d_w);

            vertices[0].y = d_y + j * BLOCKV * d_h / psp_screen_y;
            vertices[1].y = d_y + (j+1) * BLOCKV * d_h / psp_screen_y;

            if (vertices[1].y > (d_y + d_h))
                vertices[1].y = (d_y + d_h);

            vertices[0].z = 0;
            vertices[1].z = 0;

            sceGuDrawArray(GU_SPRITES,GU_TEXTURE_16BIT|GU_VERTEX_16BIT|GU_TRANSFORM_2D, 2,0,vertices);
        }
   	sceGuDisable(GU_BLEND);
#else
    // Mac 15bit chunky to PSP 15bit chunky
    uint32 *sp = (uint32 *)the_buffer;
    uint32 *dp = (uint32 *)((uint32)the_buffer + 768*576*2);
    for (j=0; j<576; j++)
        for (i=0; i<768/2; i++)
        {
            uint32 v = sp[j*768/2 + i];
            dp[j*768/2 + i] = (v>>2)&0x001F001F | (v>>8)&0x00E000E0 | (v<<8)&0x03000300 | (v<<2)&0x7C007C00;
        }

	sceGuStart(GU_DIRECT,list);
	sceGuEnable(GU_TEXTURE_2D);
	sceGuTexMode(GU_PSM_5551,0,0,0);
	sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGB);
	sceGuTexFilter(GU_LINEAR, GU_LINEAR);

    for (j=0; j<psp_screen_y/BLOCKV; j++)
        for (i=0; i<psp_screen_x/BLOCKH; i++)
        {
            // set the texture block
            uint32 ts = (uint32)the_buffer + 768*576*2 + j * BLOCKV * 768 * 2 + i * BLOCKH * 2;
            sceGuTexImage(0, BLOCKH, 512, 768, (void *)ts);
            sceGuTexSync();

            vertices = (struct texVertex*)sceGuGetMemory(2 * sizeof(struct texVertex));
            // now draw the block with scaling
			vertices[0].u = 0;
            vertices[1].u = BLOCKH;

            vertices[0].v = 0;
            vertices[1].v = BLOCKV;

            vertices[0].x = d_x + i * BLOCKH * d_w / psp_screen_x;
            vertices[1].x = d_x + (i+1) * BLOCKH * d_w / psp_screen_x;

            if (vertices[1].x > (d_x + d_w))
                vertices[1].x = (d_x + d_w);

            vertices[0].y = d_y + j * BLOCKV * d_h / psp_screen_y;
            vertices[1].y = d_y + (j+1) * BLOCKV * d_h / psp_screen_y;

            if (vertices[1].y > (d_y + d_h))
                vertices[1].y = (d_y + d_h);

            vertices[0].z = 0;
            vertices[1].z = 0;

            sceGuDrawArray(GU_SPRITES,GU_TEXTURE_16BIT|GU_VERTEX_16BIT|GU_TRANSFORM_2D, 2,0,vertices);
        }
#endif
}

static void refresh24(void)
{
    int32 BLOCKH = 64;
    int32 BLOCKV = psp_screen_y <= 512 ? psp_screen_y : psp_screen_y / 2;
    int32 i, j;
	struct texVertex *vertices;

    sceGuStart(GU_DIRECT,list);

    for (j=0; j<psp_screen_y/BLOCKV; j++)
        for (i=0; i<psp_screen_x/BLOCKH; i++)
        {
            uint32 ts = (uint32)the_buffer + j * BLOCKV * 768 * 4 + i * BLOCKH * 4;

            // copy the block to vram
            sceGuCopyImage(GU_PSM_8888, 0, 0, BLOCKH, BLOCKV, 768, (void *)ts, 0, 0, BLOCKH, (void*)(0x40000000 | (uint32)sceGeEdramGetAddr() + TEX_BUF));
            sceGuTexSync();

            // first pass - convert red
            sceGuClutMode(GU_PSM_8888,8,0xFF,0);	        // 32-bit palette, shift = 8, mask = 0xFF, start = 0
            sceGuClutLoad((256/(32/4)),clut0_24);			// upload 32 blocks (each block is 32 bytes / bytesPerColorEntry)
            sceGuEnable(GU_TEXTURE_2D);
            sceGuTexMode(GU_PSM_T32,0,0,0);
            sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGB);
            sceGuTexFilter(GU_LINEAR, GU_LINEAR);
            sceGuDisable(GU_BLEND);

            // set the texture block
            sceGuTexImage(0, BLOCKH, 512, BLOCKH, (void *)(0x40000000 | (uint32)sceGeEdramGetAddr() + TEX_BUF));
            sceGuTexSync();

            vertices = (struct texVertex*)sceGuGetMemory(2 * sizeof(struct texVertex));
            // now draw the block with scaling
			vertices[0].u = 0;
            vertices[1].u = BLOCKH;

            vertices[0].v = 0;
            vertices[1].v = BLOCKV;

            vertices[0].x = d_x + i * BLOCKH * d_w / psp_screen_x;
            vertices[1].x = d_x + (i+1) * BLOCKH * d_w / psp_screen_x;

            if (vertices[1].x > (d_x + d_w))
                vertices[1].x = (d_x + d_w);

            vertices[0].y = d_y + j * BLOCKV * d_h / psp_screen_y;
            vertices[1].y = d_y + (j+1) * BLOCKV * d_h / psp_screen_y;

            if (vertices[1].y > (d_y + d_h))
                vertices[1].y = (d_y + d_h);

            vertices[0].z = 0;
            vertices[1].z = 0;

            sceGuDrawArray(GU_SPRITES,GU_TEXTURE_16BIT|GU_VERTEX_16BIT|GU_TRANSFORM_2D, 2,0,vertices);

            // second pass - convert green
            sceGuClutMode(GU_PSM_8888,16,0xFF,0);	        // 32-bit palette, shift = 16, mask = 0xFF, start = 0
            sceGuClutLoad((256/(32/4)),clut1_24);			// upload 32 blocks (each block is 32 bytes / bytesPerColorEntry)
            sceGuTexFunc(GU_TFX_ADD, GU_TCC_RGB);
            sceGuBlendFunc(GU_ADD, GU_FIX, GU_FIX, 0x00FFFFFF, 0x00FFFFFF);
            sceGuEnable(GU_BLEND);

            sceGuDrawArray(GU_SPRITES,GU_TEXTURE_16BIT|GU_VERTEX_16BIT|GU_TRANSFORM_2D, 2,0,vertices);

            // third pass - convert blue
            sceGuClutMode(GU_PSM_8888,24,0xFF,0);	        // 32-bit palette, shift = 24, mask = 0xFF, start = 0
            sceGuClutLoad((256/(32/4)),clut2_24);			// upload 32 blocks (each block is 32 bytes / bytesPerColorEntry)

            sceGuDrawArray(GU_SPRITES,GU_TEXTURE_16BIT|GU_VERTEX_16BIT|GU_TRANSFORM_2D, 2,0,vertices);
        }

   	sceGuDisable(GU_BLEND);
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
	add_mode(640, 480, 0x82, PSPBytesPerRow(640, vdepth), vdepth);
	add_mode(768, 432, 0x83, PSPBytesPerRow(768, vdepth), vdepth);
	add_mode(768, 576, 0x84, PSPBytesPerRow(768, vdepth), vdepth);
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

void psp_video_setup(void)
{
	if (psp_vidout_select)
	{
		if (psp_tv_cable == 1)
			pspDveMgrSetVideoOut(2, 0x1d1, 720, 503, 1, 15, 0); // composite
		else
			if (psp_tv_laced)
				pspDveMgrSetVideoOut(0, 0x1d1, 720, 503, 1, 15, 0); // component interlaced
			else
				pspDveMgrSetVideoOut(0, 0x1d2, 720, 480, 1, 15, 0); // component progressive
	}
	else
	{
		sceDisplaySetMode(0, 480, 272);
	}

	// setup GU
	sceGuInit();
	sceGuStart(GU_DIRECT,list);
	sceGuDrawBuffer(GU_PSM_8888,(void*)DRAW_BUF,BUF_WIDTH);
	sceGuDispBuffer(SCR_WIDTH,SCR_HEIGHT,(void*)DISP_BUF,BUF_WIDTH);
	sceGuDepthBuffer((void*)DEBUG_BUF,BUF_WIDTH);
	sceGuOffset(2048 - (SCR_WIDTH/2),2048 - (SCR_HEIGHT/2));
	sceGuViewport(2048,2048,SCR_WIDTH,SCR_HEIGHT);
	sceGuDepthRange(65535,0);
	sceGuDepthMask(GU_TRUE);
	sceGuDisable(GU_DEPTH_TEST);
   	sceGuDisable(GU_BLEND);
	sceGuScissor(0,0,SCR_WIDTH,SCR_HEIGHT);
	sceGuEnable(GU_SCISSOR_TEST);
	sceGuFrontFace(GU_CW);
	sceGuEnable(GU_TEXTURE_2D);
	sceGuClear(GU_COLOR_BUFFER_BIT);
	sceGuFinish();
	sceGuSync(0,0);
	sceDisplayWaitVblankStart();
	sceGuDisplay(GU_TRUE);

    memset((void *)(0x40000000 | (uint32)sceGeEdramGetAddr() + DISP_BUF), 0, 768*480*4);
    if (psp_tv_laced)
        memset((void *)(0x40000000 | LACE_BUF), 0, 768*503*4);
    else
        memset((void *)(0x40000000 | (uint32)sceGeEdramGetAddr() + DRAW_BUF), 0, 768*480*4);

	if (psp_tv_laced)
		sceDisplaySetFrameBuf((void *)(0x40000000 | LACE_BUF), 768, GU_PSM_8888, PSP_DISPLAY_SETBUF_NEXTFRAME);
	else
		sceDisplaySetFrameBuf((void *)(0x40000000 | (uint32)sceGeEdramGetAddr() + DISP_BUF), 768, GU_PSM_8888, PSP_DISPLAY_SETBUF_NEXTFRAME);

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

	// Init keycode translation
	keycode_init();

    // init pspirkeyb
    psp_irkeyb_init = pspIrKeybInit( CONFIG_FILE, KERNELMODE );
    if (psp_irkeyb_init == PSP_IRKBD_RESULT_OK)
        pspIrKeybOutputMode(PSP_IRKBD_OUTPUT_MODE_SCANCODE);

	// Read prefs
	frame_skip = PrefsFindInt32("frameskip");
    psp_vidout_select = PrefsFindBool("psptv") ? 1 : 0;
    if (psp_vidout_select == 0)
        psp_lcd_aspect = PrefsFindInt32("pspdar");
    else
    {
        psp_mon_aspect = PrefsFindInt32("pspmar");
        psp_tv_aspect = PrefsFindInt32("pspdar");
        psp_tv_overscan = PrefsFindBool("psposcan") ? 1 : 0;
        if (psp_tv_cable == 1)
            psp_tv_laced = 1;
        else if (psp_tv_cable == 2)
            psp_tv_laced = PrefsFindBool("psplaced") ? 1 : 0;
    }
    // set PSP screen variables
    BUF_WIDTH = 768;
    SCR_WIDTH = psp_vidout_select ? 720 : 480;
    SCR_HEIGHT = psp_vidout_select ? 480 : 272;
    FRAME_SIZE = BUF_WIDTH * SCR_HEIGHT * PIXEL_SIZE;
    DISP_BUF = 0;
    if (!psp_tv_laced)
    {
        DRAW_BUF = FRAME_SIZE;
        TEX_BUF = DRAW_BUF + FRAME_SIZE;
        DEBUG_BUF = TEX_BUF + 64*4*576;
    }
    else
    {
        DRAW_BUF = 0; // not double buffered
        TEX_BUF = FRAME_SIZE;
        DEBUG_BUF = TEX_BUF + 64*4*576;
        LACE_BUF = (uint32)sceGeEdramGetAddr() + DEBUG_BUF + 512*4*8;
    }
    if (psp_vidout_select)
    {
        switch (psp_mon_aspect*4 + psp_tv_aspect*2 + psp_tv_overscan)
        {
            case 0: // 4:3 monitor, 4:3 display, no overscan
            d_w = 640;
            d_h = 448;
            d_x = (720-640)/2;
            d_y = (480-448)/2;
            break;
            case 1: // 4:3 monitor, 4:3 display, overscan
            d_w = 720;
            d_h = 480;
            d_x = 0;
            d_y = 0;
            break;
            case 2: // 4:3 monitor, 16:9 display, no overscan
            d_w = 640;
            d_h = 336;
            d_x = (720-640)/2;
            d_y = (480-336)/2;
            break;
            case 3: // 4:3 monitor, 16:9 display, overscan
            d_w = 720;
            d_h = 360;
            d_x = 0;
            d_y = (480-360)/2;
            break;
            case 4: // 16:9 monitor, 4:3 display, no overscan
            d_w = 480;
            d_h = 448;
            d_x = (720-480)/2;
            d_y = (480-448)/2;
            break;
            case 5: // 16:9 monitor, 4:3 display, overscan
            d_w = 540;
            d_h = 480;
            d_x = (720-540)/2;
            d_y = 0;
            break;
            case 6: // 16:9 monitor, 16:9 display, no overscan
            d_w = 640;
            d_h = 448;
            d_x = (720-640)/2;
            d_y = (480-448)/2;
            break;
            case 7: // 16:9 monitor, 16:9 display, overscan
            d_w = 720;
            d_h = 480;
            d_x = 0;
            d_y = 0;
            break;
        }
    }
    else
    {
        if (psp_lcd_aspect)
        {
            d_w = 480;
            d_h = 272;
            d_x = 0;
            d_y = 0;
        }
        else
        {
            d_w = 368;
            d_h = 272;
            d_x = (480-368)/2;
            d_y = 0;
        }
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
	else if ((default_width == 640) && (default_height == 480))
		default_mode = 0x82;
	else if ((default_width == 768) && (default_height == 432))
		default_mode = 0x83;
	else if ((default_width == 768) && (default_height == 576))
		default_mode = 0x84;
	else {
			default_width = 640;
			default_height = 480;
			default_depth = 8;
			default_mode = 0x81;
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

    // close pspirkeyb
    if (psp_irkeyb_init == PSP_IRKBD_RESULT_OK)
        pspIrKeybFinish();
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

extern char psp_home[];

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
    unsigned char buffer[256];
    SIrKeybScanCodeData * scanData;
    int i, count, length;

    static uint8 mac_key[128] = {
        255, 53, 18, 19, 20, 21, 23, 22,
        26, 28, 25, 29, 27, 24, 51, 48,
        12, 13, 14, 15, 17, 16, 32, 34,
        31, 35, 33, 30, 36, 54, 0, 1,
        2, 3, 5, 4, 38, 40, 37, 41,
        39, 10, 56, 42, 6, 7, 8, 9,
        11, 45, 46, 43, 47, 44, 56, 67,
        58, 49, 57, 122, 120, 99, 118, 96,
        97, 98, 100, 101, 109, 71, 107, 89,
        91, 92, 78, 86, 87, 88, 69, 83,
        84, 85, 82, 65, 255, 105, 255, 103,
        111, 107, 113, 255, 255, 255, 255, 255,
        76, 54, 75, 105, 58, 255, 115, 62,
        116, 59, 60, 119, 61, 121, 114, 117,
        255, 255, 255, 255, 255, 81, 69, 113,
        255, 255, 255, 255, 78, 55, 55, 255
    };

    if (pspIrKeybReadinput(buffer, &length) >= 0)
    {
        if (length % sizeof(SIrKeybScanCodeData) > 0)
            return;

        count = length / sizeof(SIrKeybScanCodeData);
        for( i=0; i < count; i++ )
        {
            scanData = (SIrKeybScanCodeData *)buffer+i;
            if (scanData->pressed)
            {
                if (mac_key[scanData->raw] == 57)
                {
                    // caps lock
                    caps_lock = caps_lock ? false : true;
                    if (caps_lock)
                        ADBKeyDown(57);
                    else
                        ADBKeyUp(57);
                    continue;
                }

                ADBKeyDown(mac_key[scanData->raw]);
            }
            else
            {
                if (mac_key[scanData->raw] == 57)
                    continue; // ignore caps lock key up

                ADBKeyUp(mac_key[scanData->raw]);
            }

        }
    }
}

void handle_menu(SceCtrlData pad, char *message)
{
    uint32 buttons;
    static uint32 oldButtons = 0;
    static uint32 sel = 0;
    static uint32 max = 0;
    static uint32 idx = 0; // 0 = input maps, 1 = floppies, 2 = cdroms
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
    if (numimaps == -1)
    {
        strcpy(temp, psp_home);
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

    buttons = pad.Buttons ^ oldButtons; // set if button changed
    oldButtons = pad.Buttons;

	if (buttons & (PSP_CTRL_LEFT | PSP_CTRL_RIGHT))
        if (pad.Buttons & PSP_CTRL_LEFT)
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
        else if (pad.Buttons & PSP_CTRL_RIGHT)
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

	if (buttons & PSP_CTRL_UP)
        if (pad.Buttons & PSP_CTRL_UP)
            sel = sel>0 ? sel-1 : max-1;

	if (buttons & PSP_CTRL_DOWN)
        if (pad.Buttons & PSP_CTRL_DOWN)
            sel = sel<max-1 ? sel+1 : 0;

    if (idx == 0)
    {
        // doing imaps
        strcpy(temp, " ");
        strcat(temp, imaps[sel].filename);
        strcat(temp, " ");
    }
    else if (idx == 1)
    {
        // doing floppies
        strcpy(temp, " ");
        strcat(temp, floppies[sel].filename);
        strcat(temp, " ");
    }
    else if (idx == 2)
    {
        // doing cdroms
        strcpy(temp, " ");
        strcat(temp, cdroms[sel].filename);
        strcat(temp, " ");
    }

    strncpy(message, temp, 67);


   	if (buttons & PSP_CTRL_CROSS)
        if (pad.Buttons & PSP_CTRL_CROSS)
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

void show_msg(char *message, uint32 fc, uint32 bc)
{
	struct texVertex *vertices;

    pspDebugScreenSetOffset(DEBUG_BUF);
	pspDebugScreenSetBackColor(bc);
	pspDebugScreenSetTextColor(fc);
	pspDebugScreenSetXY(0, 0);
	pspDebugScreenPrintf("%s", message);

	sceGuEnable(GU_TEXTURE_2D);

	sceGuTexMode(GU_PSM_8888,0,0,0);
	sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGB);
	sceGuTexFilter(GU_LINEAR, GU_LINEAR);
	sceGuTexImage(0, 512, 8, 512, (void *)(0x40000000 | (uint32)sceGeEdramGetAddr() + DEBUG_BUF));
	sceGuTexSync();

	vertices = (struct texVertex*)sceGuGetMemory(2 * sizeof(struct texVertex));
	vertices[0].u = 0;
	vertices[1].u = strlen(message)*7;
	vertices[0].v = 0;
	vertices[1].v = 8;
	vertices[0].x = (SCR_WIDTH-strlen(message)*7)/2;
	vertices[1].x = vertices[0].x + strlen(message)*7;
	vertices[0].y = SCR_HEIGHT-48;
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
    static int stick[16] = { -8, -8, -6, -4, -2, -1, -1, 0, 0, 0, 1, 1, 2, 4, 6, 8 };

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
            show_menu = show_menu ? false : true; // toggle imap/floppy/cdrom menu
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
                refresh4();
            else if (psp_screen_d == VDEPTH_8BIT)
                refresh8();
            else if (psp_screen_d == VDEPTH_16BIT)
                refresh15();
            else if (psp_screen_d == VDEPTH_32BIT)
                refresh24();

            if (input_mode)
            {
                danzeff_moveTo(show_on_right ? d_x+d_w-150 : d_x, d_y+d_h-150);
                danzeff_render();
            }
            else if (show_menu)
                handle_menu(pad, msgtxt);

            if (msgtxt[0] != 0)
                show_msg(msgtxt, fc, bc);

            sceGuFinish();
            sceGuSync(0,0);

            if (psp_tv_laced)
            {
                uint32 src = 0x40000000 | ((uint32)sceGeEdramGetAddr() + DRAW_BUF);
                uint32 dst = 0x40000000 | LACE_BUF;

                sceGuStart(GU_DIRECT,list);
                sceGuCopyImage(GU_PSM_8888, 0, 0, 720, 240, 768*2, (void *)(src + 768*4), 0, 0, 768, (void*)dst);
                sceGuTexSync();
                sceGuCopyImage(GU_PSM_8888, 0, 0, 720, 240, 768*2, (void *)src, 0, 0, 768, (void *)(dst + 768*262*4));
                sceGuTexSync();
                sceGuFinish();
                sceGuSync(0,0);
            }
            else
                sceGuSwapBuffers();
        }

	// process inputs
    if (!input_mode && !show_menu)
    {
        // mouse
        if (abs(pad.Lx - 128) >= 32 || abs(pad.Ly - 128) >= 32)
            ADBMouseMoved(stick[((pad.Lx - 128) >> 4) + 8], stick[((pad.Ly - 128) >> 4) + 8]);

        for (int i=0; i<NUM_MAPS; i++)
        {
            if (button_map[i][0] == 0)
                break;
            if ((pad.Buttons & button_map[i][0]) == button_map[i][0] && (~pad.Buttons & button_map[i][1]) == button_map[i][1])
                key_down(i);
            else if (button_map[i][6])
                key_up(i);
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

    // process keyboard input
    if (psp_irkeyb_init == PSP_IRKBD_RESULT_OK)
        handle_keyboard();

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
	unsigned int *clut = (unsigned int *)(((unsigned int)clut256)|0x40000000);
	for (i = 0; i < 256; ++i)
		clut[i] = (0xFF << 24)|(pal[i*3 + 2] << 16)|(pal[i*3 + 1] << 8)|(pal[i*3]);

    if (psp_screen_d == VDEPTH_16BIT)
    {
        // convert xrrr rrgg
        unsigned int *dclut = (unsigned int *)(((unsigned int)clut0_15)|0x40000000);
        for (i = 0; i < 256; ++i)
            dclut[i] = (0xFF << 24) | (clut[(i&3)<<3] & 0x0000FF00) | (clut[(i&0x7C)>>2] & 0x000000FF);

        // convert gggb bbbb
        dclut = (unsigned int *)(((unsigned int)clut1_15)|0x40000000);
        for (i = 0; i < 256; ++i)
            dclut[i] = (0xFF << 24) | (clut[i&0x1F] & 0x00FF0000) | (clut[(i&0xE0)>>5] & 0x0000FF00);
    }
    if (psp_screen_d == VDEPTH_32BIT)
    {
        // convert red
        unsigned int *dclut = (unsigned int *)(((unsigned int)clut0_24)|0x40000000);
        for (i = 0; i < 256; ++i)
            dclut[i] = (0xFF << 24) | (clut[i] & 0x000000FF);

        // convert green
        dclut = (unsigned int *)(((unsigned int)clut1_24)|0x40000000);
        for (i = 0; i < 256; ++i)
            dclut[i] = (0xFF << 24) | (clut[i] & 0x0000FF00);

        // convert blue
        dclut = (unsigned int *)(((unsigned int)clut2_24)|0x40000000);
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
