/////////////////////////////////////////////////////////////////////////
// $Id$
/////////////////////////////////////////////////////////////////////////
//
//   Copyright (c) 2003-2018 Stanislav Shwartsman
//          Written by Stanislav Shwartsman [sshwarts at sourceforge net]
//
//  This library is free software; you can redistribute it and/or
//  modify it under the terms of the GNU Lesser General Public
//  License as published by the Free Software Foundation; either
//  version 2 of the License, or (at your option) any later version.
//
//  This library is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
//  Lesser General Public License for more details.
//
//  You should have received a copy of the GNU Lesser General Public
//  License along with this library; if not, write to the Free Software
//  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
//
/////////////////////////////////////////////////////////////////////////

#ifndef BX_FPU_TRANSCENDENTAL_H
#define BX_FPU_TRANSCENDENTAL_H

#include "softfloat.h"
#include "softfloat-specialize.h"

extern extFloat80_t softfloat_propagateNaNExtF80UI(uint16_t uiA64, uint64_t uiA0, uint16_t uiB64, uint64_t uiB0, struct softfloat_status_t *status);

/*----------------------------------------------------------------------------
| Software IEC/IEEE extended double-precision operations.
*----------------------------------------------------------------------------*/

int floatx80_remainder(extFloat80_t a, extFloat80_t b, extFloat80_t &r, Bit64u &q, struct softfloat_status_t *status);
int floatx80_ieee754_remainder(extFloat80_t a, extFloat80_t b, extFloat80_t &r, Bit64u &q, struct softfloat_status_t *status);

floatx80 f2xm1(floatx80 a, float_status_t &status);
floatx80 fyl2x(floatx80 a, floatx80 b, float_status_t &status);
floatx80 fyl2xp1(floatx80 a, floatx80 b, float_status_t &status);
floatx80 fpatan(floatx80 a, floatx80 b, float_status_t &status);

/*----------------------------------------------------------------------------
| Software IEC/IEEE extended double-precision trigonometric functions.
*----------------------------------------------------------------------------*/

int fsincos(floatx80 a, floatx80 *sin_a, floatx80 *cos_a, float_status_t &status);
int fsin(floatx80 &a, float_status_t &status);
int fcos(floatx80 &a, float_status_t &status);
int ftan(floatx80 &a, float_status_t &status);

/*-----------------------------------------------------------------------------
| Calculates the absolute value of the extended double-precision floating-point
| value `a'.  The operation is performed according to the IEC/IEEE Standard
| for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

BX_CPP_INLINE floatx80& floatx80_abs(floatx80 &reg)
{
    reg.exp &= 0x7FFF;
    return reg;
}

/*-----------------------------------------------------------------------------
| Changes the sign of the extended double-precision floating-point value 'a'.
| The operation is performed according to the IEC/IEEE Standard for Binary
| Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

BX_CPP_INLINE floatx80& floatx80_chs(floatx80 &reg)
{
    reg.exp ^= 0x8000;
    return reg;
}

/*-----------------------------------------------------------------------------
| Commonly used extended double-precision floating-point constants.
*----------------------------------------------------------------------------*/

extern const floatx80 Const_Z;
extern const floatx80 Const_1;

#endif