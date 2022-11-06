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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file LICENSE.
 * If not, see <https://www.gnu.org/licenses/>
 *
 * https://github.com/the-m-master/Moruga
 */
#include "pkz.h"
#include <cassert>
#include <climits>
#include <cstdint>
#include "Buffer.h"
#include "File.h"
#include "filter.h"
#include "gzip.h"
#include "iEncoder.h"

auto Header_t::ScanPKZ(int32_t /*ch*/) noexcept -> Filter {
  // ------------------------------------------------------------------------
  // PKZip header
  // -------+------+---------------------------------------------------------
  // offset | size | description
  // -------+------+---------------------------------------------------------
  //      0 |   4  | The signature of the local file header. This is always 'PK\x3\x4'.
  //      4 |   2  | PKZip version needed to extract
  //      6 |   2  | General purpose bits
  //        |      |   Bit 00: encrypted file
  //        |      |   Bit 01: compression option
  //        |      |   Bit 02: compression option
  //        |      |   Bit 03: data descriptor
  //        |      |   Bit 04: enhanced deflation
  //        |      |   Bit 05: compressed patched data
  //        |      |   Bit 06: strong encryption
  //        |      |   Bit 07-10: unused
  //        |      |   Bit 11: language encoding
  //        |      |   Bit 12: reserved
  //        |      |   Bit 13: mask header values
  //        |      |   Bit 14-15: reserved
  //      8 |   2  | Compression method
  //        |      |   00: no compression
  //        |      |   01: shrunk
  //        |      |   02: reduced with compression factor 1
  //        |      |   03: reduced with compression factor 2
  //        |      |   04: reduced with compression factor 3
  //        |      |   05: reduced with compression factor 4
  //        |      |   06: imploded
  //        |      |   07: reserved
  //        |      |   08: deflated
  //        |      |   09: enhanced deflated
  //        |      |   10: PKWare DCL imploded
  //        |      |   11: reserved
  //        |      |   12: compressed using BZIP2
  //        |      |   13: reserved
  //        |      |   14: LZMA
  //        |      |   15-17: reserved
  //        |      |   18: compressed using IBM TERSE
  //        |      |   19: IBM LZ77 z
  //        |      |   98: PPMd version I, Rev 1
  //     10 |   2  | File modification time
  //        |      | Stored in standard MS-DOS format:
  //        |      |   Bits 00-04: seconds divided by 2
  //        |      |   Bits 05-10: minute
  //        |      |   Bits 11-15: hour
  //     12 |   2  | File modification date
  //        |      | Stored in standard MS-DOS format:
  //        |      |   Bits 00-04: day
  //        |      |   Bits 05-08: month
  //        |      |   Bits 09-15: years from 1980
  //     14 |   4  | CRC32 value computed over file data by CRC-32 algorithm with
  //        |      | 'magic number' 0xDEBB20E3 (little endian)
  //     18 |   4  | Compressed size if archive is in ZIP64 format,
  //        |      | this filed is 0xFFFFFFFF and the length is stored in the extra field
  //     22 |   4  | Uncompressed size if archive is in ZIP64 format,
  //        |      | this filed is 0xFFFFFFFF and the length is stored in the extra field
  //     26 |   2  | File name length the length of the file name field below
  //     28 |   2  | Extra field length the length of the extra field below
  //     30 |  ... | File name the name of the file including an optional relative path.
  //        |      | All slashes in the path should be forward slashes '/'.
  //    ... |  ... | Extra field Used to store additional information.
  //        |      | The field consists of a sequence of header and data pairs,
  //        |      | where the header has a 2 byte identifier and a 2 byte data size field.

  if ((0 == _di.pkzippos) && ('PK\x3\x4' == _buf.m4(32)) && (8 == _buf.i2(32 - 8))) {
    const int32_t nlen{_buf.i2(32 - 26) + _buf.i2(32 - 28)};
    if ((nlen > 0) && (nlen < 256)) {
      _di.pkzippos = _buf.Pos() + static_cast<uint32_t>(nlen) - (_encode ? 3 : 2);
      _di.pkziplen = static_cast<int32_t>(_buf.i4(32 - 18));
      const int32_t usize{static_cast<int32_t>(_buf.i4(32 - 22))};
      if ((usize > 0) && (_di.pkziplen > 0) && (usize < _di.pkziplen)) {  // Normally compressed size is less then uncompressed size
        _di.pkzippos = 0;
        _di.pkziplen = 0;
        _di.filter_end = 0;
      } else {
        _di.offset_to_start = 0;   // start now!
        _di.filter_end = INT_MAX;  // end never..
        return Filter::PKZ;
      }
    }
  }
  return Filter::NOFILTER;
}

PKZ_filter::PKZ_filter(File_t& stream, iEncoder_t* const coder, DataInfo_t& di, const Buffer_t& __restrict buf) noexcept
    : _buf{buf},  //
      _stream{stream},
      _coder{coder},
      _di{di} {}

PKZ_filter::~PKZ_filter() noexcept = default;

auto PKZ_filter::Handle(int32_t ch) noexcept -> bool {  // encoding
  if ((_di.pkzippos > 0) && (_buf.Pos() == _di.pkzippos)) {
    const int64_t safe_pos{_stream.Position()};
    _coder->Compress(ch);  // Encode last character
    DecodeEncodeCompare(_stream, _coder, safe_pos, _di.pkziplen, 0);
    _di.pkzippos = 0;
    _di.pkziplen = 0;
    _di.filter_end = 0;
    return true;
  }
  return false;
}

auto PKZ_filter::Handle(int32_t ch, int64_t& pos) noexcept -> bool {  // decoding
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

  if ((_di.pkzippos > 0) && (_buf.Pos() == _di.pkzippos)) {
    _di.tag = 0;
    _di.pkzippos = 0;
    _stream.putc(ch);
    _block_length = 0;
    _length = 4;
    return true;
  }

  return false;
}
