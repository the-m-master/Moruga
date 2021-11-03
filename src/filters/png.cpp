/* Filter, is a binary preparation for encoding/decoding
 *
 * Copyright (c) 2019-2021 Marwijn Hessel
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
 * along with this program; see the file COPYING3.
 * If not, see <https://www.gnu.org/licenses/>
 */
#include "png.h"
#include <cassert>
#include <cinttypes>
#include <climits>
#include <cstdint>
#include "Buffer.h"
#include "File.h"
#include "filter.h"
#include "iEncoder.h"
#include "ziplib.h"

auto Header_t::ScanPNG(int32_t /*ch*/) noexcept -> Filter {
  // ------------------------------------------------------------------------
  // PNG header (https://www.fileformat.info/format/png/corion.htm)
  // -------+------+---------------------------------------------------------
  // offset | size | description
  // -------+------+---------------------------------------------------------
  //      0 |   4  | The signature of the header. This is always '%PNG\r\n^Z\n'.
  //     12 |   4  | IHDR
  //     16 |   4  | Image width in pixels
  //     20 |   4  | Image height in pixels
  //     24 |   1  | Bit Depth
  //     25 |   1  | Colour Type, indicates the method that the image uses to represent colours:
  //        |      |   0 Each pixel is a grayscale value
  //        |      |   2 Each pixel is an RGB triplet
  //        |      |   3 Each pixel is an index to a color table
  //        |      |   4 Each pixel is a grayscale value followed by an alpha mask
  //        |      |   6 Each pixel is an RGB triplet followed by an alpha mask
  //     26 |   1  | Compression Type, indicates the type of compression used to encode the image data.
  //        |      | As of Version 1.0, PNG only supports the Deflate method, so this field should be 0.
  //     27 |   1  | Filtering Type, indicating the type of filtering that was
  //        |      | applied to the data before it was compressed.
  //        |      | At this time the only type of filtering supported is an
  //        |      | adaptive filtering method described in the PNG spec,
  //        |      | so this field should be 0.
  //     28 |   1  | Interlace Scheme
  //    ... |  ... | This table summaries some properties of the standard chunk types.
  //        |      |
  //        |      | Critical chunks (must appear in this order, except PLTE is optional):
  //        |      |   Name  Multiple  Ordering constraints
  //        |      |   IHDR    No      Must be first
  //        |      |   PLTE    No      Before IDAT
  //        |      |   IDAT    Yes     Multiple IDATs must be consecutive
  //        |      |   IEND    No      Must be last
  //        |      |
  //        |      | Ancillary chunks (need not appear in this order):
  //        |      |
  //        |      |   Name  Multiple  Ordering constraints
  //        |      |   cHRM    No      Before PLTE and IDAT
  //        |      |   gAMA    No      Before PLTE and IDAT
  //        |      |   sBIT    No      Before PLTE and IDAT
  //        |      |   bKGD    No      After PLTE; before IDAT
  //        |      |   hIST    No      After PLTE; before IDAT
  //        |      |   tRNS    No      After PLTE; before IDAT
  //        |      |   pHYs    No      Before IDAT
  //        |      |   tIME    No      None
  //        |      |   tEXt    Yes     None
  //        |      |   zTXt    Yes     None

  static constexpr uint32_t offset{32};

  if ((0x89504E47u == m4(offset - 0)) && (0x0D0A1A0Au == m4(offset - 4)) && ('IHDR' == m4(offset - 12))) {
    _di.offset_to_start = 0;   // start now!
    _di.filter_end = INT_MAX;  // end never..
    return Filter::PNG;
  }

  return Filter::NOFILTER;
}

PNG_filter::PNG_filter(File_t& stream, iEncoder_t& coder, DataInfo_t& di, const Buffer_t& __restrict buf)
    : _buf{buf},  //
      _stream{stream},
      _coder{coder},
      _di{di} {}

PNG_filter::~PNG_filter() noexcept = default;

auto PNG_filter::Handle(int32_t ch) noexcept -> bool {  // encoding
  if ('IDAT' == m4(4)) {
    const uint32_t lpos{_buf.Pos() - 4};
    int32_t length = _buf[lpos - 1] | (_buf[lpos - 2] << 8) | (_buf[lpos - 3] << 16) | (_buf[lpos - 4] << 24);
    if (length > 64) {
      _di.pkzippos = 0;
      _di.pkziplen = length;
    }
  }

  if (_di.pkziplen > 0) {
    const int64_t safe_pos{_stream.Position()};
    decodeEncodeCompare(_stream, _coder, safe_pos - 1, _di.pkziplen);
    _di.pkziplen = 0;
    return true;
  }

  if ('IEND' == m4(4)) {
    _di.filter_end = 0;
  }

  _coder.Compress(ch);
  return true;
}

auto PNG_filter::Handle(int32_t ch, int64_t& pos) noexcept -> bool {  // decoding
  if (_data && (_block_length > 0)) {
    --_block_length;
    _data->putc(ch);
    if (0 == _block_length) {
      _data->Rewind();
      const bool status = encode_zlib(*_data, _data->Size(), _stream, false);
      (void)status;  // Avoid warning in release mode
      assert(status);
      delete _data;
      _data = nullptr;
      pos = _stream.Position() - 1;
    }
    return true;
  }

  if (_length > 0) {
    --_length;
    _block_length = (_block_length << 8) | ch;
    if (0 == _length) {
      if ((_DEADBEEF != uint32_t(_block_length)) && (_block_length > 0)) {
        _data = new File_t;
        pos -= _block_length;
      } else {
        _block_length = 0;
        pos = _stream.Position() - 1;
      }
    }
    return true;
  }

  if ('IDAT' == m4(4)) {
    const uint32_t lpos{_buf.Pos() - 4};
    int32_t length = _buf[lpos - 1] | (_buf[lpos - 2] << 8) | (_buf[lpos - 3] << 16) | (_buf[lpos - 4] << 24);
    if (length > 64) {
      _stream.putc(ch);
      _block_length = 0;
      _length = 4;
      return true;
    }
  }

  if ('IEND' == m4(4)) {
    _di.filter_end = 0;
  }

  _stream.putc(ch);
  return true;
}
