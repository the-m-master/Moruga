/* Filter, is a binary preparation for encoding/decoding
 *
 * Copyright (c) 2019-2023 Marwijn Hessel
 *
 * Moruga is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Moruga is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file LICENSE.
 * If not, see <https://www.gnu.org/licenses/>
 *
 * https://github.com/the-m-master/Moruga
 */
#include "gzp.h"
#include <cassert>
#include <climits>
#include <cstdint>
#include "Buffer.h"
#include "File.h"
#include "filter.h"
#include "gzip.h"
#include "iEncoder.h"

// GZ header
// 1F 8B 08 08 9E FF 94 61 02 03 77 6F 72 6C 64 39   .......a..world9
// 35 2E 74 78 74 00 DC FD 6B 73 DB 48 B3 2E 0A 7E   5.txt...ks.H...~
//                   ^^ ^^ ^^ ^^ ^^ ^^ ^^ ^^ ^^ ^^
// PKZip header
// 50 4B 03 04 14 00 02 00 08 00 7D 71 71 53 94 8A   PK........}qqS..
// 09 35 CD 2A 0D 00 22 9A 2D 00 0B 00 1C 00 77 6F   .5.*..".-.....wo
// 72 6C 64 39 35 2E 74 78 74 55 54 09 00 03 9E FF   rld95.txtUT.....
// 94 61 61 F5 A9 61 75 78 0B 00 01 04 EA 03 03 00   .aa..aux........
// 04 01 02 03 00 DC FD 6B 73 DB 48 B3 2E 0A 7E 1E   .......ks.H...~.
//                ^^ ^^ ^^ ^^ ^^ ^^ ^^ ^^ ^^ ^^ ^^

auto Header_t::ScanGZP(int32_t /*ch*/) noexcept -> Filter {
  // ------------------------------------------------------------------------
  // GZ header (https://datatracker.ietf.org/doc/html/rfc1952)
  // ------------------------------------------------------------------------
  //
  // Each member has the following structure:
  //    +---+---+---+---+---+---+---+---+---+---+
  //    |ID1|ID2|CM |FLG|     MTIME     |XFL|OS | (more-->)
  //    +---+---+---+---+---+---+---+---+---+---+
  //
  // (if FLG.FEXTRA set)
  //
  //    +---+---+=================================+
  //    | XLEN  |...XLEN bytes of "extra field"...| (more-->)
  //    +---+---+=================================+
  //
  // (if FLG.FNAME set)
  //
  //    +=========================================+
  //    |...original file name, zero-terminated...| (more-->)
  //    +=========================================+
  //
  // (if FLG.FCOMMENT set)
  //
  //    +===================================+
  //    |...file comment, zero-terminated...| (more-->)
  //    +===================================+
  //
  // (if FLG.FHCRC set)
  //
  //    +---+---+
  //    | CRC16 |
  //    +---+---+
  //
  //    +=======================+
  //    |...compressed blocks...| (more-->)
  //    +=======================+
  //
  //      0   1   2   3   4   5   6   7
  //    +---+---+---+---+---+---+---+---+
  //    |     CRC32     |     ISIZE     |
  //    +---+---+---+---+---+---+---+---+
  //
  // -------+------+---------------------------------------------------------
  // offset | size | description
  // -------+------+---------------------------------------------------------
  //      0 |   2  | The signature of the local file header. Always: '\x1F\x8B'
  //      2 |   1  | Compression Method * 0-7 (Reserved) * 8 (Deflate)
  //      3 |   1  | File flags (FLG):
  //        |      |   Bit 00: FTEXT    If set the uncompressed data needs to be treated as text instead of binary data.
  //        |      |                    This flag hints end-of-line conversion for cross-platform text files but does not enforce it.
  //        |      |   Bit 01: FHCRC    The file contains a header checksum (CRC-16)
  //        |      |   Bit 02: FEXTRA   The file contains extra fields
  //        |      |   Bit 03: FNAME    The file contains an original file name string (zero-terminated)
  //        |      |   Bit 04: FCOMMENT The file contains comment (zero-terminated)
  //        |      |   Bit 05: reserved
  //        |      |   Bit 06: reserved
  //        |      |   Bit 07: reserved
  //      4 |   4  | Modification TIME (MTIME)
  //      8 |   1  | Compression flags (CM)
  //      9 |   1  | Operating system ID (OS)
  //
  // ------------------------------------------------------------------------
  // GZ footer
  // -------+------+---------------------------------------------------------
  // offset | size | description
  // -------+------+---------------------------------------------------------
  //     0  |   4  | Checksum (CRC-32)
  //     4  |   4  | Uncompressed data size value in bytes
  //        |      |

  static constexpr uint32_t offset{9};

  const auto header{_buf.m4(offset - 0)};
  if (UINT32_C(0x1F8B0800) == ((header >> 8) << 8)) {
    const auto cm{_buf(offset - 8)};
    if ((0 == cm) || (2 == cm) || (4 == cm)) {
      _di.flags = _buf(offset - 3);
      _di.offset_to_start = 0;   // start now!
      _di.filter_end = INT_MAX;  // end never..
      return Filter::GZP;
    }
  }

  return Filter::NOFILTER;
}

GZP_filter::GZP_filter(File_t& stream, iEncoder_t* const coder, DataInfo_t& di, const int64_t original_length) noexcept
    : _original_length{original_length},  //
      _stream{stream},
      _coder{coder},
      _di{di} {}

GZP_filter::~GZP_filter() noexcept = default;

auto GZP_filter::Handle_GZ_flags(int32_t ch) noexcept -> bool {
  // ------------------------------------------------------------------------
  // The file contains extra fields
  // ------------------------------------------------------------------------
  if (_di.flags & (1 << 2)) {
    _extra_field_length = (_extra_field_length * 10u) + static_cast<uint32_t>(ch);
    if (1 == _state) {
      _state = 0;
      _di.flags = static_cast<uint8_t>(_di.flags & ~(1u << 2));
    } else {
      _state = 1;
    }
    return false;
  }
  if (_extra_field_length > 0) {
    --_extra_field_length;
    return false;
  }

  // ------------------------------------------------------------------------
  // The file contains an original file name string (zero-terminated)
  // ------------------------------------------------------------------------
  if (_di.flags & (1 << 3)) {
    if (0 == ch) {
      _di.flags = static_cast<uint8_t>(_di.flags & ~(1u << 3));
    } else {
      return false;
    }
  }

  // ------------------------------------------------------------------------
  // The file contains comment (zero-terminated)
  // ------------------------------------------------------------------------
  if (_di.flags & (1 << 4)) {
    if (0 == ch) {
      _di.flags = static_cast<uint8_t>(_di.flags & ~(1u << 4));
    } else {
      return false;
    }
  }

  return true;
}

auto GZP_filter::Handle(int32_t ch) noexcept -> bool {  // encoding
  if (Handle_GZ_flags(ch)) {
    const int64_t safe_pos{_stream.Position()};
    _coder->Compress(ch);  // Encode last character
    DecodeEncodeCompare(_stream, _coder, safe_pos, _original_length, 0);
    _di.pkziplen = 0;
    _di.filter_end = 0;
    return true;
  }
  return false;
}

auto GZP_filter::Handle(int32_t ch, int64_t& pos) noexcept -> bool {  // decoding
  if (Handle_GZ_flags(ch)) {
    if (_data && (_block_length > 0)) {
      --_block_length;
      _data->putc(ch);
      if (0 == _block_length) {
        _data->Rewind();
        const bool status{EncodeGZip(*_data, _data->Size(), _stream)};
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
        if ((_DEADBEEF != static_cast<uint32_t>(_block_length)) && (_block_length > 0)) {
          _data = new File_t /*("_GZP_filter_.bin", "wb+")*/;
          pos -= _block_length;
        } else {
          _block_length = 0;
          pos = _stream.Position() - 1;
          _di.filter_end = 0;
        }
      }
      return true;
    }

    _di.tag = 0;
    _stream.putc(ch);
    _block_length = 0;
    _length = 4;
    return true;
  }

  return false;
}
