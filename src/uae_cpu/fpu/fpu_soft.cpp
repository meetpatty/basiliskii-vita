/*
 * UAE - The Un*x Amiga Emulator
 *
 * MC68881/MC68040 emulation
 *
 * Copyright 1996 Herman ten Brugge
 *
 *
 * Following fixes by Lauri Pesonen, July 1999:
 *
 * FMOVEM list handling:
 *  The lookup tables did not work correctly, rewritten.
 * FINT:
 *  (int) cast does not work, fixed.
 *  Further, now honors the FPU fpcr rounding modes.
 * FINTRZ:
 *  (int) cast cannot be used, fixed.
 * FGETEXP:
 *  Input argument value 0 returned erroneous value.
 * FMOD:
 *  (int) cast cannot be used. Replaced by proper rounding.
 *  Quotient byte handling was missing.
 * FREM:
 *  (int) cast cannot be used. Replaced by proper rounding.
 *  Quotient byte handling was missing.
 * FSCALE:
 *  Input argument value 0 was not handled correctly.
 * FMOVEM Control Registers to/from address FPU registers An:
 *  A bug caused the code never been called.
 * FMOVEM Control Registers pre-decrement:
 *  Moving of control regs from memory to FPP was not handled properly,
 *  if not all of the three FPU registers were moved.
 * Condition code "Not Greater Than or Equal":
 *  Returned erroneous value.
 * FSINCOS:
 *  Cosine must be loaded first if same register.
 * FMOVECR:
 *  Status register was not updated (yes, this affects it).
 * FMOVE <ea> -> reg:
 *  Status register was not updated (yes, this affects it).
 * FMOVE reg -> reg:
 *  Status register was not updated.
 * FDBcc:
 *  The loop termination condition was wrong.
 *  Possible leak from int16 to int32 fixed.
 * get_fp_value:
 *  Immediate addressing mode && Operation Length == Byte ->
 *  Use the low-order byte of the extension word.
 * Now FPU fpcr high 16 bits are always read as zeroes, no matter what was
 * written to them.
 *
 * Other:
 * - Optimized single/double/extended to/from conversion functions.
 *   Huge speed boost, but not (necessarily) portable to other systems.
 *   Enabled/disabled by #define FPU_HAVE_IEEE_DOUBLE 1
 * - Optimized versions of FSCALE, FGETEXP, FGETMAN
 * - Conversion routines now handle NaN and infinity better.
 * - Some constants precalculated. Not all compilers can optimize the
 *   expressions previously used.
 *
 * TODO:
 * - Floating point exceptions.
 * - More Infinity/NaN/overflow/underflow checking.
 * - FPU instruction_address (only needed when exceptions are implemented)
 * - Should be written in assembly to support long doubles.
 * - Precision rounding single/double
 */

#include "sysdeps.h"
#include <stdio.h>
#include <math.h>
#include <errno.h>
#include "memory.h"
#include "readcpu.h"
#include "newcpu.h"
#include "main.h"

#define FPU_IMPLEMENTATION

#include "fpu/softfloat/softfloat.h"
#include "fpu/softfloat/softfloat-macros.h"

#include "fpu/fpu.h"
#include "fpu/fpu_soft.h"

/* Global FPU context */
fpu_t fpu;


/* -------------------------------------------------------------------------- */
/* --- Scopes Definition                                                  --- */
/* -------------------------------------------------------------------------- */

#undef	PUBLIC
#define PUBLIC	/**/

#undef	PRIVATE
#define PRIVATE	static

#undef	FFPU
#define FFPU	/**/

#undef	FPU
#define	FPU		fpu.


#include "fpu/impl.h"


PRIVATE float_status fp_status;
PRIVATE int current_exception_flags;
PRIVATE int accrued_exception_flags;
#define FPS , &fp_status

#define CHKERR(d) ({	\
	double ret = (d);	\
	if (errno == EDOM)	\
		set_float_exception_flags(get_float_exception_flags(&fp_status) | float_flag_invalid, &fp_status);		\
	if (errno == ERANGE && ret == 0.0) \
		set_float_exception_flags(get_float_exception_flags(&fp_status) | float_flag_underflow, &fp_status);	\
	else if (errno == ERANGE) \
		set_float_exception_flags(get_float_exception_flags(&fp_status) | float_flag_overflow, &fp_status);		\
	ret; })


/* -------------------------------------------------------------------------- */
/* --- Constant ROM table                                                 --- */
/* -------------------------------------------------------------------------- */

uint32 constantRom[64*3] = {
	0x2168C235,0xC90FDAA2,0x4000,	// 0: Pi
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,	// 1-5
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,	// 6-A
	0xFBCFF798,0x9A209A84,0x3FFD,	// B: Log10(2)
	0xA2BB4A9A,0xADF85458,0x4000,	// C: e
	0x5C17F0BC,0xB8AA3B29,0x3FFF,	// D: Log2(e)
	0x37287195,0xDE5BD8A9,0x3FFD,	// E: Log10(e)
	0,0,0,							// F: 0.0
	0,0,0,0,0,0,0,0,0,0,0,0,		// 10-13
	0,0,0,0,0,0,0,0,0,0,0,0,		// 14-17
	0,0,0,0,0,0,0,0,0,0,0,0,		// 18-1B
	0,0,0,0,0,0,0,0,0,0,0,0,		// 1C-1F
	0,0,0,0,0,0,0,0,0,0,0,0,		// 20-23
	0,0,0,0,0,0,0,0,0,0,0,0,		// 24-27
	0,0,0,0,0,0,0,0,0,0,0,0,		// 28-2B
	0,0,0,0,0,0,0,0,0,0,0,0,		// 2C-2F
	0xD1CF79AC,0xB17217F7,0x3FFE,	// 30: ln(2)
	0xAAA8AC17,0x935D8DDD,0x4000,	// 31: ln(10)
	0,0x80000000,0x3FFF,			// 32: 1.0
	0,0xA0000000,0x4002,			// 33: 10.0
	0,0xC8000000,0x4005,			// 34: 100.0
	0,0x9C400000,0x400C,			// 35: 10^4
	0,0xBEBC2000,0x4019,			// 36: 10^8
	0x04000000,0x8E1BC9BF,0x4034,	// 37: 10^16
	0x2B70B59E,0x9DC5ADA8,0x4069,	// 38: 10^32
	0xFFCFA6D5,0xC2781F49,0x40D3,	// 39: 10^64
	0x80E98CE0,0x93BA47C9,0x41A8,	// 3A: 10^128
	0x9DF9DE8E,0xAA7EEBFB,0x4351,	// 3B: 10^256
	0xA60E91C7,0xE319A0AE,0x46A3,	// 3C: 10^512
	0x81750C17,0xC9767586,0x4D48,	// 3D: 10^1024
	0xC53D5DE5,0x9E8B3B5D,0x5A92,	// 3E: 10^2048
	0x8A20979B,0xC4605202,0x7525	// 3F: 10^4096
};

/*----------------------------------------------------------------------------*/

#define floatx80_default_nan_high 0xFFFF
#define floatx80_default_nan_low  LIT64( 0xFFFFFFFFFFFFFFFF )

/* -------------------------------------------------------------------------- */
/* --- Debugging                                                          --- */
/* -------------------------------------------------------------------------- */

PUBLIC void FFPU fpu_dump_registers(void)
{
}

PUBLIC void FFPU fpu_dump_flags(void)
{
}

PRIVATE void FFPU dump_registers(const char * str)
{
}

PRIVATE void FFPU dump_first_bytes(uae_u8 * buffer, uae_s32 actual)
{
}

/* -------------------------------------------------------------------------- */
/* --- Support                                                            --- */
/* -------------------------------------------------------------------------- */

/* Set host control word for rounding mode and rounding precision */
PRIVATE inline void set_host_control_word(void)
{
	/*
		Exception enable byte is ignored, but the same value is returned
		that was previously set.
	*/
	switch (FPU fpcr.rounding_mode & FPCR_ROUNDING_MODE)
	{
		case FPCR_ROUND_NEAR:
		set_float_rounding_mode(float_round_nearest_even FPS);
		break;
		case FPCR_ROUND_ZERO:
		set_float_rounding_mode(float_round_to_zero FPS);
		break;
		case FPCR_ROUND_MINF:
		set_float_rounding_mode(float_round_down FPS);
		break;
		case FPCR_ROUND_PINF:
		set_float_rounding_mode(float_round_up FPS);
		break;
	}
	switch (FPU fpcr.rounding_precision & FPCR_ROUNDING_PRECISION)
	{
		case FPCR_PRECISION_EXTENDED:
		set_floatx80_rounding_precision(80 FPS);
		break;
		case FPCR_PRECISION_SINGLE:
		set_floatx80_rounding_precision(32 FPS);
		break;
		case FPCR_PRECISION_DOUBLE:
		set_floatx80_rounding_precision(64 FPS);
		break;
		default:
		set_floatx80_rounding_precision(80 FPS);
		break;
	}
}

/* Return the current rounding mode in m68k format */
static inline uae_u32 FFPU get_rounding_mode(void)
	{ return FPU fpcr.rounding_mode; }

/* Convert and set to native rounding mode */
static inline void FFPU set_rounding_mode(uae_u32 new_rounding_mode)
	{ FPU fpcr.rounding_mode = new_rounding_mode; }

/* Return the current rounding precision in m68k format */
static inline uae_u32 FFPU get_rounding_precision(void)
	{ return FPU fpcr.rounding_precision; }

/* Convert and set to native rounding precision */
static inline void FFPU set_rounding_precision(uae_u32 new_rounding_precision)
	{ FPU fpcr.rounding_precision = new_rounding_precision; }

/* Native to m68k floating-point condition codes - SELF */
PRIVATE inline uae_u32 FFPU get_fpccr(void)
{
	uae_u32 fpccr = 0;
	if ((FPU result.high & 0x7fff) == 0x7fff && FPU result.low != 0)
		fpccr |= FPSR_CCB_NAN;
	else
	{
		if ((FPU result.high & 0x7fff) == 0 && FPU result.low == 0)
			fpccr |= FPSR_CCB_ZERO;
		else if ((FPU result.high & 0x7fff) == 0x7fff && FPU result.low == 0)
			fpccr |= FPSR_CCB_INFINITY;
		if (FPU result.high & 0x8000)
			fpccr |= FPSR_CCB_NEGATIVE;
	}
	return fpccr;
}

/* M68k to native floating-point condition codes - SELF */
PRIVATE inline void FFPU set_fpccr(uae_u32 new_fpcond)
{
#if 1
	if (new_fpcond & FPSR_CCB_NAN)
	{
		FPU result.high = floatx80_default_nan_high;
		FPU result.low = floatx80_default_nan_low;
	}
	else
	{
		if (new_fpcond & FPSR_CCB_ZERO)
			FPU result = packFloatx80(0, 0, 0);
		else if (new_fpcond & FPSR_CCB_INFINITY)
			FPU result = packFloatx80(0, 0x7FFF, 0);
		else
			FPU result = int32_to_floatx80(1 FPS);

		if (new_fpcond & FPSR_CCB_NEGATIVE)
			floatx80_chs(FPU result);
	}
#endif
}

/* Return m68k floating-point exception status */
PRIVATE inline uae_u32 FFPU get_exception_status(void)
{
	int exs = current_exception_flags & float_flag_invalid ? FPSR_EXCEPTION_OPERR : 0;
	exs |= current_exception_flags & float_flag_divbyzero ? FPSR_EXCEPTION_DZ : 0;
	exs |= current_exception_flags & float_flag_overflow ? FPSR_EXCEPTION_OVFL : 0;
	exs |= current_exception_flags & float_flag_underflow ? FPSR_EXCEPTION_UNFL : 0;
	exs |= current_exception_flags & float_flag_inexact ? FPSR_EXCEPTION_INEX2 : 0;

	return (uae_u32)exs;
}

/* Set new exception status. Assumes mask against FPSR_EXCEPTION to be already performed */
PRIVATE inline void FFPU set_exception_status(uae_u32 new_status)
{
#if 1
	if (new_status == 0)
	{
		// quick clear
		current_exception_flags = 0;
		set_float_exception_flags(0, &fp_status);
		return;
	}

	int exs = new_status & FPSR_EXCEPTION_OPERR ? float_flag_invalid : 0;
	exs |= new_status & FPSR_EXCEPTION_DZ ? float_flag_divbyzero : 0;
	exs |= new_status & FPSR_EXCEPTION_OVFL ? float_flag_overflow : 0;
	exs |= new_status & FPSR_EXCEPTION_UNFL ? float_flag_underflow : 0;
	exs |= new_status & FPSR_EXCEPTION_INEX2 ? float_flag_inexact : 0;

	current_exception_flags = exs;
	set_float_exception_flags(0, &fp_status);
#else
	current_exception_flags = 0;
	set_float_exception_flags(0, &fp_status);
#endif
}

/* Return m68k accrued exception byte */
PRIVATE inline uae_u32 FFPU get_accrued_exception(void)
{
	int aex = accrued_exception_flags & float_flag_invalid ? FPSR_ACCR_IOP : 0;
	aex |= accrued_exception_flags & float_flag_divbyzero ? FPSR_ACCR_DZ : 0;
	aex |= accrued_exception_flags & float_flag_overflow ? FPSR_ACCR_OVFL : 0;
	aex |= accrued_exception_flags & float_flag_underflow ? FPSR_ACCR_UNFL : 0;
	aex |= accrued_exception_flags & float_flag_inexact ? FPSR_ACCR_INEX : 0;

	return (uae_u32)aex;
}

/* Set new accrued exception byte */
PRIVATE inline void FFPU set_accrued_exception(uae_u32 new_status)
{
#if 1
	if (new_status == 0)
	{
		// quick clear
		accrued_exception_flags = 0;
		set_float_exception_flags(0, &fp_status);
		return;
	}

	int aex = new_status & FPSR_ACCR_IOP ? float_flag_invalid : 0;
	aex |= new_status & FPSR_ACCR_DZ ? float_flag_divbyzero : 0;
	aex |= new_status & FPSR_ACCR_OVFL ? float_flag_overflow : 0;
	aex |= new_status & FPSR_ACCR_UNFL ? float_flag_underflow : 0;
	aex |= new_status & FPSR_ACCR_INEX ? float_flag_inexact : 0;

	accrued_exception_flags = aex;
	set_float_exception_flags(0, &fp_status);
#else
	accrued_exception_flags = 0;
	set_float_exception_flags(0, &fp_status);
#endif
}


/* Make FPSR according to the value passed in argument */
PRIVATE inline void FFPU make_fpsr(fpu_register const &r)
{
	FPU result = r; // used for ccr flags
	current_exception_flags = get_float_exception_flags(&fp_status);
	accrued_exception_flags |= current_exception_flags;
	set_float_exception_flags(0, &fp_status);
}


// Quotient Byte is loaded with the sign and least significant
// seven bits of the quotient.
PRIVATE inline void FFPU make_quotient(fpu_register const &quotient, uae_u32 sign)
{
	uae_u32 lsb = floatx80_to_int64(floatx80_abs(quotient) FPS) & 0x7f;
	FPU fpsr.quotient = sign | (lsb << 16);
	set_float_exception_flags(0, &fp_status);
}

// to_single
PRIVATE inline fpu_register FFPU make_single(uae_u32 value)
{
	return float32_to_floatx80((float32)value FPS);
}

// from_single
PRIVATE inline uae_u32 FFPU extract_single(fpu_register const &src)
{
	return (uae_u32)floatx80_to_float32(src FPS);
}

// to_exten
PRIVATE inline fpu_register FFPU make_extended(uae_u32 wrd1, uae_u32 wrd2, uae_u32 wrd3)
{
	// is it zero?
	if ((wrd1 & 0x7fff0000) == 0 && wrd2 == 0 && wrd3 == 0)
		return packFloatx80(0, 0, 0);
	// is it NaN?
	if ((wrd1 & 0x7fff0000) == 0x7fff0000 && (wrd2 != 0 || wrd3 != 0))
		return packFloatx80( floatx80_default_nan_high >> 15, floatx80_default_nan_high & 0x7fff, floatx80_default_nan_low);

	// normalize
    int8 shiftCount;
	flag zSign = (wrd1 >> 16) >> 15;
	int32 zExp = (wrd1 >> 16) & 0x7fff;
	bits64 zSig0 = ((uint64_t)wrd2 << 32) | wrd3;
	//bits64 zSig1 = LIT64( 0 );

    shiftCount = countLeadingZeros64( zSig0 );
    if (shiftCount)
    {
	    zSig0 = zSig0 << shiftCount; //shortShift128Left( zSig0, zSig1, shiftCount, &zSig0, &zSig1 );
    	zExp -= shiftCount;
	}
	return packFloatx80( zSign, zExp, zSig0 );
}

// make_extended_no_normalize
PRIVATE inline void FFPU make_extended_no_normalize(
	uae_u32 wrd1, uae_u32 wrd2, uae_u32 wrd3, fpu_register &result
)
{
	// is it zero?
	if ((wrd1 && 0x7fff0000) == 0 && wrd2 == 0 && wrd3 == 0) {
		result = packFloatx80(0, 0, 0);
		return;
	}
	// is it NaN?
	if ((wrd1 & 0x7fff0000) == 0x7fff0000 && (wrd2 != 0 || wrd3 != 0)) {
		result.high = floatx80_default_nan_high;
		result.low = floatx80_default_nan_low;
		return;
	}

    result.low = ((uint64_t)wrd2 << 32) | wrd3;
    result.high = (wrd1 >> 16) & 0xffff;
}

// from_exten
PRIVATE inline void FFPU extract_extended(fpu_register const &src,
	uae_u32 *wrd1, uae_u32 *wrd2, uae_u32 *wrd3
)
{
	if ((src.high & 0x7fff) == 0 && src.low == 0) {
		*wrd1 = *wrd2 = *wrd3 = 0;
		return;
	}
	*wrd3 = (uae_u32)(src.low & 0xffffffffLL);
	*wrd2 = (uae_u32)(src.low >> 32);
	*wrd1 = (uae_u32)src.high << 16;
}

// to_double
PRIVATE inline fpu_register FFPU make_double(uae_u32 wrd1, uae_u32 wrd2)
{
	union {
		fpu_double value;
		uae_u32    parts[2];
	} dest;
#ifdef WORDS_BIGENDIAN
	dest.parts[0] = wrd1;
	dest.parts[1] = wrd2;
#else
	dest.parts[0] = wrd2;
	dest.parts[1] = wrd1;
#endif
	return float64_to_floatx80(dest.value FPS);
}

// from_double
PRIVATE inline void FFPU extract_double(fpu_register const &src,
	uae_u32 *wrd1, uae_u32 *wrd2
)
{
	union {
		fpu_double value;
		uae_u32    parts[2];
	} dest;
	dest.value = floatx80_to_float64(src FPS);
#ifdef WORDS_BIGENDIAN
	*wrd1 = dest.parts[0];
	*wrd2 = dest.parts[1];
#else
	*wrd2 = dest.parts[0];
	*wrd1 = dest.parts[1];
#endif
}

// to_pack
PRIVATE inline fpu_register FFPU make_packed(uae_u32 wrd1, uae_u32 wrd2, uae_u32 wrd3)
{
	fpu_double d;
	char *cp;
	char str[100];

	cp = str;
	if (wrd1 & 0x80000000)
		*cp++ = '-';
	*cp++ = (char)((wrd1 & 0xf) + '0');
	*cp++ = '.';
	*cp++ = (char)(((wrd2 >> 28) & 0xf) + '0');
	*cp++ = (char)(((wrd2 >> 24) & 0xf) + '0');
	*cp++ = (char)(((wrd2 >> 20) & 0xf) + '0');
	*cp++ = (char)(((wrd2 >> 16) & 0xf) + '0');
	*cp++ = (char)(((wrd2 >> 12) & 0xf) + '0');
	*cp++ = (char)(((wrd2 >> 8) & 0xf) + '0');
	*cp++ = (char)(((wrd2 >> 4) & 0xf) + '0');
	*cp++ = (char)(((wrd2 >> 0) & 0xf) + '0');
	*cp++ = (char)(((wrd3 >> 28) & 0xf) + '0');
	*cp++ = (char)(((wrd3 >> 24) & 0xf) + '0');
	*cp++ = (char)(((wrd3 >> 20) & 0xf) + '0');
	*cp++ = (char)(((wrd3 >> 16) & 0xf) + '0');
	*cp++ = (char)(((wrd3 >> 12) & 0xf) + '0');
	*cp++ = (char)(((wrd3 >> 8) & 0xf) + '0');
	*cp++ = (char)(((wrd3 >> 4) & 0xf) + '0');
	*cp++ = (char)(((wrd3 >> 0) & 0xf) + '0');
	*cp++ = 'E';
	if (wrd1 & 0x40000000)
		*cp++ = '-';
	*cp++ = (char)(((wrd1 >> 24) & 0xf) + '0');
	*cp++ = (char)(((wrd1 >> 20) & 0xf) + '0');
	*cp++ = (char)(((wrd1 >> 16) & 0xf) + '0');
	*cp = 0;
	sscanf(str, "%le", &d);

	return float64_to_floatx80(d FPS);
}

// from_pack
PRIVATE inline void FFPU extract_packed(fpu_register const &src, uae_u32 *wrd1, uae_u32 *wrd2, uae_u32 *wrd3)
{
	int i;
	int t;
	char *cp;
	char str[100];
	fpu_double d = floatx80_to_float64(src FPS);

	sprintf(str, "%.16e", d);

	cp = str;
	*wrd1 = *wrd2 = *wrd3 = 0;
	if (*cp == '-') {
		cp++;
		*wrd1 = 0x80000000;
	}
	if (*cp == '+')
		cp++;
	*wrd1 |= (*cp++ - '0');
	if (*cp == '.')
		cp++;
	for (i = 0; i < 8; i++) {
		*wrd2 <<= 4;
		if (*cp >= '0' && *cp <= '9')
		*wrd2 |= *cp++ - '0';
	}
	for (i = 0; i < 8; i++) {
		*wrd3 <<= 4;
		if (*cp >= '0' && *cp <= '9')
		*wrd3 |= *cp++ - '0';
	}
	if (*cp == 'e' || *cp == 'E') {
		cp++;
		if (*cp == '-') {
			cp++;
			*wrd1 |= 0x40000000;
		}
		if (*cp == '+')
			cp++;
		t = 0;
		for (i = 0; i < 3; i++) {
			if (*cp >= '0' && *cp <= '9')
				t = (t << 4) | (*cp++ - '0');
		}
		*wrd1 |= t << 16;
	}
}

/* -------------------------------------------------------------------------- */
/* --- FP OP functions                                                    --- */
/* -------------------------------------------------------------------------- */

PRIVATE inline int FFPU get_fp_value (uae_u32 opcode, uae_u16 extra, fpu_register &src)
{
	uaecptr tmppc;
	uae_u16 tmp;
	int size;
	int mode;
	int reg;
	uae_u32 ad = 0;
	static int sz1[8] = {4, 4, 12, 12, 2, 8, 1, 0};
	static int sz2[8] = {4, 4, 12, 12, 2, 8, 2, 0};

	if ((extra & 0x4000) == 0) {
		src = FPU registers[(extra >> 10) & 7];
		return 1;
	}
	mode = (opcode >> 3) & 7;
	reg = opcode & 7;
	size = (extra >> 10) & 7;

	switch (mode) {
	case 0:
		switch (size) {
		case 6:
			src = int32_to_floatx80((uae_s32) (uae_s8) m68k_dreg (regs, reg) FPS);
			break;
		case 4:
			src = int32_to_floatx80((uae_s32) (uae_s16) m68k_dreg (regs, reg) FPS);
			break;
		case 0:
			src = int32_to_floatx80((uae_s32) m68k_dreg (regs, reg) FPS);
			break;
		case 1:
			src = make_single(m68k_dreg (regs, reg));
			break;
		default:
			return 0;
		}
		return 1;
	case 1:
		return 0;
	case 2:
		ad = m68k_areg (regs, reg);
		break;
	case 3:
		ad = m68k_areg (regs, reg);
		m68k_areg (regs, reg) += reg == 7 ? sz2[size] : sz1[size];
		break;
	case 4:
		m68k_areg (regs, reg) -= reg == 7 ? sz2[size] : sz1[size];
		ad = m68k_areg (regs, reg);
		break;
	case 5:
		ad = m68k_areg (regs, reg) + (uae_s32) (uae_s16) next_iword();
		break;
	case 6:
		ad = get_disp_ea_020 (m68k_areg (regs, reg), next_iword());
		break;
	case 7:
		switch (reg) {
		case 0:
			ad = (uae_s32) (uae_s16) next_iword();
			break;
		case 1:
			ad = next_ilong();
			break;
		case 2:
			ad = m68k_getpc ();
			ad += (uae_s32) (uae_s16) next_iword();
			break;
		case 3:
			tmppc = m68k_getpc ();
			tmp = (uae_u16)next_iword();
			ad = get_disp_ea_020 (tmppc, tmp);
			break;
		case 4:
			ad = m68k_getpc ();
			m68k_setpc (ad + sz2[size]);
			// Immediate addressing mode && Operation Length == Byte ->
			// Use the low-order byte of the extension word.
			if(size == 6) ad++;
				break;
		default:
			return 0;
		}
	}

	switch (size) {
	case 0:
		src = int32_to_floatx80((uae_s32) get_long (ad) FPS);
		break;
	case 1:
		src = make_single(get_long (ad));
		break;
	case 2: {
		uae_u32 wrd1, wrd2, wrd3;
		wrd1 = get_long (ad);
		ad += 4;
		wrd2 = get_long (ad);
		ad += 4;
		wrd3 = get_long (ad);
		src = make_extended(wrd1, wrd2, wrd3);
		break;
	}
	case 3: {
		uae_u32 wrd1, wrd2, wrd3;
		wrd1 = get_long (ad);
		ad += 4;
		wrd2 = get_long (ad);
		ad += 4;
		wrd3 = get_long (ad);
		src = make_packed(wrd1, wrd2, wrd3);
		break;
	}
	case 4:
		src = int32_to_floatx80((uae_s32) (uae_s16) get_word(ad) FPS);
		break;
	case 5: {
		uae_u32 wrd1, wrd2;
		wrd1 = get_long (ad);
		ad += 4;
		wrd2 = get_long (ad);
		src = make_double(wrd1, wrd2);
		break;
	}
	case 6:
		src = int32_to_floatx80((uae_s32) (uae_s8) get_byte(ad) FPS);
		break;
	default:
		return 0;
	}

	return 1;
}

PRIVATE inline int FFPU put_fp_value (uae_u32 opcode, uae_u16 extra, fpu_register const &value)
{
	uae_u16 tmp;
	uaecptr tmppc;
	int size;
	int mode;
	int reg;
	uae_u32 ad;
	static int sz1[8] = {4, 4, 12, 12, 2, 8, 1, 0};
	static int sz2[8] = {4, 4, 12, 12, 2, 8, 2, 0};

	if ((extra & 0x4000) == 0) {
		int dest_reg = (extra >> 10) & 7;
		FPU registers[dest_reg] = value;
		make_fpsr(FPU registers[dest_reg]);
		return 1;
	}
	mode = (opcode >> 3) & 7;
	reg = opcode & 7;
	size = (extra >> 10) & 7;
	ad = 0xffffffff;
	switch (mode) {
	case 0:
		switch (size) {
		case 6:
			m68k_dreg (regs, reg) = ((floatx80_to_int32(value FPS) & 0xff)
									 | (m68k_dreg (regs, reg) & ~0xff));
			break;
		case 4:
			m68k_dreg (regs, reg) = ((floatx80_to_int32(value FPS) & 0xffff)
									 | (m68k_dreg (regs, reg) & ~0xffff));
			break;
		case 0:
			m68k_dreg (regs, reg) = floatx80_to_int32(value FPS);
			break;
		case 1:
			m68k_dreg (regs, reg) = extract_single(value);
			break;
		default:
			return 0;
		}
		return 1;
	case 1:
		return 0;
	case 2:
		ad = m68k_areg (regs, reg);
		break;
	case 3:
		ad = m68k_areg (regs, reg);
		m68k_areg (regs, reg) += reg == 7 ? sz2[size] : sz1[size];
		break;
	case 4:
		m68k_areg (regs, reg) -= reg == 7 ? sz2[size] : sz1[size];
		ad = m68k_areg (regs, reg);
		break;
	case 5:
		ad = m68k_areg (regs, reg) + (uae_s32) (uae_s16) next_iword();
		break;
	case 6:
		ad = get_disp_ea_020 (m68k_areg (regs, reg), next_iword());
		break;
	case 7:
		switch (reg) {
		case 0:
			ad = (uae_s32) (uae_s16) next_iword();
			break;
		case 1:
			ad = next_ilong();
			break;
		case 2:
			ad = m68k_getpc ();
			ad += (uae_s32) (uae_s16) next_iword();
			break;
		case 3:
			tmppc = m68k_getpc ();
			tmp = (uae_u16)next_iword();
			ad = get_disp_ea_020 (tmppc, tmp);
			break;
		case 4:
			ad = m68k_getpc ();
			m68k_setpc (ad + sz2[size]);
			break;
		default:
			return 0;
		}
	}

	switch (size) {
	case 0:
		put_long (ad, floatx80_to_int32(value FPS));
		break;
	case 1:
		put_long (ad, extract_single(value));
		break;
	case 2: {
		uae_u32 wrd1, wrd2, wrd3;
		extract_extended(value, &wrd1, &wrd2, &wrd3);
		put_long (ad, wrd1);
		ad += 4;
		put_long (ad, wrd2);
		ad += 4;
		put_long (ad, wrd3);
		break;
	}
	case 3: {
		uae_u32 wrd1, wrd2, wrd3;
		extract_packed(value, &wrd1, &wrd2, &wrd3);
		put_long (ad, wrd1);
		ad += 4;
		put_long (ad, wrd2);
		ad += 4;
		put_long (ad, wrd3);
		break;
	}
	case 4:
		put_word(ad, (uae_s16) floatx80_to_int32(value FPS));
		break;
	case 5: {
		uae_u32 wrd1, wrd2;
		extract_double(value, &wrd1, &wrd2);
		put_long (ad, wrd1);
		ad += 4;
		put_long (ad, wrd2);
		break;
	}
	case 6:
		put_byte(ad, (uae_s8) floatx80_to_int32(value FPS));
		break;
	default:
		return 0;
	}
	return 1;
}

PRIVATE inline int FFPU get_fp_ad(uae_u32 opcode, uae_u32 *ad)
{
	uae_u16 tmp;
	uaecptr tmppc;
	int mode;
	int reg;

	mode = (opcode >> 3) & 7;
	reg = opcode & 7;
	switch (mode) {
	case 0:
	case 1:
		return 0;
	case 2:
		*ad = m68k_areg (regs, reg);
		break;
	case 3:
		*ad = m68k_areg (regs, reg);
		break;
	case 4:
		*ad = m68k_areg (regs, reg);
		break;
	case 5:
		*ad = m68k_areg (regs, reg) + (uae_s32) (uae_s16) next_iword();
		break;
	case 6:
		*ad = get_disp_ea_020 (m68k_areg (regs, reg), next_iword());
		break;
	case 7:
		switch (reg) {
		case 0:
			*ad = (uae_s32) (uae_s16) next_iword();
			break;
		case 1:
			*ad = next_ilong();
			break;
		case 2:
			*ad = m68k_getpc ();
			*ad += (uae_s32) (uae_s16) next_iword();
			break;
		case 3:
			tmppc = m68k_getpc ();
			tmp = (uae_u16)next_iword();
			*ad = get_disp_ea_020 (tmppc, tmp);
			break;
		default:
			return 0;
		}
	}
	return 1;
}

#if FPU_DEBUG
# define CONDRET(s,x) fpu_debug(("fpp_cond %s = %d\n",s,(uint32)(x))); return (x)
#else
# define CONDRET(s,x) return (x)
#endif

PRIVATE inline int FFPU fpp_cond(int condition)
{
	int N	= ((FPU result.high & 0x8000) == 0x8000);
	int Z	= ((FPU result.high & 0x7fff) == 0 && FPU result.low == 0);
	int NaN	= ((FPU result.high & 0x7fff) == 0x7fff && FPU result.low != 0);

	if (NaN)
		N = Z = 0;

	switch (condition) {
	case 0x00:	CONDRET("False",0);
	case 0x01:	CONDRET("Equal",Z);
	case 0x02:	CONDRET("Ordered Greater Than",!(NaN || Z || N));
	case 0x03:	CONDRET("Ordered Greater Than or Equal",Z || !(NaN || N));
	case 0x04:	CONDRET("Ordered Less Than",N && !(NaN || Z));
	case 0x05:	CONDRET("Ordered Less Than or Equal",Z || (N && !NaN));
	case 0x06:	CONDRET("Ordered Greater or Less Than",!(NaN || Z));
	case 0x07:	CONDRET("Ordered",!NaN);
	case 0x08:	CONDRET("Unordered",NaN);
	case 0x09:	CONDRET("Unordered or Equal",NaN || Z);
	case 0x0a:	CONDRET("Unordered or Greater Than",NaN || !(N || Z));
	case 0x0b:	CONDRET("Unordered or Greater or Equal",NaN || Z || !N);
	case 0x0c:	CONDRET("Unordered or Less Than",NaN || (N && !Z));
	case 0x0d:	CONDRET("Unordered or Less or Equal",NaN || Z || N);
	case 0x0e:	CONDRET("Not Equal",!Z);
	case 0x0f:	CONDRET("True",1);
	case 0x10:	CONDRET("Signaling False",0);
	case 0x11:	CONDRET("Signaling Equal",Z);
	case 0x12:	CONDRET("Greater Than",!(NaN || Z || N));
	case 0x13:	CONDRET("Greater Than or Equal",Z || !(NaN || N));
	case 0x14:	CONDRET("Less Than",N && !(NaN || Z));
	case 0x15:	CONDRET("Less Than or Equal",Z || (N && !NaN));
	case 0x16:	CONDRET("Greater or Less Than",!(NaN || Z));
	case 0x17:	CONDRET("Greater, Less or Equal",!NaN);
	case 0x18:	CONDRET("Not Greater, Less or Equal",NaN);
	case 0x19:	CONDRET("Not Greater or Less Than",NaN || Z);
	case 0x1a:	CONDRET("Not Less Than or Equal",NaN || !(N || Z));
	case 0x1b:	CONDRET("Not Less Than",NaN || Z || !N);
	case 0x1c:	CONDRET("Not Greater Than or Equal", NaN || (N && !Z));
	case 0x1d:	CONDRET("Not Greater Than",NaN || Z || N);
	case 0x1e:	CONDRET("Signaling Not Equal",!Z);
	case 0x1f:	CONDRET("Signaling True",1);
	default:	CONDRET("",-1);
	}
}

void FFPU fpuop_dbcc(uae_u32 opcode, uae_u32 extra)
{
	uaecptr pc = (uae_u32) m68k_getpc ();
	uae_s32 disp = (uae_s32) (uae_s16) next_iword();
	int cc = fpp_cond(extra & 0x3f);
	if (cc == -1) {
		m68k_setpc (pc - 4);
		op_illg (opcode);
	} else if (!cc) {
		int reg = opcode & 0x7;

		m68k_dreg (regs, reg) = ((m68k_dreg (regs, reg) & 0xffff0000)
				| (((m68k_dreg (regs, reg) & 0xffff) - 1) & 0xffff));

		if ((m68k_dreg (regs, reg) & 0xffff) != 0xffff)
		m68k_setpc (pc + disp);
	}
}

void FFPU fpuop_scc(uae_u32 opcode, uae_u32 extra)
{
	uae_u32 ad;
	int cc = fpp_cond(extra & 0x3f);
	if (cc == -1) {
		m68k_setpc (m68k_getpc () - 4);
		op_illg (opcode);
	}
	else if ((opcode & 0x38) == 0) {
		m68k_dreg (regs, opcode & 7) = (m68k_dreg (regs, opcode & 7) & ~0xff) |
		(cc ? 0xff : 0x00);
	}
	else if (get_fp_ad(opcode, &ad) == 0) {
		m68k_setpc (m68k_getpc () - 4);
		op_illg (opcode);
	}
	else
		put_byte(ad, cc ? 0xff : 0x00);
}

void FFPU fpuop_trapcc(uae_u32 opcode, uaecptr oldpc)
{
	int cc = fpp_cond(opcode & 0x3f);
	if (cc == -1) {
		m68k_setpc (oldpc);
		op_illg (opcode);
	}
	if (cc)
		Exception(7, oldpc - 2);
}

// NOTE that we get here also when there is a FNOP (nontrapping false, displ 0)
void FFPU fpuop_bcc(uae_u32 opcode, uaecptr pc, uae_u32 extra)
{
#ifdef SOFT_EXCEPTIONS
	// check exceptions
	if (FPU fpcr.exception_enable && accrued_exception_flags)
	{
		if (FPU fpcr.exception_enable & FPCR_EXCEPTION_BSUN)
		{
			if (accrued_exception_flags & float_flag_invalid)
			{
				// take Coprocessor Pre-Instruction Exception
				m68k_setpc (pc);
				Exception(48, 0);
				return;
			}
		}
		else if (FPU fpcr.exception_enable & FPCR_EXCEPTION_INEX2)
		{
			if (accrued_exception_flags & float_flag_inexact)
			{
				// take Coprocessor Pre-Instruction Exception
				m68k_setpc (pc);
				Exception(49, 0);
				return;
			}
		}
		else if (FPU fpcr.exception_enable & FPCR_EXCEPTION_DZ)
		{
			if (accrued_exception_flags & float_flag_divbyzero)
			{
				// take Coprocessor Pre-Instruction Exception
				m68k_setpc (pc);
				Exception(50, 0);
				return;
			}
		}
		else if (FPU fpcr.exception_enable & FPCR_EXCEPTION_UNFL)
		{
			if (accrued_exception_flags & float_flag_underflow)
			{
				// take Coprocessor Pre-Instruction Exception
				m68k_setpc (pc);
				Exception(51, 0);
				return;
			}
		}
		else if (FPU fpcr.exception_enable & FPCR_EXCEPTION_OPERR)
		{
			if (accrued_exception_flags & float_flag_invalid)
			{
				// take Coprocessor Pre-Instruction Exception
				m68k_setpc (pc);
				Exception(52, 0);
				return;
			}
		}
		else if (FPU fpcr.exception_enable & FPCR_EXCEPTION_OVFL)
		{
			if (accrued_exception_flags & float_flag_overflow)
			{
				// take Coprocessor Pre-Instruction Exception
				m68k_setpc (pc);
				Exception(53, 0);
				return;
			}
		}
		else if (FPU fpcr.exception_enable & FPCR_EXCEPTION_SNAN)
		{
			if ((FPU result.high & 0x7fff) == 0x7fff && FPU result.low != 0 && !(FPU result.low & 0x4000000000000000LL))
			{
				// take Coprocessor Pre-Instruction Exception
				m68k_setpc (pc);
				Exception(54, 0);
				return;
			}
		}
	}
#endif
	int cc = fpp_cond(opcode & 0x3f);
	if (cc == -1) {
		m68k_setpc (pc);
		op_illg (opcode);
	}
	else if (cc) {
		if ((opcode & 0x40) == 0)
			extra = (uae_s32) (uae_s16) extra;
		m68k_setpc (pc + extra);
	}
}

// FSAVE has no post-increment
// 0x1f180000 == IDLE state frame, coprocessor version number 1F
void FFPU fpuop_save(uae_u32 opcode)
{
	uae_u32 ad;
	int incr = (opcode & 0x38) == 0x20 ? -1 : 1;
	int i;

	if (get_fp_ad(opcode, &ad) == 0) {
		m68k_setpc (m68k_getpc () - 2);
		op_illg (opcode);
		return;
	}

	if (CPUType == 4) {
		// Put 4 byte 68040 IDLE frame.
		if (incr < 0) {
			ad -= 4;
			put_long (ad, 0x41000000);
		}
		else {
			put_long (ad, 0x41000000);
			ad += 4;
		}
	} else {
		// Put 28 byte 68881 IDLE frame.
		if (incr < 0) {
			ad -= 4;
			// What's this? Some BIU flags, or (incorrectly placed) command/condition?
			put_long (ad, 0x70000000);
			for (i = 0; i < 5; i++) {
				ad -= 4;
				put_long (ad, 0x00000000);
			}
			ad -= 4;
			put_long (ad, 0x1f180000); // IDLE, vers 1f
		}
		else {
			put_long (ad, 0x1f180000); // IDLE, vers 1f
			ad += 4;
			for (i = 0; i < 5; i++) {
				put_long (ad, 0x00000000);
				ad += 4;
			}
			// What's this? Some BIU flags, or (incorrectly placed) command/condition?
			put_long (ad, 0x70000000);
			ad += 4;
		}
	}
	if ((opcode & 0x38) == 0x18) {
		m68k_areg (regs, opcode & 7) = ad; // Never executed on a 68881
	}
	if ((opcode & 0x38) == 0x20) {
		m68k_areg (regs, opcode & 7) = ad;
	}
}

// FRESTORE has no pre-decrement
void FFPU fpuop_restore(uae_u32 opcode)
{
	uae_u32 ad;
	uae_u32 d;
	int incr = (opcode & 0x38) == 0x20 ? -1 : 1;

	if (get_fp_ad(opcode, &ad) == 0) {
		m68k_setpc (m68k_getpc () - 2);
		op_illg (opcode);
		return;
	}

	if (CPUType == 4) {
		// 68040
		if (incr < 0) {
			// this may be wrong, but it's never called.
			ad -= 4;
			d = get_long (ad);
			if ((d & 0xff000000) != 0) { // Not a NULL frame?
				if ((d & 0x00ff0000) == 0) { 				// IDLE
					;
				}
				else if ((d & 0x00ff0000) == 0x00300000) {	// UNIMP
					ad -= 0x30;
				}
				else if ((d & 0x00ff0000) == 0x00600000) {	// BUSY
					ad -= 0x60;
				}
			}
		}
		else {
			d = get_long (ad);
			ad += 4;
			if ((d & 0xff000000) != 0) { // Not a NULL frame?
				if ((d & 0x00ff0000) == 0) { 				// IDLE
					;
				}
				else if ((d & 0x00ff0000) == 0x00300000) {	// UNIMP
					ad += 0x30;
				}
				else if ((d & 0x00ff0000) == 0x00600000) {	// BUSY
					ad += 0x60;
				}
			}
		}
	}
	else {
		// 68881/2
		if (incr < 0) {
			// this may be wrong, but it's never called.
			ad -= 4;
			d = get_long (ad);
			if ((d & 0xff000000) != 0) {
				if ((d & 0x00ff0000) == 0x00180000)			// 68881 Idle Frame
					ad -= 0x18;
				else if ((d & 0x00ff0000) == 0x00380000)	// 68882 Idle Frame
					ad -= 0x38;
				else if ((d & 0x00ff0000) == 0x00b40000)	// 68881 Busy Frame
					ad -= 0xb4;
				else if ((d & 0x00ff0000) == 0x00d40000)	// 68882 Busy Frame
					ad -= 0xd4;
			}
		}
		else {
			d = get_long (ad);
			ad += 4;
			if ((d & 0xff000000) != 0) {
				if ((d & 0x00ff0000) == 0x00180000)			// 68881 Idle Frame
					ad += 0x18;
				else if ((d & 0x00ff0000) == 0x00380000)	// 68882 Idle Frame
					ad += 0x38;
				else if ((d & 0x00ff0000) == 0x00b40000)	// 68881 Busy Frame
					ad += 0xb4;
				else if ((d & 0x00ff0000) == 0x00d40000)	// 68882 Busy Frame
					ad += 0xd4;
			}
		}
	}
	if ((opcode & 0x38) == 0x18) {
		m68k_areg (regs, opcode & 7) = ad;
	}
	if ((opcode & 0x38) == 0x20) {
		m68k_areg (regs, opcode & 7) = ad; // Never executed on a 68881
	}
}

void FFPU fpuop_arithmetic(uae_u32 opcode, uae_u32 extra)
{
	int reg;
	fpu_register src;

	switch ((extra >> 13) & 0x7) {
	case 3:
		fpu_debug(("FMOVE -> <ea>\n"));
		if (put_fp_value (opcode, extra, FPU registers[(extra >> 7) & 7]) == 0) {
			m68k_setpc (m68k_getpc () - 4);
			op_illg (opcode);
		}
		return;
	case 4:
	case 5:
		if ((opcode & 0x38) == 0) {
			if (extra & 0x2000) { // dr bit
				if (extra & 0x1000) {
					// according to the manual, the msb bits are always zero.
					m68k_dreg (regs, opcode & 7) = get_fpcr() & 0xFFFF;
					fpu_debug(("FMOVEM FPU fpcr (%X) -> D%d\n", get_fpcr(), opcode & 7));
				}
				if (extra & 0x0800) {
					m68k_dreg (regs, opcode & 7) = get_fpsr();
					fpu_debug(("FMOVEM FPU fpsr (%X) -> D%d\n", get_fpsr(), opcode & 7));
				}
				if (extra & 0x0400) {
					m68k_dreg (regs, opcode & 7) = FPU instruction_address;
					fpu_debug(("FMOVEM FPU instruction_address (%X) -> D%d\n", FPU instruction_address, opcode & 7));
				}
			}
			else {
				if (extra & 0x1000) {
					set_fpcr( m68k_dreg (regs, opcode & 7) );
					fpu_debug(("FMOVEM D%d (%X) -> FPU fpcr\n", opcode & 7, get_fpcr()));
				}
				if (extra & 0x0800) {
					set_fpsr( m68k_dreg (regs, opcode & 7) );
					fpu_debug(("FMOVEM D%d (%X) -> FPU fpsr\n", opcode & 7, get_fpsr()));
				}
				if (extra & 0x0400) {
					FPU instruction_address = m68k_dreg (regs, opcode & 7);
					fpu_debug(("FMOVEM D%d (%X) -> FPU instruction_address\n", opcode & 7, FPU instruction_address));
				}
			}
		}
		else if ((opcode & 0x38) == 8) {
			if (extra & 0x2000) { // dr bit
				if (extra & 0x1000) {
					// according to the manual, the msb bits are always zero.
					m68k_areg (regs, opcode & 7) = get_fpcr() & 0xFFFF;
					fpu_debug(("FMOVEM FPU fpcr (%X) -> A%d\n", get_fpcr(), opcode & 7));
				}
				if (extra & 0x0800) {
					m68k_areg (regs, opcode & 7) = get_fpsr();
					fpu_debug(("FMOVEM FPU fpsr (%X) -> A%d\n", get_fpsr(), opcode & 7));
				}
				if (extra & 0x0400) {
					m68k_areg (regs, opcode & 7) = FPU instruction_address;
					fpu_debug(("FMOVEM FPU instruction_address (%X) -> A%d\n", FPU instruction_address, opcode & 7));
				}
			} else {
				if (extra & 0x1000) {
					set_fpcr( m68k_areg (regs, opcode & 7) );
					fpu_debug(("FMOVEM A%d (%X) -> FPU fpcr\n", opcode & 7, get_fpcr()));
				}
				if (extra & 0x0800) {
					set_fpsr( m68k_areg (regs, opcode & 7) );
					fpu_debug(("FMOVEM A%d (%X) -> FPU fpsr\n", opcode & 7, get_fpsr()));
				}
				if (extra & 0x0400) {
					FPU instruction_address = m68k_areg (regs, opcode & 7);
					fpu_debug(("FMOVEM A%d (%X) -> FPU instruction_address\n", opcode & 7, FPU instruction_address));
				}
			}
		}
		else if ((opcode & 0x3f) == 0x3c) {
			if ((extra & 0x2000) == 0) {
				if (extra & 0x1000) {
					set_fpcr( next_ilong() );
					fpu_debug(("FMOVEM #<%X> -> FPU fpcr\n", get_fpcr()));
				}
				if (extra & 0x0800) {
					set_fpsr( next_ilong() );
					fpu_debug(("FMOVEM #<%X> -> FPU fpsr\n", get_fpsr()));
				}
				if (extra & 0x0400) {
					FPU instruction_address = next_ilong();
					fpu_debug(("FMOVEM #<%X> -> FPU instruction_address\n", FPU instruction_address));
				}
			}
		}
		else if (extra & 0x2000) {
			/* FMOVEM FPP->memory */
			uae_u32 ad;
			int incr = 0;

			if (get_fp_ad(opcode, &ad) == 0) {
				m68k_setpc (m68k_getpc () - 4);
				op_illg (opcode);
				return;
			}
			if ((opcode & 0x38) == 0x20) {
				if (extra & 0x1000)
					incr += 4;
				if (extra & 0x0800)
					incr += 4;
				if (extra & 0x0400)
					incr += 4;
			}
			ad -= incr;
			if (extra & 0x1000) {
				// according to the manual, the msb bits are always zero.
				put_long (ad, get_fpcr() & 0xFFFF);
				fpu_debug(("FMOVEM FPU fpcr (%X) -> mem %X\n", get_fpcr(), ad ));
				ad += 4;
			}
			if (extra & 0x0800) {
				put_long (ad, get_fpsr());
				fpu_debug(("FMOVEM FPU fpsr (%X) -> mem %X\n", get_fpsr(), ad ));
				ad += 4;
			}
			if (extra & 0x0400) {
				put_long (ad, FPU instruction_address);
				fpu_debug(("FMOVEM FPU instruction_address (%X) -> mem %X\n", FPU instruction_address, ad ));
				ad += 4;
			}
			ad -= incr;
			if ((opcode & 0x38) == 0x18) // post-increment?
				m68k_areg (regs, opcode & 7) = ad;
			if ((opcode & 0x38) == 0x20) // pre-decrement?
				m68k_areg (regs, opcode & 7) = ad;
		}
		else {
			/* FMOVEM memory->FPP */
			uae_u32 ad;

			if (get_fp_ad(opcode, &ad) == 0) {
				m68k_setpc (m68k_getpc () - 4);
				op_illg (opcode);
				return;
			}

			// ad = (opcode & 0x38) == 0x20 ? ad - 12 : ad;
			int incr = 0;
			if((opcode & 0x38) == 0x20) {
				if (extra & 0x1000)
					incr += 4;
				if (extra & 0x0800)
					incr += 4;
				if (extra & 0x0400)
					incr += 4;
				ad = ad - incr;
			}

			if (extra & 0x1000) {
				set_fpcr( get_long (ad) );
				fpu_debug(("FMOVEM mem %X (%X) -> FPU fpcr\n", ad, get_fpcr() ));
				ad += 4;
			}
			if (extra & 0x0800) {
				set_fpsr( get_long (ad) );
				fpu_debug(("FMOVEM mem %X (%X) -> FPU fpsr\n", ad, get_fpsr() ));
				ad += 4;
			}
			if (extra & 0x0400) {
				FPU instruction_address = get_long (ad);
				fpu_debug(("FMOVEM mem %X (%X) -> FPU instruction_address\n", ad, FPU instruction_address ));
				ad += 4;
			}
			if ((opcode & 0x38) == 0x18) // post-increment?
				m68k_areg (regs, opcode & 7) = ad;
			if ((opcode & 0x38) == 0x20) // pre-decrement?
//				m68k_areg (regs, opcode & 7) = ad - 12;
				m68k_areg (regs, opcode & 7) = ad - incr;
		}
		return;
	case 6:
	case 7: {
		uae_u32 ad, list = 0;
		int incr = 0;
		if (extra & 0x2000) {
			/* FMOVEM FPP->memory */
			fpu_debug(("FMOVEM FPP->memory\n"));

			if (get_fp_ad(opcode, &ad) == 0) {
				m68k_setpc (m68k_getpc () - 4);
				op_illg (opcode);
				return;
			}
			switch ((extra >> 11) & 3) {
			case 0:	/* static pred */
				list = extra & 0xff;
				incr = -1;
				break;
			case 1:	/* dynamic pred */
				list = m68k_dreg (regs, (extra >> 4) & 3) & 0xff;
				incr = -1;
				break;
			case 2:	/* static postinc */
				list = extra & 0xff;
				incr = 1;
				break;
			case 3:	/* dynamic postinc */
				list = m68k_dreg (regs, (extra >> 4) & 3) & 0xff;
				incr = 1;
				break;
			}

			if (incr < 0) {
				for(reg=7; reg>=0; reg--) {
					uae_u32 wrd1, wrd2, wrd3;
					if( list & 0x80 ) {
						extract_extended(FPU registers[reg], &wrd1, &wrd2, &wrd3);
						ad -= 4;
						put_long (ad, wrd3);
						ad -= 4;
						put_long (ad, wrd2);
						ad -= 4;
						put_long (ad, wrd1);
					}
					list <<= 1;
				}
			}
			else {
				for(reg=0; reg<8; reg++) {
					uae_u32 wrd1, wrd2, wrd3;
					if( list & 0x80 ) {
						extract_extended(FPU registers[reg], &wrd1, &wrd2, &wrd3);
						put_long (ad, wrd1);
						ad += 4;
						put_long (ad, wrd2);
						ad += 4;
						put_long (ad, wrd3);
						ad += 4;
					}
					list <<= 1;
				}
			}
			if ((opcode & 0x38) == 0x18) // post-increment?
				m68k_areg (regs, opcode & 7) = ad;
			if ((opcode & 0x38) == 0x20) // pre-decrement?
				m68k_areg (regs, opcode & 7) = ad;
		}
		else {
			/* FMOVEM memory->FPP */
			fpu_debug(("FMOVEM memory->FPP\n"));

			if (get_fp_ad(opcode, &ad) == 0) {
				m68k_setpc (m68k_getpc () - 4);
				op_illg (opcode);
				return;
			}
			switch ((extra >> 11) & 3) {
			case 0:	/* static pred */
				fpu_debug(("memory->FMOVEM FPP not legal mode.\n"));
				list = extra & 0xff;
				incr = -1;
				break;
			case 1:	/* dynamic pred */
				fpu_debug(("memory->FMOVEM FPP not legal mode.\n"));
				list = m68k_dreg (regs, (extra >> 4) & 3) & 0xff;
				incr = -1;
				break;
			case 2:	/* static postinc */
				list = extra & 0xff;
				incr = 1;
				break;
			case 3:	/* dynamic postinc */
				list = m68k_dreg (regs, (extra >> 4) & 3) & 0xff;
				incr = 1;
				break;
			}

			/**/
			if (incr < 0) {
				// not reached
				for(reg=7; reg>=0; reg--) {
					uae_u32 wrd1, wrd2, wrd3;
					if( list & 0x80 ) {
						ad -= 4;
						wrd3 = get_long (ad);
						ad -= 4;
						wrd2 = get_long (ad);
						ad -= 4;
						wrd1 = get_long (ad);
						FPU registers[reg] = make_extended(wrd1, wrd2, wrd3);
						//make_extended_no_normalize (wrd1, wrd2, wrd3, FPU registers[reg]);
					}
					list <<= 1;
				}
			}
			else {
				for(reg=0; reg<8; reg++) {
					uae_u32 wrd1, wrd2, wrd3;
					if( list & 0x80 ) {
						wrd1 = get_long (ad);
						ad += 4;
						wrd2 = get_long (ad);
						ad += 4;
						wrd3 = get_long (ad);
						ad += 4;
						FPU registers[reg] = make_extended(wrd1, wrd2, wrd3);
						//make_extended_no_normalize (wrd1, wrd2, wrd3, FPU registers[reg]);
					}
					list <<= 1;
				}
			}
			if ((opcode & 0x38) == 0x18) // post-increment?
				m68k_areg (regs, opcode & 7) = ad;
			if ((opcode & 0x38) == 0x20) // pre-decrement?
				m68k_areg (regs, opcode & 7) = ad;
		}
		return;
	}
	case 0:
	case 2:
		reg = (extra >> 7) & 7;
		if ((extra & 0xfc00) == 0x5c00) {
			fpu_debug(("FMOVECR memory->FPP\n"));
			if (extra & 0x7f >= 0x40)
				FPU registers[reg] = packFloatx80(0, 0, 0); // 0.0
			else
			{
				FPU registers[reg].low = ((uint64_t)constantRom[(extra & 0x3f) * 3 + 1] << 32) | constantRom[(extra & 0x3f) * 3];
				FPU registers[reg].high = constantRom[(extra & 0x3f) * 3 + 2];
			}
			// these *do* affect the status reg
			make_fpsr(FPU registers[reg]);
			return;
		}

		if (get_fp_value (opcode, extra, src) == 0) {
			m68k_setpc (m68k_getpc () - 4);
			op_illg (opcode);
			return;
		}

		if (FPU is_integral) {
			// 68040-specific operations
			switch (extra & 0x7f) {
			case 0x40:		/* FSMOVE */
				fpu_debug(("FSMOVE %.04f\n",(double)src));
				FPU registers[reg] = float32_to_floatx80(floatx80_to_float32(src FPS) FPS);
				make_fpsr(FPU registers[reg]);
				break;
			case 0x41:		/* FSSQRT */
				fpu_debug(("FSQRT %.04f\n",(double)src));
				FPU registers[reg] = float32_to_floatx80(floatx80_to_float32(floatx80_sqrt(src FPS) FPS) FPS);
				make_fpsr(FPU registers[reg]);
				break;
			case 0x44:		/* FDMOVE */
				fpu_debug(("FDMOVE %.04f\n",(double)src));
				FPU registers[reg] = float64_to_floatx80(floatx80_to_float64(src FPS) FPS);
				make_fpsr(FPU registers[reg]);
				break;
			case 0x45:		/* FDSQRT */
				fpu_debug(("FSQRT %.04f\n",(double)src));
				FPU registers[reg] = float64_to_floatx80(floatx80_to_float64(floatx80_sqrt(src FPS) FPS) FPS);
				make_fpsr(FPU registers[reg]);
				break;
			case 0x58:		/* FSABS */
				fpu_debug(("FSABS %.04f\n",(double)src));
				FPU registers[reg] = float32_to_floatx80(floatx80_to_float32(floatx80_abs(src) FPS) FPS);
				make_fpsr(FPU registers[reg]);
				break;
			case 0x5a:		/* FSNEG */
				fpu_debug(("FSNEG %.04f\n",(double)src));
				FPU registers[reg] = float32_to_floatx80(floatx80_to_float32(floatx80_chs(src) FPS) FPS);
				make_fpsr(FPU registers[reg]);
				break;
			case 0x5c:		/* FDABS */
				fpu_debug(("FDABS %.04f\n",(double)src));
				FPU registers[reg] = float64_to_floatx80(floatx80_to_float64(floatx80_abs(src) FPS) FPS);
				make_fpsr(FPU registers[reg]);
				break;
			case 0x5e:		/* FDNEG */
				fpu_debug(("FDNEG %.04f\n",(double)src));
				FPU registers[reg] = float64_to_floatx80(floatx80_to_float64(floatx80_chs(src) FPS) FPS);
				make_fpsr(FPU registers[reg]);
				break;
			case 0x60:		/* FSDIV */
				fpu_debug(("FSDIV %.04f\n",(double)src));
				FPU registers[reg] = float32_to_floatx80(floatx80_to_float32(floatx80_div(FPU registers[reg], src FPS) FPS) FPS);
				make_fpsr(FPU registers[reg]);
				break;
			case 0x62:		/* FSADD */
				fpu_debug(("FSADD %.04f\n",(double)src));
				FPU registers[reg] = float32_to_floatx80(floatx80_to_float32(floatx80_add(FPU registers[reg], src FPS) FPS) FPS);
				make_fpsr(FPU registers[reg]);
				break;
			case 0x63:		/* FSMUL */
				fpu_debug(("FSMUL %.04f\n",(double)src));
				FPU registers[reg] = float32_to_floatx80(floatx80_to_float32(floatx80_mul(FPU registers[reg], src FPS) FPS) FPS);
				make_fpsr(FPU registers[reg]);
				break;
			case 0x64:		/* FDDIV */
				fpu_debug(("FDDIV %.04f\n",(double)src));
				FPU registers[reg] = float64_to_floatx80(floatx80_to_float64(floatx80_div(FPU registers[reg], src FPS) FPS) FPS);
				make_fpsr(FPU registers[reg]);
				break;
			case 0x66:		/* FDADD */
				fpu_debug(("FDADD %.04f\n",(double)src));
				FPU registers[reg] = float64_to_floatx80(floatx80_to_float64(floatx80_add(FPU registers[reg], src FPS) FPS) FPS);
				make_fpsr(FPU registers[reg]);
				break;
			case 0x67:		/* FDMUL */
				fpu_debug(("FDMUL %.04f\n",(double)src));
				FPU registers[reg] = float64_to_floatx80(floatx80_to_float64(floatx80_mul(FPU registers[reg], src FPS) FPS) FPS);
				make_fpsr(FPU registers[reg]);
				break;
			case 0x68:		/* FSSUB */
				fpu_debug(("FSSUB %.04f\n",(double)src));
				FPU registers[reg] = float32_to_floatx80(floatx80_to_float32(floatx80_sub(FPU registers[reg], src FPS) FPS) FPS);
				make_fpsr(FPU registers[reg]);
				break;
			case 0x6c:		/* FDSUB */
				fpu_debug(("FDSUB %.04f\n",(double)src));
				FPU registers[reg] = float64_to_floatx80(floatx80_to_float64(floatx80_sub(FPU registers[reg], src FPS) FPS) FPS);
				make_fpsr(FPU registers[reg]);
				break;
			default:
				// Continue decode-execute 6888x instructions below
				goto process_6888x_instructions;
			}
			return;
		}

	process_6888x_instructions:
		errno = 0;
		switch (extra & 0x7f) {
		case 0x00:		/* FMOVE */
			fpu_debug(("FMOVE %.04f\n",(double)src));
			FPU registers[reg] = src;
			make_fpsr(FPU registers[reg]);
			break;
		case 0x01:		/* FINT */
			fpu_debug(("FINT %.04f\n",(double)src));
			FPU registers[reg] = floatx80_round_to_int(src FPS);
			make_fpsr(FPU registers[reg]);
			break;
		case 0x02:		/* FSINH */
			fpu_debug(("FSINH %.04f\n",(double)src));
			FPU registers[reg] = float64_to_floatx80(CHKERR(sinh(floatx80_to_float64(src FPS))) FPS);
			make_fpsr(FPU registers[reg]);
			break;
		case 0x03:		/* FINTRZ */
			fpu_debug(("FINTRZ %.04f\n",(double)src));
			set_float_rounding_mode(float_round_to_zero FPS);
			FPU registers[reg] = floatx80_round_to_int(src FPS);
			set_host_control_word(); // restore rounding mode
			make_fpsr(FPU registers[reg]);
			break;
		case 0x04:		/* FSQRT */
			fpu_debug(("FSQRT %.04f\n",(double)src));
			FPU registers[reg] = floatx80_sqrt(src FPS);
			make_fpsr(FPU registers[reg]);
			break;
		case 0x06:		/* FLOGNP1 */
			fpu_debug(("FLOGNP1 %.04f\n",(double)src));
			FPU registers[reg] = float64_to_floatx80(CHKERR(log(floatx80_to_float64(src FPS) + 1.0)) FPS);
			make_fpsr(FPU registers[reg]);
			break;
		case 0x08:		/* FETOXM1 */
			fpu_debug(("FETOXM1 %.04f\n",(double)src));
			FPU registers[reg] = float64_to_floatx80(CHKERR(exp(floatx80_to_float64(src FPS))) - 1.0 FPS);
			make_fpsr(FPU registers[reg]);
			break;
		case 0x09:		/* FTANH */
			fpu_debug(("FTANH %.04f\n",(double)src));
			FPU registers[reg] = float64_to_floatx80(CHKERR(tanh(floatx80_to_float64(src FPS))) FPS);
			make_fpsr(FPU registers[reg]);
			break;
		case 0x0a:		/* FATAN */
			fpu_debug(("FATAN %.04f\n",(double)src));
			FPU registers[reg] = float64_to_floatx80(CHKERR(atan(floatx80_to_float64(src FPS))) FPS);
			make_fpsr(FPU registers[reg]);
			break;
		case 0x0c:		/* FASIN */
			fpu_debug(("FASIN %.04f\n",(double)src));
			FPU registers[reg] = float64_to_floatx80(CHKERR(asin(floatx80_to_float64(src FPS))) FPS);
			make_fpsr(FPU registers[reg]);
			break;
		case 0x0d:		/* FATANH */
			fpu_debug(("FATANH %.04f\n",(double)src));
			FPU registers[reg] = float64_to_floatx80(CHKERR(atanh(floatx80_to_float64(src FPS))) FPS);
			make_fpsr(FPU registers[reg]);
			break;
		case 0x0e:		/* FSIN */
			fpu_debug(("FSIN %.04f\n",(double)src));
			FPU registers[reg] = float64_to_floatx80(CHKERR(sin(floatx80_to_float64(src FPS))) FPS);
			make_fpsr(FPU registers[reg]);
			break;
		case 0x0f:		/* FTAN */
			fpu_debug(("FTAN %.04f\n",(double)src));
			FPU registers[reg] = float64_to_floatx80(CHKERR(tan(floatx80_to_float64(src FPS))) FPS);
			make_fpsr(FPU registers[reg]);
			break;
		case 0x10:		/* FETOX */
			fpu_debug(("FETOX %.04f\n",(double)src));
			FPU registers[reg] = float64_to_floatx80(CHKERR(exp(floatx80_to_float64(src FPS))) FPS);
			make_fpsr(FPU registers[reg]);
			break;
		case 0x11:		/* FTWOTOX */
			fpu_debug(("FTWOTOX %.04f\n",(double)src));
			FPU registers[reg] = float64_to_floatx80(CHKERR(pow(2.0, floatx80_to_float64(src FPS))) FPS);
			make_fpsr(FPU registers[reg]);
			break;
		case 0x12:		/* FTENTOX */
			fpu_debug(("FTENTOX %.04f\n",(double)src));
			FPU registers[reg] = float64_to_floatx80(CHKERR(pow(10.0, floatx80_to_float64(src FPS))) FPS);
			make_fpsr(FPU registers[reg]);
			break;
		case 0x14:		/* FLOGN */
			fpu_debug(("FLOGN %.04f\n",(double)src));
			FPU registers[reg] = float64_to_floatx80(CHKERR(log(floatx80_to_float64(src FPS))) FPS);
			make_fpsr(FPU registers[reg]);
			break;
		case 0x15:		/* FLOG10 */
			fpu_debug(("FLOG10 %.04f\n",(double)src));
			FPU registers[reg] = float64_to_floatx80(CHKERR(log10(floatx80_to_float64(src FPS))) FPS);
			make_fpsr(FPU registers[reg]);
			break;
		case 0x16:		/* FLOG2 */
			fpu_debug(("FLOG2 %.04f\n",(double)src));
			FPU registers[reg] = float64_to_floatx80(CHKERR(log(floatx80_to_float64(src FPS)) / log(2.0)) FPS);
			make_fpsr(FPU registers[reg]);
			break;
		case 0x18:		/* FABS */
			fpu_debug(("FABS %.04f\n",(double)src));
			FPU registers[reg] = floatx80_abs(src);
			make_fpsr(FPU registers[reg]);
			break;
		case 0x19:		/* FCOSH */
			fpu_debug(("FCOSH %.04f\n",(double)src));
			FPU registers[reg] = float64_to_floatx80(CHKERR(cosh(floatx80_to_float64(src FPS))) FPS);
			make_fpsr(FPU registers[reg]);
			break;
		case 0x1a:		/* FNEG */
			fpu_debug(("FNEG %.04f\n",(double)src));
			FPU registers[reg] = floatx80_chs(src);
			make_fpsr(FPU registers[reg]);
			break;
		case 0x1c:		/* FACOS */
			fpu_debug(("FACOS %.04f\n",(double)src));
			FPU registers[reg] = float64_to_floatx80(CHKERR(acos(floatx80_to_float64(src FPS))) FPS);
			make_fpsr(FPU registers[reg]);
			break;
		case 0x1d:		/* FCOS */
			fpu_debug(("FCOS %.04f\n",(double)src));
			FPU registers[reg] = float64_to_floatx80(CHKERR(cos(floatx80_to_float64(src FPS))) FPS);
			make_fpsr(FPU registers[reg]);
			break;
		case 0x1e:		/* FGETEXP */
			fpu_debug(("FGETEXP %.04f\n",(double)src));
			if( (src.high & 0x7fff) == 0 && src.low == 0 ) {
				// zero -> zero
				FPU registers[reg] = src;
			}
			else if( (src.high & 0x7fff) == 0x7fff && src.low == 0 ) {
				// inf -> nan
				FPU registers[reg].high = floatx80_default_nan_high;
				FPU registers[reg].low = floatx80_default_nan_low;
				set_float_exception_flags(get_float_exception_flags(&fp_status) | float_flag_invalid, &fp_status);
			}
			else if( (src.high & 0x7fff) == 0x7fff && src.low != 0 ) {
				// nan -> nan
				FPU registers[reg].high = floatx80_default_nan_high;
				FPU registers[reg].low = floatx80_default_nan_low;
			}
			else {
				FPU registers[reg] = int32_to_floatx80(extractFloatx80Exp(src) - FP_EXTENDED_EXP_BIAS FPS);
			}
			make_fpsr(FPU registers[reg]);
			break;
		case 0x1f:		/* FGETMAN */
			fpu_debug(("FGETMAN %.04f\n",(double)src));
			if( (src.high & 0x7fff) == 0 && src.low == 0 ) {
				// zero -> zero
				FPU registers[reg] = src;
			}
			else if( (src.high & 0x7fff) == 0x7fff && src.low == 0 ) {
				// inf -> nan
				FPU registers[reg].high = floatx80_default_nan_high;
				FPU registers[reg].low = floatx80_default_nan_low;
				set_float_exception_flags(get_float_exception_flags(&fp_status) | float_flag_invalid, &fp_status);
			}
			else if( (src.high & 0x7fff) == 0x7fff && src.low != 0 ) {
				// nan -> nan
				FPU registers[reg].high = floatx80_default_nan_high;
				FPU registers[reg].low = floatx80_default_nan_low;
			}
			else {
				FPU registers[reg] = packFloatx80(extractFloatx80Sign(src), FP_EXTENDED_EXP_BIAS, extractFloatx80Frac(src));
			}
			make_fpsr(FPU registers[reg]);
			break;
		case 0x20:		/* FDIV */
			fpu_debug(("FDIV %.04f\n",(double)src));
			FPU registers[reg] = floatx80_div(FPU registers[reg], src FPS);
			make_fpsr(FPU registers[reg]);
			break;
		case 0x21:		/* FMOD */
			fpu_debug(("FMOD %.04f\n",(double)src));
			// FPU registers[reg] = FPU registers[reg] - (fpu_register) ((int) (FPU registers[reg] / src)) * src;
			{
				set_float_rounding_mode(float_round_to_zero FPS);
				fpu_register quot = floatx80_round_to_int(floatx80_div(FPU registers[reg], src FPS) FPS);
				set_host_control_word(); // restore rounding mode
				uae_u32 sign = (extractFloatx80Sign(FPU registers[reg]) ^ extractFloatx80Sign(src)) ? FPSR_QUOTIENT_SIGN : 0;
				FPU registers[reg] = floatx80_sub(FPU registers[reg], floatx80_mul(quot, src FPS) FPS);
				make_fpsr(FPU registers[reg]);
				make_quotient(quot, sign);
			}
			break;
		case 0x22:		/* FADD */
			fpu_debug(("FADD %.04f\n",(double)src));
			FPU registers[reg] = floatx80_add(FPU registers[reg], src FPS);
			make_fpsr(FPU registers[reg]);
			break;
		case 0x23:		/* FMUL */
			fpu_debug(("FMUL %.04f\n",(double)src));
			FPU registers[reg] = floatx80_mul(FPU registers[reg], src FPS);
			make_fpsr(FPU registers[reg]);
			break;
		case 0x24:		/* FSGLDIV */
			fpu_debug(("FSGLDIV %.04f\n",(double)src));
			FPU registers[reg] = float32_to_floatx80(floatx80_to_float32(floatx80_div(FPU registers[reg], src FPS) FPS) FPS);
			make_fpsr(FPU registers[reg]);
			break;
		case 0x25:		/* FREM */
			fpu_debug(("FREM %.04f\n",(double)src));
			// FPU registers[reg] = FPU registers[reg] - (double) ((int) (FPU registers[reg] / src + 0.5)) * src;
			{
				set_float_rounding_mode(float_round_nearest_even FPS);
				fpu_register quot = floatx80_round_to_int(floatx80_div(FPU registers[reg], src FPS) FPS);
				set_host_control_word(); // restore rounding mode
				uae_u32 sign = (extractFloatx80Sign(FPU registers[reg]) ^ extractFloatx80Sign(src)) ? FPSR_QUOTIENT_SIGN : 0;
				FPU registers[reg] = floatx80_sub(FPU registers[reg], floatx80_mul(quot, src FPS) FPS);
				make_fpsr(FPU registers[reg]);
				make_quotient(quot,sign);
			}
			break;

		case 0x26:		/* FSCALE */
			fpu_debug(("FSCALE %.04f\n",(double)src));
			{
				set_float_rounding_mode(float_round_to_zero FPS);
				uae_s32 temp = floatx80_to_int32(src FPS);
				set_host_control_word(); // restore rounding mode
				FPU registers[reg] = floatx80_scalbn(FPU registers[reg], temp FPS);
				make_fpsr(FPU registers[reg]);
			}
			break;
		case 0x27:		/* FSGLMUL */
			fpu_debug(("FSGLMUL %.04f\n",(double)src));
			FPU registers[reg] = float32_to_floatx80(floatx80_to_float32(floatx80_mul(FPU registers[reg], src FPS) FPS) FPS);
			make_fpsr(FPU registers[reg]);
			break;
		case 0x28:		/* FSUB */
			fpu_debug(("FSUB %.04f\n",(double)src));
			FPU registers[reg] = floatx80_sub(FPU registers[reg], src FPS);
			make_fpsr(FPU registers[reg]);
			break;
		case 0x30:		/* FSINCOS */
		case 0x31:
		case 0x32:
		case 0x33:
		case 0x34:
		case 0x35:
		case 0x36:
		case 0x37:
			fpu_debug(("FSINCOS %.04f\n",(double)src));
			// Cosine must be calculated first if same register
			FPU registers[extra & 7] = float64_to_floatx80(CHKERR(cos(floatx80_to_float64(src FPS))) FPS);
			FPU registers[reg] = float64_to_floatx80(CHKERR(sin(floatx80_to_float64(src FPS))) FPS);
			// Set FPU fpsr according to the sine result
			make_fpsr(FPU registers[reg]);
			break;
		case 0x38:		/* FCMP */
			fpu_debug(("FCMP %.04f\n",(double)src));
			//set_fpsr(0);
			set_exception_status(0);
			make_fpsr(floatx80_sub(FPU registers[reg], src FPS));
			break;
		case 0x3a:		/* FTST */
			fpu_debug(("FTST %.04f\n",(double)src));
			//set_fpsr(0);
			set_exception_status(0);
			make_fpsr(src);
			break;
		default:
			fpu_debug(("ILLEGAL F OP %X\n",opcode));
			m68k_setpc (m68k_getpc () - 4);
			op_illg (opcode);
			break;
		}
		return;
	}

	fpu_debug(("ILLEGAL F OP 2 %X\n",opcode));
	m68k_setpc (m68k_getpc () - 4);
	op_illg (opcode);
}

/* -------------------------- Initialization -------------------------- */

PUBLIC void FFPU fpu_init (bool integral_68040)
{
	fpu_debug(("fpu_init\n"));

	FPU is_integral = integral_68040;
	FPU instruction_address = 0;
	FPU fpsr.quotient = 0;
	set_fpcr(0);
	set_fpsr(0);

	FPU result = int32_to_floatx80(1 FPS);

	for (int i = 0; i < 8; i++) {
		FPU registers[i].high = floatx80_default_nan_high;
		FPU registers[i].low = floatx80_default_nan_low;
	}
}

PUBLIC void FFPU fpu_exit (void)
{
	fpu_debug(("fpu_exit\n"));
}

PUBLIC void FFPU fpu_reset (void)
{
	fpu_debug(("fpu_reset\n"));
	fpu_exit();
	fpu_init(FPU is_integral);
}
