TARGET = BasiliskII

CPUOBJS = ../uae_cpu/basilisk_glue.o ../uae_cpu/memory.o ../uae_cpu/newcpu.o \
	../uae_cpu/readcpu.o cpustbl.o cpudefs.o \
	cpuemu1.o cpuemu2.o cpuemu3.o cpuemu4.o cpuemu5.o cpuemu6.o cpuemu7.o cpuemu8.o \
	 ../uae_cpu/fpu/fpu_soft.o ../uae_cpu/fpu/softfloat/softfloat.o

OBJS = ../main.o main_psp.o ../prefs.o ../prefs_items.o prefs_psp.o \
	prefs_editor_psp.o sys_psp.o ../rom_patches.o ../slot_rom.o \
	../rsrc_patches.o ../emul_op.o ../macos_util.o ../xpram.o \
	xpram_psp.o ../timer.o timer_psp.o clip_psp.o ../adb.o \
	../serial.o serial_psp.o ../ether.o ether_psp.o ../sony.o \
	../disk.o ../cdrom.o ../scsi.o scsi_psp.o ../video.o \
	video_psp.o ../audio.o audio_psp.o ../extfs.o extfs_psp.o \
	ftruncate.o ../user_strings.o user_strings_psp.o \
	intraFont.o gui_psp.o reqfile.o dvemgr/pspDveManager.o \
	danzeff/danzeff.o \
	$(CPUOBJS)

INCDIR = $(PSPDEV)/SDK/include ../include ./include . ../uae_cpu ../uae_cpu/fpu/softfloat
CFLAGS = -O3 -G8 -Wall -DPSP -DDANZEFF_SCEGU
#CFLAGS += -DFPU_UAE -ffast-math
#CFLAGS += -DFPU_IEEE
CFLAGS += -DFPU_SOFT
CFLAGS += -falign-functions=64
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti
ASFLAGS = $(CFLAGS)

BUILD_PRX = 1
PSP_FW_VERSION = 371
PSP_LARGE_MEMORY = 1

LIBDIR = $(PSPDEV)/SDK/lib
LIBS = -lstdc++ -lpng -lz -lpspirkeyb -lpsprtc -lpspwlan -lpspgum -lpspgu -lpspaudio -lm -lpsppower -lpspkubridge -lpspsystemctrl_user
LDFLAGS = -DMODULE_NAME="BasiliskII"

EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = Basilisk II
PSP_EBOOT_SND0="SND0.AT3"
PSP_EBOOT_ICON="ICON0blk.PNG"
PSP_EBOOT_PIC1="PIC1.PNG"

all: danzeff/danzeff.o cpuemu1.o cpuemu2.o cpuemu3.o cpuemu4.o cpuemu5.o cpuemu6.o cpuemu7.o cpuemu8.o $(EXTRA_TARGETS) $(FINAL_TARGET)

danzeff/danzeff.o: danzeff/danzeff.c
	$(CC) $(CFLAGS) -c $< -o $@

cpuemu1.o: cpuemu.cpp
	$(CXX) -D_REENTRANT -DPART_1 $(CXXFLAGS) -c $< -o $@
cpuemu2.o: cpuemu.cpp
	$(CXX) -D_REENTRANT -DPART_2 $(CXXFLAGS) -c $< -o $@
cpuemu3.o: cpuemu.cpp
	$(CXX) -D_REENTRANT -DPART_3 $(CXXFLAGS) -c $< -o $@
cpuemu4.o: cpuemu.cpp
	$(CXX) -D_REENTRANT -DPART_4 $(CXXFLAGS) -c $< -o $@
cpuemu5.o: cpuemu.cpp
	$(CXX) -D_REENTRANT -DPART_5 $(CXXFLAGS) -c $< -o $@
cpuemu6.o: cpuemu.cpp
	$(CXX) -D_REENTRANT -DPART_6 $(CXXFLAGS) -c $< -o $@
cpuemu7.o: cpuemu.cpp
	$(CXX) -D_REENTRANT -DPART_7 $(CXXFLAGS) -c $< -o $@
cpuemu8.o: cpuemu.cpp
	$(CXX) -D_REENTRANT -DPART_8 $(CXXFLAGS) -c $< -o $@

PSPSDK=$(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
