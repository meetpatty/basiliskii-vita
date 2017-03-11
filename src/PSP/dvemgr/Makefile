TARGET = dvemgr
OBJS = main.o exports.o sceGe_driver.o

INCDIR = $(PSPDEV)/SDK/include
CFLAGS = -O2 -Os -G0 -Wall -fshort-wchar -fno-pic -mno-check-zero-division -DF_sceGe_driver_0013
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti
ASFLAGS = $(CFLAGS)

BUILD_PRX = 1
PRX_EXPORTS = exports.exp

USE_KERNEL_LIBC = 1
USE_KERNEL_LIBS = 1

LIBDIR = $(PSPDEV)/SDK/lib
LIBS = -lpspimpose_driver -lpspdve_driver -lpsphprm_driver -lpspge_driver

PSPSDK=$(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build_prx.mak
