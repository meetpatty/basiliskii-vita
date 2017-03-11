#ifndef GUI_PSP_H
#define GUI_PSP_H

struct gui_menu {
	char *text;
	unsigned int flags;
	void *field1;
	void *field2;
	unsigned int enable;
};

struct gui_list {
	char *text;
	int index;
};

// gui_menu.flags
#define GUI_DIVIDER   0
#define GUI_TEXT      1
#define GUI_MENU      2
#define GUI_FUNCTION  3
#define GUI_SELECT    4
#define GUI_TOGGLE    5
#define GUI_INTEGER   6
#define GUI_FILE      7
#define GUI_STRING    8

#define GUI_CENTER 0x10000000
#define GUI_LEFT   0x20000000
#define GUI_RIGHT  0x30000000

// gui_menu.enable
#define GUI_DISABLED 0
#define GUI_ENABLED  1
#define GUI_SET_ME   0xFFFFFFFF

// misc
#define GUI_END_OF_MENU 0xFFFFFFFF
#define GUI_END_OF_LIST 0xFFFFFFFF

#endif
