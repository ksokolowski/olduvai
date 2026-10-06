// ****************************************************************************
// * This file is part of the xBRZ project. It is distributed under           *
// * GNU General Public License: https://www.gnu.org/licenses/gpl-3.0         *
// * Copyright (C) Zenju (zenju AT gmx DOT de) - All Rights Reserved          *
// *                                                                          *
// * Additionally and as a special exception, the author gives permission     *
// * to link the code of this program with the following libraries            *
// * (or with modified versions that use the same licenses), and distribute   *
// * linked combinations including the two: MAME, FreeFileSync, Snes9x, ePSXe *
// *                                                                          *
// * You must obey the GNU General Public License in all respects for all of  *
// * the code used other than MAME, FreeFileSync, Snes9x, ePSXe.              *
// * If you modify this file, you may extend this exception to your version   *
// * of the file, but you are not obligated to do so. If you do not wish to   *
// * do so, delete this exception statement from your version.                *
// ****************************************************************************
// ****************************************************************************
// * Modified for Olduvai (2026), from xBRZ 1.9 (https://sourceforge.net/projects/xbrz/):
// * C++17 spelling ([[likely]] and the lambda's parameter list), and only the
// * unbuffered ARGB scaler is kept (no 64 MB colour-distance table, no RGB
// * path, no bilinear/nearest helpers); scale()'s ColorFormat parameter is gone.
// * Output is byte-identical to upstream's argbUnbuffered; third_party/xbrz/README.md
// * records how that was checked.
// ****************************************************************************

#ifndef XBRZ_TOOLS_H_825480175091875
#define XBRZ_TOOLS_H_825480175091875

#include <cassert>
#include <cstdint>


namespace xbrz
{
template <uint32_t N>
inline unsigned char getByte(uint32_t val)        { return static_cast<unsigned char>((val >> (8 * N)) & 0xff); }
inline unsigned char getByte(uint32_t val, int n) { return static_cast<unsigned char>((val >> (8 * n)) & 0xff); }

inline unsigned char getAlpha(uint32_t pix) { return getByte<3>(pix); }
inline unsigned char getRed  (uint32_t pix) { return getByte<2>(pix); }
inline unsigned char getGreen(uint32_t pix) { return getByte<1>(pix); }
inline unsigned char getBlue (uint32_t pix) { return getByte<0>(pix); }

inline uint32_t makePixel(uint32_t a, uint32_t r, uint32_t g, uint32_t b) { return (a << 24) | (r << 16) | (g << 8) | b; }
inline uint32_t makePixel(            uint32_t r, uint32_t g, uint32_t b) { return             (r << 16) | (g << 8) | b; }

inline
unsigned int uintDivRound(unsigned int num, unsigned int den)
{
    assert(den != 0);
    return (num + den / 2) / den;
}
}

#endif //XBRZ_TOOLS_H_825480175091875
