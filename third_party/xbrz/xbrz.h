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
// * path, no bilinear/nearest helpers); scale()'s ColorFormat parameter is gone and
// * it takes a DistancePath (an exact alternative for the alpha part of the distance).
// * Output is byte-identical to upstream's argbUnbuffered; third_party/xbrz/README.md
// * records how that was checked.
// ****************************************************************************

#ifndef XBRZ_HEADER_3847894708239054
#define XBRZ_HEADER_3847894708239054

#include <cstddef> //size_t
#include <cstdint> //uint32_t
#include <limits>
#include "xbrz_config.h"


namespace xbrz
{
/*  -------------------------------------------------------------------------
    | xBRZ: "Scale by rules" - high quality image upscaling filter by Zenju |
    -------------------------------------------------------------------------
    using a modified approach of xBR: https://forums.libretro.com/t/xbr-algorithm-tutorial/123
    - new rule set preserving small image features
    - highly optimized for performance
    - support alpha channel
    - support multithreading
    - support 64-bit architectures
    - support processing image slices
    - support scaling up to 6xBRZ                                             */

const int SCALE_FACTOR_MAX = 6;

//How the colour distance treats alpha.  Both give the same pixels, bit for bit; they differ in speed,
//by CPU.  Reference is upstream's: two divisions by 255 per call.  AlphaTable skips them for opaque
//pairs and reads the rest from a table.  Measured on a 320x200 frame: 14% faster on a Cortex-A7,
//38% slower on a Cortex-A53, no difference on an M-series core, hence the default below.
enum class DistancePath { Reference, AlphaTable };
#if defined(__arm__) && !defined(__aarch64__)
constexpr DistancePath DEFAULT_DISTANCE_PATH = DistancePath::AlphaTable;
#else
constexpr DistancePath DEFAULT_DISTANCE_PATH = DistancePath::Reference;
#endif

/*
-> map source (srcWidth * srcHeight) to target (scale * width x scale * height) image, optionally processing a half-open slice of rows [yFirst, yLast) only
-> if your emulator changes only a few image slices during each cycle (e.g. DOSBox) then there's no need to run xBRZ on the complete image:
   Just make sure you enlarge the source image slice by 2 rows on top and 2 on bottom (this is the additional range the xBRZ algorithm is using during analysis)
   CAVEAT: If there are multiple changed slices, make sure they do not overlap after adding these additional rows in order to avoid a memory race condition
   in the target image data if you are using multiple threads for processing each enlarged slice!

THREAD-SAFETY: - parts of the same image may be scaled by multiple threads as long as the [yFirst, yLast) ranges do not overlap!
               - there is a minor inefficiency for the first row of a slice, so avoid processing single rows only; suggestion: process at least 8-16 rows
*/
void scale(size_t factor, //valid range: 2 - SCALE_FACTOR_MAX
           const uint32_t* src, uint32_t* trg, int srcWidth, int srcHeight, //ARGB, alpha respected
           const ScalerCfg& cfg = ScalerCfg(),
           int yFirst = 0, int yLast = std::numeric_limits<int>::max(), //slice of source image
           DistancePath path = DEFAULT_DISTANCE_PATH);
}

#endif
