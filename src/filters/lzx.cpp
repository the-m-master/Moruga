/* Filter, is a binary preparation for encoding/decoding
 *
 * Copyright (c) 2019-2022 Marwijn Hessel
 *
 * Moruga is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Moruga is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file LICENSE.
 * If not, see <https://www.gnu.org/licenses/>
 */
#include "lzx.h"
#include <climits>
#include "filter.h"

// 0 43585A4C
// 4 00000002
// 8 00000002
// 12 00000002
// 16 00000001
// 20 00000000
// 24 00000002
// 28 000000D3
// 32 00000008
// 36 00000028
// 40 006911CD
// 44 00000000
// 48 0047DC22
// 52 00000000

auto Header_t::ScanLZX(int32_t /*ch*/) noexcept -> Filter {
  // ------------------------------------------------------------------------
  // CHM/LZX header (http://www.russotto.net/chm/chmformat.html)
  // -------+------+---------------------------------------------------------
  // offset | size | description
  // -------+------+---------------------------------------------------------
  //      0 |   4  | Compression type identifier, always: 'LZXC'
  //      4 |   4  | Version (2 or 3)
  //      8 |   4  | Reset Interval Bits (0..16)
  //     12 |   4  | Window Size Bits (0..16)
  //     16 |   4  | Cache Size
  //     20 |   4  | (unknown)
  //     24 |   4  | (unknown)
  //     28 |   4  | Number of entries in reset table
  //     32 |   4  | Size of table entry (bytes)
  //     36 |   4  | Length of table header (area before table entries)
  //     40 |   8  | Uncompressed length
  //     48 |   8  | Compressed length
  //
  static constexpr uint32_t offset{56};

  if (('LZXC' == m4(offset - 0)) && (0x28 == i4(offset - 36))) {
    // uint64_t ulength{i8(offset - 40)};
    // uint64_t clength{i8(offset - 48)};
    // for (uint32_t n{0}; n < offset; n += 4) {
    //   const uint32_t var{i4(offset - n)};
    //   fprintf(stderr, "%u %08X\n", n, var);
    // }
    // fprintf(stderr, "%" PRIx64 "\n", ulength);
    // fprintf(stderr, "%" PRIx64 "\n", clength);
    // fflush(stderr);

    const uint32_t resetIntervalBits{i4(offset - 8)};  // 2
    const uint32_t windowSizeBits{i4(offset - 12)};    // 2
    if ((resetIntervalBits <= 16) && (windowSizeBits <= 16)) {
      _di.resetIntervalBits = (1u << 15) << resetIntervalBits;
      _di.windowSizeBits = uint8_t(15 + windowSizeBits);
      _di.offset_to_start = 0;   // start now!
      _di.filter_end = INT_MAX;  // end never..
      return Filter::LZX;
    }
  }

  return Filter::NOFILTER;
}

LZX_filter::LZX_filter(File_t& stream, iEncoder_t* const coder, DataInfo_t& di) : _stream{stream}, _coder{coder}, _di{di} {}

LZX_filter::~LZX_filter() noexcept = default;

auto LZX_filter::Handle(int32_t /*ch*/) noexcept -> bool {  // encoding
  if (_di.offset_to_start > 0) {
    --_di.offset_to_start;
    return false;
  }
  _di.filter_end = 0;  // end never..

  // TODO finish this ...

  return false;
}

auto LZX_filter::Handle(int32_t /*ch*/, int64_t& /*pos*/) noexcept -> bool {  // decoding
  return false;
}
