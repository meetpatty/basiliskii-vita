#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <vita2d.h>
#include <psp2/types.h>
#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/display.h>
#include <psp2/ime_dialog.h>

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
//#define SCR_WIDTH (480)
//#define SCR_HEIGHT (272)
#define PIXEL_SIZE (4) /* change this if you change to another screenmode */
#define FRAME_SIZE (BUF_WIDTH * SCR_HEIGHT * PIXEL_SIZE)
#define ZBUF_SIZE (BUF_WIDTH SCR_HEIGHT * 2) /* zbuffer seems to be 16-bit? */
int FONT_SIZE = 26;

static unsigned int __attribute__((aligned(16))) list[4096];
vita2d_font *font;

int get_text_osk(char *input, char *intext, char *desc)
{
	return 0;
}

void gui_font_init(void)
{
	font = vita2d_load_font_file("app0:/fonts/ChicagoFLF.ttf");
}


/*
 * GUI support code
 *
 */

void gui_PrePrint(void)
{
    vita2d_start_drawing();
    vita2d_set_clear_color(0xFF7F7F7F);
	vita2d_clear_screen();
}

void gui_PostPrint(void)
{
    vita2d_end_drawing();
    vita2d_common_dialog_update();
	vita2d_swap_buffers();
}

int gui_PrintWidth(char *text)
{
	return vita2d_font_text_width(font, FONT_SIZE, text);
}

void gui_Print(char *text, uint32 fc, uint32 bc, int x, int y)
{
    vita2d_font_draw_text(font, x, y, fc, FONT_SIZE, text);
}

void gui_Print_Left(char* text, uint32 fc)
{
    int xpos = 20;
    int ypos = 20 + FONT_SIZE;
    gui_PrePrint();
    gui_Print(text, fc, 0x0, xpos, ypos);
    gui_PostPrint();
}

char *RequestString (char *initialStr)
{
	int ok, i;
	static char str[255];
    char* desc = "Enter String";

	ok = get_text_osk(str, initialStr, desc);
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
	uint32 fc, bc;
	int tx, ty;
	char line[69];

	mlen = gui_menu_len(menu);
	msy = 272 - mlen*FONT_SIZE*1.5/2;

	sceCtrlPeekBufferPositive(0, &pad, 1);
	while (!(pad.buttons & SCE_CTRL_CIRCLE))
	{
        void (*fnptr)(struct gui_menu *);
		fnptr = (void (*)(struct gui_menu *))menufn;
		if (fnptr)
			(*fnptr)(menu);
		if (pad.buttons & SCE_CTRL_CROSS)
		{
			if (menu[csel].enable == GUI_ENABLED)
				switch (menu[csel].flags & 0x0FFFFFFF)
				{
					void (*fnptr)(void *);
					char temp[256];
					char *req;

					case GUI_MENU:
					while (pad.buttons & SCE_CTRL_CROSS)
						sceCtrlPeekBufferPositive(0, &pad, 1);
					do_gui((struct gui_menu *)menu[csel].field1, menu[csel].field2, Get_String(STR_MENU_BACK));
					break;
					case GUI_FUNCTION:
					while (pad.buttons & SCE_CTRL_CROSS)
						sceCtrlPeekBufferPositive(0, &pad, 1);
					fnptr = (void (*)(void *))menu[csel].field1;
					(*fnptr)(menu[csel].field2);
					break;
					case GUI_TOGGLE:
					while (pad.buttons & SCE_CTRL_CROSS)
						sceCtrlPeekBufferPositive(0, &pad, 1);
					*(int *)menu[csel].field1 ^= 1;
					break;
					case GUI_SELECT:
					if (pad.buttons & (SCE_CTRL_RIGHT | SCE_CTRL_DOWN))
					{
						// next selection
						while (pad.buttons & (SCE_CTRL_RIGHT | SCE_CTRL_DOWN))
							sceCtrlPeekBufferPositive(0, &pad, 1);
						j = gui_list_len((struct gui_list *)menu[csel].field1);
						k = *(int *)menu[csel].field2;
						k = (k == (j - 1)) ? 0 : k + 1;
						*(int *)menu[csel].field2 = k;
					}
					if (pad.buttons & (SCE_CTRL_LEFT | SCE_CTRL_UP))
					{
						// previous selection
						while (pad.buttons & (SCE_CTRL_LEFT | SCE_CTRL_UP))
							sceCtrlPeekBufferPositive(0, &pad, 1);
						j = gui_list_len((struct gui_list *)menu[csel].field1);
						k = *(int *)menu[csel].field2;
						k = (k == 0) ? j - 1 : k - 1;
						*(int *)menu[csel].field2 = k;
					}
					break;
					case GUI_FILE:
					while (pad.buttons & SCE_CTRL_CROSS)
						sceCtrlPeekBufferPositive(0, &pad, 1);
					if (*(int *)menu[csel].field1)
						free(*(char **)menu[csel].field1);
					strcpy(temp, psp_home);
					strcat(temp, (char *)menu[csel].field2);
					strcat(temp, "/");
					req = RequestFile(temp);
					*(char **)menu[csel].field1 = req ? strdup(req) : 0;
					break;
					case GUI_STRING:
					while (pad.buttons & SCE_CTRL_CROSS)
						sceCtrlPeekBufferPositive(0, &pad, 1);
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
					if (pad.buttons & SCE_CTRL_RIGHT)
					{
						// +1
						//while (pad.buttons & SCE_CTRL_RIGHT)
						//	sceCtrlPeekBufferPositive(0, &pad, 1);
						sceKernelDelayThread(200*1000);
						*(int *)menu[csel].field1 += 1;
						if (*(int *)menu[csel].field1 > max)
							*(int *)menu[csel].field1 = max;
					}
					if (pad.buttons & SCE_CTRL_LEFT)
					{
						// -1
						//while (pad.buttons & SCE_CTRL_LEFT)
						//	sceCtrlPeekBufferPositive(0, &pad, 1);
						sceKernelDelayThread(200*1000);
						*(int *)menu[csel].field1 -= 1;
						if (*(int *)menu[csel].field1 < min)
							*(int *)menu[csel].field1 = min;
					}
					if (pad.buttons & SCE_CTRL_DOWN)
					{
						// +10
						//while (pad.buttons & SCE_CTRL_DOWN)
						//	sceCtrlPeekBufferPositive(0, &pad, 1);
						sceKernelDelayThread(200*1000);
						*(int *)menu[csel].field1 += 10;
						if (*(int *)menu[csel].field1 > max)
							*(int *)menu[csel].field1 = max;
					}
					if (pad.buttons & SCE_CTRL_UP)
					{
						// -10
						//while (pad.buttons & SCE_CTRL_UP)
						//	sceCtrlPeekBufferPositive(0, &pad, 1);
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
			if (pad.buttons & SCE_CTRL_UP)
			{
				while (pad.buttons & SCE_CTRL_UP)
					sceCtrlPeekBufferPositive(0, &pad, 1);
				csel = (csel == 0) ? mlen - 1 : csel - 1;
			}
			if (pad.buttons & SCE_CTRL_DOWN)
			{
				while (pad.buttons & SCE_CTRL_DOWN)
					sceCtrlPeekBufferPositive(0, &pad, 1);
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
			gui_Print(Get_String(STR_MENU_MENU), fc, bc, 14, 530);
			break;
			case GUI_FUNCTION:
			gui_Print(Get_String(STR_MENU_FUNCTION), fc, bc, 14, 530);
			break;
			case GUI_SELECT:
			gui_Print(Get_String(STR_MENU_SELECT), fc, bc, 14, 530);
			break;
			case GUI_TOGGLE:
			gui_Print(Get_String(STR_MENU_TOGGLE), fc, bc, 14, 530);
			break;
			case GUI_INTEGER:
			gui_Print(Get_String(STR_MENU_INTEGER), fc, bc, 14, 530);
			break;
			case GUI_FILE:
			gui_Print(Get_String(STR_MENU_FILE), fc, bc, 14, 530);
			break;
			case GUI_STRING:
			gui_Print(Get_String(STR_MENU_STRING), fc, bc, 14, 530);
			break;
		}
		sprintf(line, "O = %s", exit_msg);
		gui_Print(line, fc, bc, 946 - gui_PrintWidth(line), 530);

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
					fc = (uint32)menu[i].field1 | 0xFF000000;
				if ((int)menu[i].field2)
					bc = (uint32)menu[i].field2 | 0xFF000000;
				break;
			}
			switch (menu[i].flags & 0xF0000000)
			{
				case GUI_LEFT:
				tx = 14;
				ty = msy + i * FONT_SIZE * 1.5;
				break;
				case GUI_RIGHT:
				tx = 936 - gui_PrintWidth(line);
				ty = msy + i * FONT_SIZE * 1.5;
				break;
				case GUI_CENTER:
				default:
				tx = 480 - gui_PrintWidth(line) / 2;
				ty = msy + i * FONT_SIZE * 1.5;
			}
			gui_Print(line, fc, bc, tx, ty);
			bc = 0xFF000000;
		}
		gui_PostPrint();

		sceKernelDelayThread(20*1000);
		sceCtrlPeekBufferPositive(0, &pad, 1);
        // Emergency quit requested? Then quit
        if (emerg_quit)
            QuitEmulator();
	}
	while (pad.buttons & SCE_CTRL_CIRCLE)
		sceCtrlPeekBufferPositive(0, &pad, 1);

    // clear display
    vita2d_set_clear_color(0xFF000000);
    vita2d_start_drawing();
	vita2d_clear_screen();
	vita2d_end_drawing();
	vita2d_swap_buffers();
}
