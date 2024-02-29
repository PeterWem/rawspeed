/*
    RawSpeed - RAW file decoder.

    Copyright (C) 2009-2014 Klaus Post
    Copyright (C) 2014-2015 Pedro Côrte-Real
    Copyright (C) 2017-2018 Roman Lebedev

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
*/

#include "decompressors/OlympusDecompressor.h"
#include "adt/Array2DRef.h"
#include "adt/Bit.h"
#include "adt/Casts.h"
#include "adt/Invariant.h"
#include "adt/Point.h"
#include "bitstreams/BitStreamerMSB.h"
#include "common/RawImage.h"
#include "common/SimpleLUT.h"
#include "decoders/RawDecoderException.h"
#include "decompressors/AbstractDecompressor.h"
#include "io/ByteStream.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <utility>

namespace rawspeed {

namespace {

class OlympusDecompressorImpl final : public AbstractDecompressor {
  RawImage mRaw;

  // A table to quickly look up "high" value
  const SimpleLUT<int8_t, 12> bittable{
      [](size_t i, [[maybe_unused]] unsigned tableSize) {
        int high;
        for (high = 0; high < 12; high++)
          if (extractHighBits(i, high, /*effectiveBitwidth=*/11) & 1)
            break;
        return std::min(12, high);
      }};

  inline __attribute__((always_inline)) int
  parseCarry(BitStreamerMSB& bits, std::array<int, 3>& carry) const;

  static inline int getPred(Array2DRef<uint16_t> out, int row, int col);

  void decompressRow(BitStreamerMSB& bits, int row) const;

public:
  explicit OlympusDecompressorImpl(RawImage img) : mRaw(std::move(img)) {}

  void decompress(ByteStream input) const;
};

/* This is probably the slowest decoder of them all.
 * I cannot see any way to effectively speed up the prediction
 * phase, which is by far the slowest part of this algorithm.
 * Also there is no way to multithread this code, since prediction
 * is based on the output of all previous pixel (bar the first four)
 */

inline __attribute__((always_inline)) int
OlympusDecompressorImpl::parseCarry(BitStreamerMSB& bits,
                                    std::array<int, 3>& carry) const {
  bits.fill();

  int nbitsBias = (carry[2] < 3) ? 2 : 0;
  int nbits = numActiveBits(implicit_cast<uint16_t>(carry[0]));
  nbits -= nbitsBias;
  nbits = std::max(nbits, 2 + nbitsBias);
  assert(nbits >= 2);
  assert(nbits <= 14);

  int b = bits.peekBitsNoFill(15);
  int sign = (b >> 14) * -1;
  int low = (b >> 12) & 3;
  int high = bittable[b & 4095];

  // Skip bytes used above or read bits
  if (high == 12) {
    bits.skipBitsNoFill(15);
    high = bits.getBitsNoFill(16 - nbits) >> 1;
  } else
    bits.skipBitsNoFill(high + 1 + 3);

  carry[0] = (high << nbits) | bits.getBitsNoFill(nbits);
  int diff = (carry[0] ^ sign) + carry[1];
  carry[1] = (diff * 3 + carry[1]) >> 5;
  carry[2] = carry[0] > 16 ? 0 : carry[2] + 1;

  return (diff * 4) | low;
}

inline int OlympusDecompressorImpl::getPred(const Array2DRef<uint16_t> out,
                                            int row, int col) {
  auto getLeft = [&]() { return out(row, col - 2); };
  auto getUp = [&]() { return out(row - 2, col); };
  auto getLeftUp = [&]() { return out(row - 2, col - 2); };

  int pred;
  if (row < 2 && col < 2)
    pred = 0;
  else if (row < 2)
    pred = getLeft();
  else if (col < 2)
    pred = getUp();
  else {
    int left = getLeft();
    int up = getUp();
    int leftUp = getLeftUp();

    int leftMinusNw = left - leftUp;
    int upMinusNw = up - leftUp;

    // Check if sign is different, and they are both not zero
    if ((std::signbit(leftMinusNw) != std::signbit(upMinusNw)) &&
        (leftMinusNw != 0 && upMinusNw != 0)) {
      if (std::abs(leftMinusNw) > 32 || std::abs(upMinusNw) > 32)
        pred = left + upMinusNw;
      else
        pred = (left + up) >> 1;
    } else
      pred = std::abs(leftMinusNw) > std::abs(upMinusNw) ? left : up;
  }

  return pred;
}

void OlympusDecompressorImpl::decompressRow(BitStreamerMSB& bits,
                                            int row) const {
  const Array2DRef<uint16_t> out(mRaw->getU16DataAsUncroppedArray2DRef());

  invariant(out.width() > 0);
  invariant(out.width() % 2 == 0);

  std::array<std::array<int, 3>, 2> acarry{{}};

  const int numGroups = out.width() / 2;
  for (int group = 0; group != numGroups; ++group) {
    for (int c = 0; c != 2; ++c) {
      const int col = 2 * group + c;
      std::array<int, 3>& carry = acarry[c];

      int diff = parseCarry(bits, carry);
      int pred = getPred(out, row, col);

      out(row, col) = implicit_cast<uint16_t>(pred + diff);
    }
  }
}

void OlympusDecompressorImpl::decompress(ByteStream input) const {
  invariant(mRaw->dim.y > 0);
  invariant(mRaw->dim.x > 0);
  invariant(mRaw->dim.x % 2 == 0);

  input.skipBytes(7);
  BitStreamerMSB bits(input.peekRemainingBuffer().getAsArray1DRef());

  for (int y = 0; y < mRaw->dim.y; y++)
    decompressRow(bits, y);
}

} // namespace

OlympusDecompressor::OlympusDecompressor(RawImage img) : mRaw(std::move(img)) {
  if (mRaw->getCpp() != 1 || mRaw->getDataType() != RawImageType::UINT16 ||
      mRaw->getBpp() != sizeof(uint16_t))
    ThrowRDE("Unexpected component count / data type");

  const uint32_t w = mRaw->dim.x;
  const uint32_t h = mRaw->dim.y;

  if (w == 0 || h == 0 || w % 2 != 0 || h % 2 != 0 || w > 10400 || h > 7792)
    ThrowRDE("Unexpected image dimensions found: (%u; %u)", w, h);
}

void OlympusDecompressor::decompress(ByteStream input) const {
  OlympusDecompressorImpl(mRaw).decompress(input);
}

} // namespace rawspeed
