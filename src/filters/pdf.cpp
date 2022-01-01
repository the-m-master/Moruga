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
#include "pdf.h"
#include <cassert>
#include <climits>
#include <cstdint>
#include <cstdio>
#include "File.h"
#include "IntegerXXL.h"
#include "filter.h"
#include "iEncoder.h"
#include "ziplib.h"

/* A stream shall consist of a dictionary followed by zero or more bytes
 * bracketed between the keywords stream (followed by newline) and endstream:
 *
 * EXAMPLE dictionary
 *         stream
 *         ...Zero or more bytes..
 *         endstream
 *
 * All streams shall be indirect objects and the stream dictionary shall
 * be a direct object. The keyword stream that follows the stream dictionary
 * shall be followed by an end-of-line marker consisting of either a
 * CARRIAGE RETURN and a LINE FEED or just a LINE FEED, and not by a
 * CARRIAGE RETURN alone. The sequence of bytes that make up a stream lie
 * between the end-of-line marker following the stream keyword and the
 * endstream keyword; the stream dictionary specifies the exact number of bytes.
 * There should be an end-of-line marker after the data and before endstream;
 * this marker shall not be included in the stream length.
 * There shall not be any extra bytes, other than white space,
 * between endstream and endobj
 */
// clang-format off
static constexpr uint128_t stream0A       {0x00000073747265616D0A_xxl}; // 'stream\n'
static constexpr uint128_t stream0A_mask  {0x000000FFFFFFFFFFFFFF_xxl};

static constexpr uint128_t stream0D0A     {0x000073747265616D0D0A_xxl}; // 'stream\r\n'
static constexpr uint128_t stream0D0A_mask{0x0000FFFFFFFFFFFFFFFF_xxl};

static constexpr uint128_t endstream0A    {0x656E6473747265616D0A_xxl}; // 'endstream\n'
static constexpr uint128_t endstream0D    {0x656E6473747265616D0D_xxl}; // 'endstream\r'
static constexpr uint128_t endstream_mask {0xFFFFFFFFFFFFFFFFFFFF_xxl};

static constexpr uint128_t endobj0A       {0x000000656E646F626A0A_xxl}; // 'endobj\n'
static constexpr uint128_t endobj0D       {0x000000656E646F626A0D_xxl}; // 'endobj\r'
static constexpr uint128_t endobj_mask    {0x000000FFFFFFFFFFFFFF_xxl};
// clang-format on

auto Header_t::ScanPDF(int32_t ch) noexcept -> Filter {
  _di.tag = (_di.tag << 8) | uint128_t(ch);
  if (((stream0A == (stream0A_mask & _di.tag)) || (stream0D0A == (stream0D0A_mask & _di.tag))) &&  //
      (endstream0A != (endstream_mask & _di.tag)) && (endstream0D != (endstream_mask & _di.tag))) {
    _di.tag = 0;
    _di.offset_to_start = 0;   // start now!
    _di.filter_end = INT_MAX;  // end never..
    return Filter::PDF;
  }

  return Filter::NOFILTER;
}

PDF_filter::PDF_filter(File_t& stream, iEncoder_t* const coder, DataInfo_t& di, const Buffer_t& __restrict buf)
    : _buf{buf},  //
      _stream{stream},
      _coder{coder},
      _di{di} {}

PDF_filter::~PDF_filter() noexcept = default;

auto PDF_filter::Handle(int32_t ch) noexcept -> bool {  // encoding
  const int64_t safe_pos{_stream.Position()};

  int64_t block_length{0};
  int32_t c;
  while (EOF != (c = _stream.getc())) {
    _di.tag = (_di.tag << 8) | uint128_t(c);
    if ((endobj0A == (endobj_mask & _di.tag)) ||                                                     //
        (endobj0D == (endobj_mask & _di.tag)) ||                                                     //
        ((stream0A == (stream0A_mask & _di.tag)) && (endstream0A != (endstream_mask & _di.tag))) ||  //
        ((stream0D0A == (stream0D0A_mask & _di.tag)) && (endstream0D != (endstream_mask & _di.tag)))) {
      // This should not happen, but to be on the safe side...
      break;
    }
    if (endstream0A == (endstream_mask & _di.tag)) {
      const int64_t pos{_stream.Position()};
      block_length = pos - safe_pos - 10;  // sub 10 bytes length of endstream
      break;
    }
    if (endstream0D == (endstream_mask & _di.tag)) {
      const int64_t pos{_stream.Position()};
      block_length = pos - safe_pos - 11;  // sub 10 bytes length of endstream, and one extra
      break;
    }
  }
  _di.tag = 0;
  _di.filter_end = 0;

  _coder->Compress(ch);  // Encode last character
  decodeEncodeCompare(_stream, _coder, safe_pos, block_length);
  return true;
}

auto PDF_filter::Handle(int32_t ch, int64_t& pos) noexcept -> bool {  // decoding
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
      _di.filter_end = 0;
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
        _di.filter_end = 0;
      }
    }
    return true;
  }

  _block_length = ch;
  _length = 4 - 1;
  return true;
}
