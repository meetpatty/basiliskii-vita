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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <pspsdk.h>

#include "intraFont.h"
#include "gui_psp.h"


#include "sysdeps.h"
#include "user_strings.h"
#include "user_strings_psp.h"
#include "main.h"

#define Get_String(x) const_cast<char*>(GetString(x))

extern char *RequestFile (char *initialPath);

extern bool emerg_quit;						    // Flag: emergency quit requested

extern char psp_home[256];


/*
 * OSK support code
 *
 */

#define BUF_WIDTH (512)
#define SCR_WIDTH (480)
#define SCR_HEIGHT (272)
#define PIXEL_SIZE (4) /* change this if you change to another screenmode */
#define FRAME_SIZE (BUF_WIDTH * SCR_HEIGHT * PIXEL_SIZE)
#define ZBUF_SIZE (BUF_WIDTH SCR_HEIGHT * 2) /* zbuffer seems to be 16-bit? */

static unsigned int __attribute__((aligned(16))) list[4096];

int gui_use_intrafont = 0;
intraFont *ltn8 = 0;

static void SetupGu(void)
{
	sceGuInit();
	sceGuStart(GU_DIRECT,list);
	sceGuDrawBuffer(GU_PSM_8888,(void*)0,BUF_WIDTH);
	sceGuDispBuffer(SCR_WIDTH,SCR_HEIGHT,(void*)0x88000,BUF_WIDTH);
	sceGuDepthBuffer((void*)0x110000,BUF_WIDTH);
	sceGuOffset(2048 - (SCR_WIDTH/2),2048 - (SCR_HEIGHT/2));
	sceGuViewport(2048,2048,SCR_WIDTH,SCR_HEIGHT);
	sceGuDepthRange(0xc350,0x2710);
	sceGuScissor(0,0,SCR_WIDTH,SCR_HEIGHT);
	sceGuEnable(GU_SCISSOR_TEST);
	sceGuDepthFunc(GU_GEQUAL);
	sceGuEnable(GU_DEPTH_TEST);
	sceGuFrontFace(GU_CW);
	sceGuShadeModel(GU_SMOOTH);
	sceGuEnable(GU_CULL_FACE);
	sceGuEnable(GU_CLIP_PLANES);
	sceGuFinish();
	sceGuSync(0,0);
	sceDisplayWaitVblankStart();
	sceGuDisplay(GU_TRUE);
}

static void ResetGu(void)
{
	sceGuInit();
	sceGuStart(GU_DIRECT, list);
	sceGuDrawBuffer(GU_PSM_8888, (void*)0, BUF_WIDTH);
	sceGuDispBuffer(SCR_WIDTH, SCR_HEIGHT, (void*)0x88000, BUF_WIDTH);
	sceGuDepthBuffer((void*)0x110000, BUF_WIDTH);
	sceGuOffset(2048 - (SCR_WIDTH/2), 2048 - (SCR_HEIGHT/2));
	sceGuViewport(2048, 2048, SCR_WIDTH, SCR_HEIGHT);
	sceGuDepthRange(65535, 0);
	sceGuScissor(0, 0, SCR_WIDTH, SCR_HEIGHT);
	sceGuEnable(GU_SCISSOR_TEST);
	sceGuDepthFunc(GU_GEQUAL);
	sceGuEnable(GU_DEPTH_TEST);
	sceGuFrontFace(GU_CW);
	sceGuShadeModel(GU_SMOOTH);
	sceGuEnable(GU_CULL_FACE);
	sceGuEnable(GU_CLIP_PLANES);
	sceGuEnable(GU_BLEND);
	sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
	sceGuFinish();
	sceGuSync(0,0);
	sceDisplayWaitVblankStart();
	sceGuDisplay(GU_TRUE);
}

int get_text_osk(char *input, unsigned short *intext, unsigned short *desc)
{
	int done=0;
	unsigned short outtext[128] = { 0 }; // text after input

	SetupGu();

	SceUtilityOskData data;
	memset(&data, 0, sizeof(data));
	data.language = 2;			// key glyphs: 0-1=hiragana, 2+=western/whatever the other field says
	data.lines = 1;				// just one line
	data.unk_24 = 1;			// set to 1
	data.desc = desc;
	data.intext = intext;
	data.outtextlength = 128;	// sizeof(outtext) / sizeof(unsigned short)
	data.outtextlimit = 50;		// just allow 50 chars
	data.outtext = (unsigned short*)outtext;

	SceUtilityOskParams osk;
	memset(&osk, 0, sizeof(osk));
	osk.base.size = sizeof(osk);
	// dialog language: 0=Japanese, 1=English, 2=French, 3=Spanish, 4=German,
	// 5=Italian, 6=Dutch, 7=Portuguese, 8=Russian, 9=Korean, 10-11=Chinese, 12+=default
	osk.base.language = 1;
	osk.base.buttonSwap = 1;		// X button: 1
	osk.base.graphicsThread = 17;	// gfx thread pri
	osk.base.accessThread = 19;		// Access/fileio thread pri
	osk.base.fontThread = 18;
	osk.base.soundThread = 16;
	osk.datacount = 1;
	osk.data = &data;

	int rc = sceUtilityOskInitStart(&osk);
	if (rc) return 0;

	while(!done) {
		int i,j=0;

		sceGuStart(GU_DIRECT,list);

		// clear screen
		sceGuClearColor(0xff554433);
		sceGuClearDepth(0);
		sceGuClear(GU_COLOR_BUFFER_BIT|GU_DEPTH_BUFFER_BIT);

		sceGuFinish();
		sceGuSync(0,0);

		switch(sceUtilityOskGetStatus())
		{
			case PSP_UTILITY_DIALOG_INIT :
			break;
			case PSP_UTILITY_DIALOG_VISIBLE :
			sceUtilityOskUpdate(2); // 2 is taken from ps2dev.org recommendation
			break;
			case PSP_UTILITY_DIALOG_QUIT :
			sceUtilityOskShutdownStart();
			break;
			case PSP_UTILITY_DIALOG_FINISHED :
			done = 1;
			break;
			case PSP_UTILITY_DIALOG_NONE :
			default :
			break;
		}

		for(i = 0; data.outtext[i]; i++)
			if (data.outtext[i]!='\0' && data.outtext[i]!='\n' && data.outtext[i]!='\r')
			{
				input[j] = data.outtext[i];
				j++;
			}
		input[j] = 0;

		// wait TWO vblanks because one makes the input "twitchy"
		sceDisplayWaitVblankStart();
		sceDisplayWaitVblankStart();
		sceGuSwapBuffers();
	}

	ResetGu();

	return 1;
}

/*
 * intraFont support code
 *
 */

void gui_font_init(void)
{
	ResetGu();

	intraFontInit();

	// Load font
    ltn8 = intraFontLoad("flash0:/font/ltn8.pgf",INTRAFONT_CACHE_ASCII); // small latin sans-serif regular

	// Make sure the fonts are loaded
	gui_use_intrafont = ltn8 ? 1 : 0;
}


/*
 * GUI support code
 *
 */

void gui_PrePrint(void)
{
	if (gui_use_intrafont)
	{
		sceGuStart(GU_DIRECT, list);

		sceGumMatrixMode(GU_PROJECTION);
		sceGumLoadIdentity();
		sceGumPerspective( 75.0f, 16.0f/9.0f, 0.5f, 1000.0f);

        sceGumMatrixMode(GU_VIEW);
		sceGumLoadIdentity();

		sceGumMatrixMode(GU_MODEL);
		sceGumLoadIdentity();

		sceGuClearColor(0xFF7F7F7F);
		sceGuClearDepth(0);
		sceGuClear(GU_COLOR_BUFFER_BIT|GU_DEPTH_BUFFER_BIT);
	}
}

void gui_PostPrint(void)
{
	if (gui_use_intrafont)
	{
        // End drawing
		sceGuFinish();
		sceGuSync(0,0);

		// Swap buffers (waiting for vsync)
		sceDisplayWaitVblankStart();
		sceGuSwapBuffers();
	}
}

int gui_PrintWidth(char *text)
{
	if (gui_use_intrafont)
		return (int)intraFontMeasureText(ltn8, text);
	else
		return strlen(text)*7;
}

void gui_Print(char *text, u32 fc, u32 bc, int x, int y)
{
	if (gui_use_intrafont)
	{
		intraFontSetStyle(ltn8, 1.0f, fc, bc,INTRAFONT_ALIGN_LEFT); // scale = 1.0
		intraFontPrint(ltn8, x, y, text);
	}
	else
	{
		static int lasty = -1;

		pspDebugScreenSetTextColor(0xFFFFFFFF);
		pspDebugScreenSetBackColor(0xFF000000);
		if (y != lasty)
		{
			// erase line if not the same as last print
			pspDebugScreenSetXY(0, y/8);
			pspDebugScreenPrintf("                                                                    ");
			lasty = y;
		}
		pspDebugScreenSetTextColor(fc);
		pspDebugScreenSetBackColor(bc);
		pspDebugScreenSetXY(x/7, y/8);
		pspDebugScreenPrintf("%s", text);
	}
}

char *RequestString (char *initialStr)
{
	int ok, i;
	static char str[64];
	unsigned short intext[128]  = { 0 }; // text already in the edit box on start
	unsigned short desc[128]	= { 'E', 'n', 't', 'e', 'r', ' ', 'S', 't', 'r', 'i', 'n', 'g', 0 }; // description

	if (initialStr[0] != 0)
		for (i=0; i<=strlen(initialStr); i++)
			intext[i] = (unsigned short)initialStr[i];

	ok = get_text_osk(str, intext, desc);

	if (!gui_use_intrafont)
	{
		pspDebugScreenInit();
		pspDebugScreenSetBackColor(0xFF000000);
		pspDebugScreenSetTextColor(0xFFFFFFFF);
		pspDebugScreenClear();
	}

	if (ok)
		return str;

	return 0;
}

/*
 * GUI code
 *
 */

int gui_menu_len(struct gui_menu *menu)
{
	int c = 0;
	while (menu[c].flags != GUI_END_OF_MENU)
		c++;
	return c;
}

int gui_list_len(struct gui_list *list)
{
	int c = 0;
	while (list[c].index != GUI_END_OF_LIST)
		c++;
	return c;
}

void do_gui(struct gui_menu *menu, void *menufn, char *exit_msg)
{
	SceCtrlData pad;
	int msy, mlen;
	int i, j, k, min, max;
	int csel = 0;
	u32 fc, bc;
	int tx, ty;
	char line[69];

	mlen = gui_menu_len(menu);
	msy = 136 - mlen*8;

	pspDebugScreenInit();
	pspDebugScreenSetBackColor(0xFF000000);
	pspDebugScreenSetTextColor(0xFFFFFFFF);
	pspDebugScreenClear();

	sceCtrlReadBufferPositive(&pad, 1);
	while (!(pad.Buttons & PSP_CTRL_CIRCLE))
	{
		void (*fnptr)(struct gui_menu *);
		fnptr = (void (*)(struct gui_menu *))menufn;
		if (fnptr)
			(*fnptr)(menu);
		if (pad.Buttons & PSP_CTRL_CROSS)
		{
			if (menu[csel].enable == GUI_ENABLED)
				switch (menu[csel].flags & 0x0FFFFFFF)
				{
					void (*fnptr)(void *);
					char temp[256];
					char *req;

					case GUI_MENU:
					while (pad.Buttons & PSP_CTRL_CROSS)
						sceCtrlReadBufferPositive(&pad, 1);
					do_gui((struct gui_menu *)menu[csel].field1, menu[csel].field2, Get_String(STR_MENU_BACK));
					break;
					case GUI_FUNCTION:
					while (pad.Buttons & PSP_CTRL_CROSS)
						sceCtrlReadBufferPositive(&pad, 1);
					fnptr = (void (*)(void *))menu[csel].field1;
					(*fnptr)(menu[csel].field2);
					break;
					case GUI_TOGGLE:
					while (pad.Buttons & PSP_CTRL_CROSS)
						sceCtrlReadBufferPositive(&pad, 1);
					*(int *)menu[csel].field1 ^= 1;
					break;
					case GUI_SELECT:
					if (pad.Buttons & (PSP_CTRL_RIGHT | PSP_CTRL_DOWN))
					{
						// next selection
						while (pad.Buttons & (PSP_CTRL_RIGHT | PSP_CTRL_DOWN))
							sceCtrlReadBufferPositive(&pad, 1);
						j = gui_list_len((struct gui_list *)menu[csel].field1);
						k = *(int *)menu[csel].field2;
						k = (k == (j - 1)) ? 0 : k + 1;
						*(int *)menu[csel].field2 = k;
					}
					if (pad.Buttons & (PSP_CTRL_LEFT | PSP_CTRL_UP))
					{
						// previous selection
						while (pad.Buttons & (PSP_CTRL_LEFT | PSP_CTRL_UP))
							sceCtrlReadBufferPositive(&pad, 1);
						j = gui_list_len((struct gui_list *)menu[csel].field1);
						k = *(int *)menu[csel].field2;
						k = (k == 0) ? j - 1 : k - 1;
						*(int *)menu[csel].field2 = k;
					}
					break;
					case GUI_FILE:
					while (pad.Buttons & PSP_CTRL_CROSS)
						sceCtrlReadBufferPositive(&pad, 1);
					if (*(int *)menu[csel].field1)
						free(*(char **)menu[csel].field1);
					strcpy(temp, psp_home);
					strcat(temp, (char *)menu[csel].field2);
					strcat(temp, "/");
					req = RequestFile(temp);
					*(char **)menu[csel].field1 = req ? strdup(req) : 0;
					break;
					case GUI_STRING:
					while (pad.Buttons & PSP_CTRL_CROSS)
						sceCtrlReadBufferPositive(&pad, 1);
					temp[0] = 0;
					if (*(int *)menu[csel].field1)
					{
						strcpy(temp, *(char **)menu[csel].field1);
						free(*(char **)menu[csel].field1);
					}
					req = RequestString(temp);
					*(char **)menu[csel].field1 = req ? strdup(req) : 0;
					break;
					case GUI_INTEGER:
					if (menu[csel].field2)
					{
						int *rng = (int *)menu[csel].field2;
						min = rng[0];
						max = rng[1];
					}
					else
					{
						min = (int)0x80000000;
						max = 0x7FFFFFFF;
					}
					if (pad.Buttons & PSP_CTRL_RIGHT)
					{
						// +1
						//while (pad.Buttons & PSP_CTRL_RIGHT)
						//	sceCtrlReadBufferPositive(&pad, 1);
						sceKernelDelayThread(200*1000);
						*(int *)menu[csel].field1 += 1;
						if (*(int *)menu[csel].field1 > max)
							*(int *)menu[csel].field1 = max;
					}
					if (pad.Buttons & PSP_CTRL_LEFT)
					{
						// -1
						//while (pad.Buttons & PSP_CTRL_LEFT)
						//	sceCtrlReadBufferPositive(&pad, 1);
						sceKernelDelayThread(200*1000);
						*(int *)menu[csel].field1 -= 1;
						if (*(int *)menu[csel].field1 < min)
							*(int *)menu[csel].field1 = min;
					}
					if (pad.Buttons & PSP_CTRL_DOWN)
					{
						// +10
						//while (pad.Buttons & PSP_CTRL_DOWN)
						//	sceCtrlReadBufferPositive(&pad, 1);
						sceKernelDelayThread(200*1000);
						*(int *)menu[csel].field1 += 10;
						if (*(int *)menu[csel].field1 > max)
							*(int *)menu[csel].field1 = max;
					}
					if (pad.Buttons & PSP_CTRL_UP)
					{
						// -10
						//while (pad.Buttons & PSP_CTRL_UP)
						//	sceCtrlReadBufferPositive(&pad, 1);
						sceKernelDelayThread(200*1000);
						*(int *)menu[csel].field1 -= 10;
						if (*(int *)menu[csel].field1 < min)
							*(int *)menu[csel].field1 = min;
					}
					break;
				}
		}
		else
		{
			if (pad.Buttons & PSP_CTRL_UP)
			{
				while (pad.Buttons & PSP_CTRL_UP)
					sceCtrlReadBufferPositive(&pad, 1);
				csel = (csel == 0) ? mlen - 1 : csel - 1;
			}
			if (pad.Buttons & PSP_CTRL_DOWN)
			{
				while (pad.Buttons & PSP_CTRL_DOWN)
					sceCtrlReadBufferPositive(&pad, 1);
				csel = (csel == (mlen - 1)) ? 0 : csel + 1;
			}
		}

		sceDisplayWaitVblankStart();
		gui_PrePrint();
		fc = 0xFFFFFFFF;
		bc = 0xFF000000;

		switch (menu[csel].flags & 0x0FFFFFFF)
		{
			case GUI_MENU:
			gui_Print(Get_String(STR_MENU_MENU), fc, bc, 14, 264);
			break;
			case GUI_FUNCTION:
			gui_Print(Get_String(STR_MENU_FUNCTION), fc, bc, 14, 264);
			break;
			case GUI_SELECT:
			gui_Print(Get_String(STR_MENU_SELECT), fc, bc, 14, 264);
			break;
			case GUI_TOGGLE:
			gui_Print(Get_String(STR_MENU_TOGGLE), fc, bc, 14, 264);
			break;
			case GUI_INTEGER:
			gui_Print(Get_String(STR_MENU_INTEGER), fc, bc, 14, 264);
			break;
			case GUI_FILE:
			gui_Print(Get_String(STR_MENU_FILE), fc, bc, 14, 264);
			break;
			case GUI_STRING:
			gui_Print(Get_String(STR_MENU_STRING), fc, bc, 14, 264);
			break;
		}
		sprintf(line, "O = %s", exit_msg);
		gui_Print(line, fc, bc, 466 - gui_PrintWidth(line), 264);

		for (i=0; i<mlen; i++)
		{
			char temp[10];

			bc = 0xFF000000;
			if ((menu[i].flags & 0x0FFFFFFF) == GUI_DIVIDER)
				continue;
			if (menu[i].enable == GUI_ENABLED)
				fc = (i==csel) ? 0xFFFFFFFF : 0xFFCCCCCC;
			else
				fc = 0xFFAAAAAA;

			strcpy(line, menu[i].text);
			switch (menu[i].flags & 0x0FFFFFFF)
			{
				case GUI_SELECT:
				strcat(line, " : ");
				strcat(line, ((struct gui_list *)menu[i].field1)[*(int *)menu[i].field2].text);
				break;
				case GUI_TOGGLE:
				strcat(line, " : ");
				strcat(line, *(int *)menu[i].field1 ? Get_String(STR_MENU_ON) : Get_String(STR_MENU_OFF));
				break;
				case GUI_INTEGER:
				strcat(line, " : ");
				sprintf(temp, "%d", *(int *)menu[i].field1);
				strcat(line, temp);
				break;
				case GUI_FILE:
				strcat(line, " : ");
				strcat(line, *(int *)menu[i].field1 ? *(char **)menu[i].field1 : "");
				break;
				case GUI_STRING:
				strcat(line, " : ");
				strcat(line, *(int *)menu[i].field1 ? *(char **)menu[i].field1 : "");
				break;
				case GUI_TEXT:
				if ((int)menu[i].field1)
					fc = (u32)menu[i].field1 | 0xFF000000;
				if ((int)menu[i].field2)
					bc = (u32)menu[i].field2 | 0xFF000000;
				break;
			}
			switch (menu[i].flags & 0xF0000000)
			{
				case GUI_LEFT:
				tx = 7;
				ty = msy + i * 16;
				break;
				case GUI_RIGHT:
				tx = 473 - gui_PrintWidth(line);
				ty = msy + i * 16;
				break;
				case GUI_CENTER:
				default:
				tx = 240 - gui_PrintWidth(line) / 2;
				ty = msy + i * 16;
			}
			gui_Print(line, fc, bc, tx, ty);
			bc = 0xFF000000;
		}
		gui_PostPrint();

		sceKernelDelayThread(20*1000);
		sceCtrlReadBufferPositive(&pad, 1);
        // Emergency quit requested? Then quit
        if (emerg_quit)
            QuitEmulator();
	}
	while (pad.Buttons & PSP_CTRL_CIRCLE)
		sceCtrlReadBufferPositive(&pad, 1);

    // clear display
	if (gui_use_intrafont)
	{
		sceGuStart(GU_DIRECT, list);

		sceGuClearColor(0xFF000000);
		sceGuClearDepth(0);
		sceGuClear(GU_COLOR_BUFFER_BIT|GU_DEPTH_BUFFER_BIT);

		sceGuFinish();
		sceGuSync(0,0);

		sceDisplayWaitVblankStart();
		sceGuSwapBuffers();

		sceGuStart(GU_DIRECT, list);

		sceGuClearColor(0xFF000000);
		sceGuClearDepth(0);
		sceGuClear(GU_COLOR_BUFFER_BIT|GU_DEPTH_BUFFER_BIT);

		sceGuFinish();
		sceGuSync(0,0);

		sceDisplayWaitVblankStart();
		sceGuSwapBuffers();
	}

	pspDebugScreenInit();
	pspDebugScreenSetBackColor(0xFF000000);
	pspDebugScreenSetTextColor(0xFFFFFFFF);
	pspDebugScreenClear();
}
