/*
 *  fpu/fpu_soft.h - Extra Definitions for the SoftFloat FPU core
 *
 *  Basilisk II (C) 1997-2005 Christian Bauer
 *
 *  MC68881/68040 fpu emulation
 *
 *  Original UAE FPU, copyright 1996 Herman ten Brugge
 *  Rewrite for x86, copyright 1999-2000 Lauri Pesonen
 *  New framework, copyright 2000 Gwenole Beauchesne
 *  Adapted for JIT compilation (c) Bernd Meyer, 2000
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

#ifndef FPU_SOFT_H
#define FPU_SOFT_H

/* NOTE: this file shall be included from fpu/fpu_soft.cpp */
#undef	PUBLIC
#define PUBLIC	extern

#undef	PRIVATE
#define PRIVATE	static

#undef	FFPU
#define FFPU	/**/

#undef	FPU
#define	FPU		fpu.

// Lauri-- full words to avoid partial register stalls.
struct double_flags {
	uae_u32		in_range;
	uae_u32		zero;
	uae_u32		infinity;
	uae_u32		nan;
	uae_u32		negative;
};


// May be optimized for particular processors
PRIVATE inline void FFPU make_fpsr(fpu_register const &r);

/* Set host control word for rounding mode and rounding precision */
PRIVATE inline void set_host_control_word(void);

/* Return the current rounding mode in m68k format */
static inline uae_u32 FFPU get_rounding_mode(void);

/* Convert and set to native rounding mode */
static inline void FFPU set_rounding_mode(uae_u32 new_rounding_mode);

/* Return the current rounding precision in m68k format */
static inline uae_u32 FFPU get_rounding_precision(void);

/* Convert and set to native rounding precision */
static inline void FFPU set_rounding_precision(uae_u32 new_rounding_precision);

/* Native to m68k floating-point condition codes - SELF */
PRIVATE inline uae_u32 FFPU get_fpccr(void);

/* M68k to native floating-point condition codes - SELF */
PRIVATE inline void FFPU set_fpccr(uae_u32 new_fpcond);

/* Return m68k floating-point exception status */
PRIVATE inline uae_u32 FFPU get_exception_status(void);

/* Set new exception status. Assumes mask against FPSR_EXCEPTION to be already performed */
PRIVATE inline void FFPU set_exception_status(uae_u32 new_status);

/* Return m68k accrued exception byte */
PRIVATE inline uae_u32 FFPU get_accrued_exception(void);

/* Set new accrued exception byte */
PRIVATE inline void FFPU set_accrued_exception(uae_u32 new_status);


// Quotient Byte is loaded with the sign and least significant
// seven bits of the quotient.
PRIVATE inline void FFPU make_quotient(
	fpu_register const &quotient, uae_u32 sign
);

// to_single
PRIVATE inline fpu_register FFPU make_single(
	uae_u32 value
);

// from_single
PRIVATE inline uae_u32 FFPU extract_single(
	fpu_register const &src
);

// to_exten
PRIVATE inline fpu_register FFPU make_extended(
	uae_u32 wrd1, uae_u32 wrd2, uae_u32 wrd3
);

// to_exten_no_normalize
PRIVATE inline void FFPU make_extended_no_normalize(
	uae_u32 wrd1, uae_u32 wrd2, uae_u32 wrd3, fpu_register &result
);

// from_exten
PRIVATE inline void FFPU extract_extended(fpu_register const &src,
	uae_u32 *wrd1, uae_u32 *wrd2, uae_u32 *wrd3
);

// to_double
PRIVATE inline fpu_register FFPU make_double(
	uae_u32 wrd1, uae_u32 wrd2
);

// from_double
PRIVATE inline void FFPU extract_double(fpu_register const &src,
	uae_u32 *wrd1, uae_u32 *wrd2
);

PRIVATE inline fpu_register FFPU make_packed(
	uae_u32 wrd1, uae_u32 wrd2, uae_u32 wrd3
);

PRIVATE inline void FFPU extract_packed(
	fpu_register const &src, uae_u32 *wrd1, uae_u32 *wrd2, uae_u32 *wrd3
);

PRIVATE inline int FFPU get_fp_value(
	uae_u32 opcode, uae_u16 extra, fpu_register &src
);

PRIVATE inline int FFPU put_fp_value(
	uae_u32 opcode, uae_u16 extra, fpu_register const &value
);

PRIVATE inline int FFPU get_fp_ad(
	uae_u32 opcode, uae_u32 *ad
);

PRIVATE inline int FFPU fpp_cond(
	int condition
);

#endif /* FPU_SOFT_H */
