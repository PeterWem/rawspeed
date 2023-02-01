/*
    RawSpeed - RAW file decoder.

    Copyright (C) 2017 Uwe Müssel
    Copyright (C) 2017 Roman Lebedev

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
*/

#pragma once

#include "adt/Array2DRef.h"                     // for Array2DRef
#include "common/RawImage.h"                    // for RawImage
#include "decompressors/AbstractDecompressor.h" // for AbstractDecompressor
#include "io/BitPumpMSB.h"                      // for BitPumpMSB
#include "io/ByteStream.h"                      // for ByteStream
#include "metadata/ColorFilterArray.h"          // for CFAColor
#include <array>                                // for array
#include <cassert>                              // for assert
#include <cstdint>                              // for int8_t, uint16_t
#include <utility>                              // for move
#include <vector>                               // for vector

namespace rawspeed {

class FujiDecompressor final : public AbstractDecompressor {
  RawImage mRaw;

  void decompressThread() const noexcept;

public:
  FujiDecompressor(const RawImage& img, ByteStream input);

  void decompress() const;

  struct FujiHeader {
    FujiHeader() = default;

    explicit FujiHeader(ByteStream& input_);
    explicit __attribute__((pure)) operator bool() const; // validity check

    uint16_t signature;
    uint8_t version;
    uint8_t raw_type;
    uint8_t raw_bits;
    uint16_t raw_height;
    uint16_t raw_rounded_width;
    uint16_t raw_width;
    uint16_t block_size;
    uint8_t blocks_in_row;
    uint16_t total_lines;
    iPoint2D MCU;
  };

private:
  FujiHeader header;

  struct FujiStrip {
    // part of which 'image' this block is
    const FujiHeader& h;

    // which strip is this, 0 .. h.blocks_in_row-1
    const int n;

    // the compressed data of this strip
    const ByteStream bs;

    FujiStrip() = delete;
    FujiStrip(const FujiStrip&) = default;
    FujiStrip(FujiStrip&&) noexcept = default;
    FujiStrip& operator=(const FujiStrip&) noexcept = delete;
    FujiStrip& operator=(FujiStrip&&) noexcept = delete;

    FujiStrip(const FujiHeader& h_, int block, ByteStream bs_)
        : h(h_), n(block), bs(std::move(bs_)) {
      assert(n >= 0 && n < h.blocks_in_row);
    }

    // each strip's line corresponds to 6 output lines.
    static int lineHeight() { return 6; }

    // how many vertical lines does this block encode?
    [[nodiscard]] int height() const { return h.total_lines; }

    // how many horizontal pixels does this block encode?
    [[nodiscard]] int width() const {
      // if this is not the last block, we are good.
      if ((n + 1) != h.blocks_in_row)
        return h.block_size;

      // ok, this is the last block...

      assert(h.block_size * h.blocks_in_row >= h.raw_width);
      return h.raw_width - offsetX();
    }

    // how many horizontal pixels does this block encode?
    [[nodiscard]] iPoint2D numMCUs(iPoint2D MCU) const {
      assert(width() % MCU.x == 0);
      assert(lineHeight() % MCU.y == 0);
      return {width() / MCU.x, lineHeight() / MCU.y};
    }

    // where vertically does this block start?
    [[nodiscard]] int offsetY(int line = 0) const {
      (void)height(); // A note for NDEBUG builds that *this is used.
      assert(line >= 0 && line < height());
      return lineHeight() * line;
    }

    // where horizontally does this block start?
    [[nodiscard]] int offsetX() const { return h.block_size * n; }
  };

  void fuji_compressed_load_raw();

  struct fuji_compressed_params {
    fuji_compressed_params() = default;

    explicit fuji_compressed_params(const FujiDecompressor& d);

    std::vector<int8_t> q_table; /* quantization table */
    std::array<int, 5> q_point; /* quantization points */
    int max_bits;
    int min_value;
    int raw_bits;
    int total_values;
    int maxDiff;
    uint16_t line_width;
  };

  fuji_compressed_params common_info;

  struct int_pair {
    int value1;
    int value2;
  };

  enum xt_lines {
    R0 = 0,
    R1,
    R2,
    R3,
    R4,
    G0,
    G1,
    G2,
    G3,
    G4,
    G5,
    G6,
    G7,
    B0,
    B1,
    B2,
    B3,
    B4,
    ltotal
  };

  struct fuji_compressed_block {
    fuji_compressed_block() = default;

    void reset(const fuji_compressed_params& params);

    BitPumpMSB pump;

    // tables of gradients
    std::array<std::array<int_pair, 41>, 3> grad_even;
    std::array<std::array<int_pair, 41>, 3> grad_odd;

    std::vector<uint16_t> linealloc;
    Array2DRef<uint16_t> lines;
  };

  ByteStream input;

  std::vector<ByteStream> strips;

  void fuji_decode_strip(fuji_compressed_block& info_block,
                         const FujiStrip& strip) const;

  template <typename Tag, typename T>
  void copy_line(fuji_compressed_block& info, const FujiStrip& strip,
                 int cur_line, T&& idx) const;

  void copy_line_to_xtrans(fuji_compressed_block& info, const FujiStrip& strip,
                           int cur_line) const;
  void copy_line_to_bayer(fuji_compressed_block& info, const FujiStrip& strip,
                          int cur_line) const;

  static inline int fuji_zerobits(BitPumpMSB& pump);
  static int bitDiff(int value1, int value2);

  template <typename T>
  inline void fuji_decode_sample(T&& func, fuji_compressed_block& info,
                                 xt_lines c, int pos,
                                 std::array<int_pair, 41>& grads) const;
  inline void fuji_decode_sample_even(fuji_compressed_block& info, xt_lines c,
                                      int pos,
                                      std::array<int_pair, 41>& grads) const;
  inline void fuji_decode_sample_odd(fuji_compressed_block& info, xt_lines c,
                                     int pos,
                                     std::array<int_pair, 41>& grads) const;

  [[nodiscard]] inline std::pair<int, int>
  fuji_decode_interpolation_even_inner(const fuji_compressed_block& info,
                                       xt_lines c, int pos) const;
  [[nodiscard]] inline std::pair<int, int>
  fuji_decode_interpolation_odd_inner(const fuji_compressed_block& info,
                                      xt_lines c, int pos) const;
  inline void fuji_decode_interpolation_even(fuji_compressed_block& info,
                                             xt_lines c, int pos) const;

  static void fuji_extend_generic(const fuji_compressed_block& info, int start,
                                  int end);
  static void fuji_extend_red(const fuji_compressed_block& info);
  static void fuji_extend_green(const fuji_compressed_block& info);
  static void fuji_extend_blue(const fuji_compressed_block& info);

  template <typename T>
  inline void fuji_decode_block(T&& func_even, fuji_compressed_block& info,
                                int cur_line) const;
  void xtrans_decode_block(fuji_compressed_block& info, int cur_line) const;
  void fuji_bayer_decode_block(fuji_compressed_block& info, int cur_line) const;
};

} // namespace rawspeed
