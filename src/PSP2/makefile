TARGET = BasiliskII
TITLE_ID = BASILISK0

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
	gui_psp.o reqfile.o debugScreen.o danzeff/danzeff.o \
	psp2_touch.o \
	$(CPUOBJS)

INCLUDES = -I../include -I./include -I. -I../uae_cpu -I../uae_cpu/fpu/softfloat
LIBS = -lSceCtrl_stub -lSceTouch_stub -lSceLibKernel_stub -lSceAudio_stub -lSceNetCtl_stub \
	-lSceNet_stub -lvita2d -lSceDisplay_stub -lSceSysmodule_stub -lSceGxm_stub \
	-lScePgf_stub -lScePvf_stub -lSceHid_stub -lScePower_stub -lSceAppUtil_stub \
	-lSceCommonDialog_stub -lSceIme_stub -lfreetype -lpng -ljpeg -lz -lm -lc

PREFIX  = arm-vita-eabi
CC      = $(PREFIX)-gcc
CXX    := $(PREFIX)-g++

CFLAGS += -DFPU_SOFT -falign-functions=64 -O3
CXXFLAGS = $(CFLAGS) -std=c++11 $(INCLUDES) -L$(VITASDK)\lib -Wno-narrowing -Wwrite-strings -fpermissive
ASFLAGS = $(CFLAGS)

all: cpuemu1.o cpuemu2.o cpuemu3.o cpuemu4.o cpuemu5.o cpuemu6.o cpuemu7.o cpuemu8.o $(TARGET).vpk

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

%.vpk: eboot.bin
	vita-mksfoex -s TITLE_ID=$(TITLE_ID) "$(TARGET)" param.sfo
	vita-pack-vpk -s param.sfo -b eboot.bin \
	-a sce_sys/icon0.png=sce_sys/icon0.png \
	-a fonts/ChicagoFLF.ttf=fonts/ChicagoFLF.ttf \
	-a danzeff/graphics/keys.png=graphics/keys.png \
	-a danzeff/graphics/keys_t.png=graphics/keys_t.png \
	-a danzeff/graphics/keys_s.png=graphics/keys_s.png \
	-a danzeff/graphics/keys_c.png=graphics/keys_c.png \
	-a danzeff/graphics/keys_c_t.png=graphics/keys_c_t.png \
	-a danzeff/graphics/keys_s_c.png=graphics/keys_s_c.png \
	-a danzeff/graphics/nums.png=graphics/nums.png \
	-a danzeff/graphics/nums_t.png=graphics/nums_t.png \
	-a danzeff/graphics/nums_s.png=graphics/nums_s.png \
	-a danzeff/graphics/nums_c.png=graphics/nums_c.png \
	-a danzeff/graphics/nums_c_t.png=graphics/nums_c_t.png \
	-a danzeff/graphics/nums_s_c.png=graphics/nums_s_c.png \
	$@

eboot.bin: $(TARGET).velf
	vita-make-fself -s $< $@

%.velf: %.elf
	$(PREFIX)-strip -g $<
	vita-elf-create $< $@

$(TARGET).elf: $(OBJS)
	$(CXX) -Wl,-q -o $@ $^ $(LIBS)

clean:
	@rm -rf $(TARGET).velf $(TARGET).elf $(OBJS)
