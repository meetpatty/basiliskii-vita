/*
 *  sysdeps.h - System dependent definitions for PSP
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

#ifndef SYSDEPS_H
#define SYSDEPS_H

//#include "config.h"
#include "user_strings_psp.h"

# include <sys/types.h>
# include <unistd.h>

#include <netinet/in.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/time.h>
#include <time.h>

#include <pspkernel.h>
#include <pspdebug.h>


/* Mac and host address space are distinct */
#define REAL_ADDRESSING 0

/* Set endian and addressing definitions */
#undef WORDS_BIGENDIAN
#define DIRECT_ADDRESSING 1

/* Using 68k emulator */
#define EMULATED_68K 1

/* The m68k emulator uses a prefetch buffer ? */
#define USE_PREFETCH_BUFFER 0

/* Mac ROM is write protected when banked memory is used */
#if REAL_ADDRESSING || DIRECT_ADDRESSING
# define ROM_IS_WRITE_PROTECTED 0
# define USE_SCRATCHMEM_SUBTERFUGE 1
#else
# define ROM_IS_WRITE_PROTECTED 1
#endif

/* ExtFS is supported */
#define SUPPORTS_EXTFS 1

/* BSD socket API supported */
#define SUPPORTS_UDP_TUNNEL 1

//#define SUPPORTS_VBL_IRQ 1

/* Data types */
typedef unsigned char uint8;
typedef signed char int8;
typedef unsigned short uint16;
typedef short int16;
typedef unsigned int uint32;
typedef int int32;
typedef unsigned long long uint64;
typedef long long int64;
#define VAL64(a) (a ## LL)
#define UVAL64(a) (a ## uLL)
typedef uint32 uintptr;
typedef int32 intptr;

typedef off_t loff_t;
typedef char * caddr_t;

//typedef struct timeval tm_time_t;
typedef uint64 tm_time_t;

/* Define codes for all the float formats that we know of.
 * Though we only handle IEEE format.  */
#define UNKNOWN_FLOAT_FORMAT 0
#define IEEE_FLOAT_FORMAT 1
#define VAX_FLOAT_FORMAT 2
#define IBM_FLOAT_FORMAT 3
#define C4X_FLOAT_FORMAT 4

/* UAE CPU data types */
#define uae_s8 int8
#define uae_u8 uint8
#define uae_s16 int16
#define uae_u16 uint16
#define uae_s32 int32
#define uae_u32 uint32
#define uae_s64 int64
#define uae_u64 uint64
typedef uae_u32 uaecptr;

/* Timing functions */
extern uint64 GetTicks_usec(void);
extern void Delay_usec(uint32 usec);

/* UAE CPU defines */

/* little-endian CPUs which can not do unaligned accesses (this needs optimization) */
//static inline uae_u32 do_get_mem_long(uae_u32 *a) {uint8 *b = (uint8 *)a; return (b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3];}
static inline uae_u32 do_get_mem_word(uae_u16 *a) {uint8 *b = (uint8 *)a; return (b[0] << 8) | b[1];}
//static inline void do_put_mem_long(uae_u32 *a, uae_u32 v) {uint8 *b = (uint8 *)a; b[0] = v >> 24; b[1] = v >> 16; b[2] = v >> 8; b[3] = v;}
static inline void do_put_mem_word(uae_u16 *a, uae_u32 v) {uint8 *b = (uint8 *)a; b[0] = v >> 8; b[1] = v;}

// these don't seem to be used
//static inline uae_u32 do_byteswap_32(uae_u32 v)
//	{ return (((v >> 24) & 0xff) | ((v >> 8) & 0xff00) | ((v & 0xff) << 24) | ((v & 0xff00) << 8)); }
static inline uae_u32 do_byteswap_16(uae_u32 v)
	{ return (((v >> 8) & 0xff) | ((v & 0xff) << 8)); }


/* PSP optimized CPU defines */
static inline uae_u32 do_get_mem_long(uae_u32 *a) {uint32 retval; __asm__ ("ulw %0,%1" : "=r" (retval) : "m" (*a)); return __builtin_allegrex_wsbw(retval);}
static inline void do_put_mem_long(uae_u32 *a, uae_u32 v) {__asm__ ("usw %1,%0" : "=m" (*a) : "r" (__builtin_allegrex_wsbw(v)));}

// doesn't seem to be used
#define HAVE_OPTIMIZED_BYTESWAP_32
static inline uae_u32 do_byteswap_32(uae_u32 v) {return __builtin_allegrex_wsbw(v);}


#define do_get_mem_byte(a) ((uae_u32)*((uae_u8 *)(a)))
#define do_put_mem_byte(a, v) (*(uae_u8 *)(a) = (v))

#define call_mem_get_func(func, addr) ((*func)(addr))
#define call_mem_put_func(func, addr, v) ((*func)(addr, v))
#define __inline__ inline
#define CPU_EMU_SIZE 0
#undef NO_INLINE_MEMORY_ACCESS
#undef MD_HAVE_MEM_1_FUNCS
#define ENUMDECL typedef enum
#define ENUMNAME(name) name
#define write_log pspDebugScreenPrintf

#undef X86_ASSEMBLY
#undef UNALIGNED_PROFITABLE
#undef OPTIMIZED_FLAGS
#define ASM_SYM_FOR_FUNC(a) __asm__(a)

#ifndef REGPARAM
# define REGPARAM
#endif
#define REGPARAM2

//#define SIZEOF_FLOAT 4
//#define SIZEOF_DOUBLE 8
//#define HOST_FLOAT_FORMAT IEEE_FLOAT_FORMAT
//#define FPU_HAVE_IEEE_DOUBLE 1

#define HOST_FLOAT_FORMAT SOFT_FLOAT_FORMAT
#define CONFIG_SOFTFLOAT

#endif /* SYSDEPS_H */
