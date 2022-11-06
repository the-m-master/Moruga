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
#include "cab.h"
#include <cassert>
#include <climits>
#include <cstdint>
#include <cstdio>
#include "File.h"
#include "filter.h"
#include "gzip.h"
#include "iEncoder.h"

auto Header_t::ScanCAB(int32_t /*ch*/) noexcept -> Filter {
  // ------------------------------------------------------------------------
  // CAB header
  //   https://docs.microsoft.com/en-us/previous-versions//bb267310(v=vs.85)?redirectedfrom=MSDN
  //   https://wiki.xentax.com/index.php/CAB_Archive
  // -------+------+---------------------------------------------------------
  // offset | size | description
  // -------+------+---------------------------------------------------------
  //      0 |   4  | Signature, must be 'MSCF'
  //      4 |   4  | Reserved, must be zero
  //      8 |   4  | Size of this cabinet file in bytes
  //     12 |   4  | Reserved, must be zero
  //     16 |   4  | Offset of the first CFFILE entry
  //     20 |   4  | Reserved, must be zero
  //     24 |   1  | Cabinet file format version, minor
  //     25 |   1  | Cabinet file format version, major
  //     26 |   2  | Number of CFFOLDER entries in this cabinet
  //     28 |   2  | Number of CFFILE entries in this cabinet
  //     30 |   2  | Cabinet file option indicators (cflags)
  //     32 |   2  | setID, must be the same for all cabinets in a set
  //     34 |   2  | Number of this cabinet file in a set
  //     36 |   2  | [optional] Size of per-cabinet reserved area
  //     38 |   1  | [optional] Size of per-folder reserved area
  //     39 |   1  | [optional] Size of per-datablock reserved area
  //     40 |   1  | [optional] Name of previous cabinet file
  //    ... |   1  | [optional] Name of previous disk
  //    ... |   1  | [optional] Name of next cabinet file
  //    ... |   1  | [optional] Name of next disk

#if 1
  static constexpr uint32_t offset{36};  // 0x24

  const auto sig{_buf.m8(offset - 0)};
  if ((UINT64_C(0x4D53434600000000) == sig)) {
    const auto size{_buf.i4(offset - 8)};  // cfhead_CabinetSize
    const auto entry{_buf.i4(offset - 16)};
    const auto version{_buf.i2(offset - 24)};
    const auto folders{_buf.i2(offset - 26)};
    const auto files{_buf.i2(offset - 28)};
    if ((size > 0) && (entry > 0) && (0x0103 == version) && !_buf.i4(offset - 12) && !_buf.i4(offset - 20) && (folders > 0) && (files > 0)) {
      _di.cfolders = folders;
      _di.cfiles = files;
      _di.cflags = _buf.i2(offset - 30);
      _di.offset_to_start = 0;
      _di.filter_end = INT_MAX;  // end never..

      return Filter::CAB;
    }
  }
#endif

  return Filter::NOFILTER;
}

CAB_filter::CAB_filter(File_t& stream, iEncoder_t* const coder, DataInfo_t& di) noexcept
    : _stream{stream},  //
      _coder{coder},
      _di{di} {}

CAB_filter::~CAB_filter() noexcept = default;

auto CAB_filter::Handle(int32_t ch) noexcept -> bool {  // encoding
  if (skip_counter_ > 0) {
    --skip_counter_;
    return false;
  }

  if (4 & _di.cflags) {
    // uint16 {2}       - HeaderReserveSize
    // byte {1}         - FolderReserveSize
    // byte {1}         - FileReserveSize
    // if (HeaderReserveSize != 0){
    //   byte {X}       - ReserveData (length = HeaderReserveSize)
    // }
    if (0 == byte_counter_) {
      byte_counter_ = 4;
    }
    --byte_counter_;
    switch (byte_counter_) {
      case 3:
      case 2:
        reserveHeader_.headerReserveSize >>= 8;
        reserveHeader_.headerReserveSize |= uint16_t(ch << 8);
        break;
      case 1:
        reserveHeader_.folderReserveSize = uint8_t(ch);
        break;
      case 0:
      default:
        reserveHeader_.fileReserveSize = uint8_t(ch);
        _di.cflags &= uint16_t(~4);
        break;
    }
    return false;
  }

  if (1 & _di.cflags) {
    // byte {0-255}     - Previous CAB Filename
    // byte {1}         - null Filename Terminator
    // byte {0-255}     - Previous Disk Name
    // byte {1}         - null Disk Name Terminator
    if (0 != cname_) {
      if (0 == ch) {
        --cname_;
        if (0 == cname_) {
          _di.cflags &= uint16_t(~1);
        }
      }
      return false;
    }
    cname_ = 2;
    return false;
  }

  if (2 & _di.cflags) {
    // byte {0-255}     - Next CAB Filename
    // byte {1}         - null Filename Terminator
    // byte {0-255}     - Next Disk Name
    // byte {1}         - null Disk Name Terminator
    if (0 != cname_) {
      if (0 == ch) {
        --cname_;
        if (0 == cname_) {
          _di.cflags &= uint16_t(~2);
        }
      }
      return false;
    }
    cname_ = 2;
    return false;
  }

  if (cfolder_ < _di.cfolders) {
    // uint32 {4}       - Offset to the first CFDATA in this Folder
    // uint16 {2}       - Number of CFDATA blocks in this Folder
    // uint16 {2}       - Compression Format for each CFDATA in this Folder (0 = NONE, 1 = MSZIP, 2 = QUANTUM, 3 = LZX)
    // if (flags & 4) {
    //   byte {X}       - Reserve Data (length = FolderReserveSize)
    // }
    if (0 == byte_counter_) {
      byte_counter_ = 8;
    }
    --byte_counter_;
    switch (byte_counter_) {
      case 7:
      case 6:
      case 5:
      case 4:
        folderHeader_.offset >>= 8;
        folderHeader_.offset |= uint32_t(ch << 24);
        break;
      case 3:
      case 2:
        folderHeader_.nBlocks >>= 8;
        folderHeader_.nBlocks |= uint16_t(ch << 8);
        break;
      case 1:
      case 0:
      default:
        folderHeader_.format >>= 8;
        folderHeader_.format |= uint16_t(ch << 8);
        if (0 == byte_counter_) {
          if (1 != folderHeader_.format) {  // For now MSZIP only
            _di.filter_end = 0;             // Wrong header end filter
          }
          skip_counter_ = reserveHeader_.headerReserveSize;
          ++cfolder_;
        }
        break;
    }
    return false;
  }

  if (cfile_ < _di.cfiles) {
    // uint32 {4}       - Uncompressed File Length
    // uint32 {4}       - Offset in the Uncompressed CFDATA for the Folder this file belongs to (relative to the start of the Uncompressed CFDATA for this Folder)
    // uint16 {2}       - Folder ID (starts at 0)
    // uint16 {2}       - File Date
    // uint16 {2}       - File Time
    // uint16 {2}       - File Attributes
    // if (FileAttributes & 64) {
    //   byte {X}       - Filename (Unicode)
    //   byte {1}       - null Filename Terminator
    // } else {
    //   byte {X}       - Filename (ASCII)
    //   byte {1}       - null Filename Terminator
    // }
    if (0 != cname_) {
      if (0 == ch) {
        --cname_;
        if (0 == cname_) {
          ++cfile_;
        }
      }
      fileHeader_.name.push_back(static_cast<char>(ch));
      return false;
    }
    if (0 == byte_counter_) {
      byte_counter_ = 16;
      cname_ = 0;
      fileHeader_.name.clear();
    }
    --byte_counter_;
    switch (byte_counter_) {
      case 15:
      case 14:
      case 13:
      case 12:
        fileHeader_.length >>= 8;
        fileHeader_.length |= uint32_t(ch << 24);
        break;
      case 11:
      case 10:
      case 9:
      case 8:
        fileHeader_.offset >>= 8;
        fileHeader_.offset |= uint32_t(ch << 24);
        break;
      case 7:
      case 6:
        fileHeader_.id >>= 8;
        fileHeader_.id |= uint16_t(ch << 8);
        break;
      case 5:
      case 4:
        fileHeader_.date >>= 8;
        fileHeader_.date |= uint16_t(ch << 8);
        break;
      case 3:
      case 2:
        fileHeader_.time >>= 8;
        fileHeader_.time |= uint16_t(ch << 8);
        break;
      case 1:
      case 0:
      default:
        fileHeader_.attributes >>= 8;
        fileHeader_.attributes |= uint16_t(ch << 8);
        if (0 == byte_counter_) {
          cname_ = 1;
        }
        break;
    }
    return false;
  }

  if (nBlocks_ < folderHeader_.nBlocks) {
    //  for each CFDATA
    //    uint32 {4}       - Checksum
    //    uint16 {2}       - Compressed Data Length
    //    uint16 {2}       - Uncompressed Data Length
    //    if (flags & 4) {
    //      byte {X}       - Reserve Data (length = FileReserveSize)
    //    }
    //    byte {X}         - Compressed Data
    if (0 == byte_counter_) {
      byte_counter_ = 8 + 2;
    }
    --byte_counter_;
    switch (byte_counter_) {
      case 9:
      case 8:
      case 7:
      case 6:
        data_.crc >>= 8;
        data_.crc |= uint16_t(ch << 8);
        break;
      case 5:
      case 4:
        data_.compressedDataLength >>= 8;
        data_.compressedDataLength |= uint16_t(ch << 8);
        break;
      case 3:
      case 2:
        data_.uncompressedDataLength >>= 8;
        data_.uncompressedDataLength |= uint16_t(ch << 8);
        break;
      case 1:
      case 0:
      default:
        // MSZIP COMPRESSION
        // for each compressed block (uncompressed size of the block is at most 32k)
        //   uint16 {2}      - Zip Header (43h, 4Bh)
        //   byte {X}        - Compressed Data (Deflate)
        header_ <<= 8;
        header_ |= uint16_t(ch);
        if (0 == byte_counter_) {
          if ('CK' == header_) {
            const int64_t safe_pos{_stream.Position()};
#if 0
            {
              static uint32_t n_blob{0};
              char tmp[128];
              snprintf(tmp, sizeof(tmp), "blob/zblob_%02u.bin", n_blob++);
              File_t zblob(tmp, "wb+");
              for (int32_t n{data_.compressedDataLength+16}; n--;) {
                int32_t c{_stream.getc()};
                zblob.putc(c);
              }
              _stream.Seek(safe_pos);
            }
#endif
            if (0 == DecodeEncodeCompare(_stream, _coder, safe_pos, data_.compressedDataLength, data_.uncompressedDataLength)) {
              for (int32_t n{data_.compressedDataLength - 2}; n--;) {
                const int32_t c{_stream.getc()};
                _coder->Compress(c);
              }
            }
            // const int64_t done{_stream.Position()};
            // assert((done - safe_pos) == data_.compressedDataLength);
          } else {
            _di.filter_end = 0;  // Wrong header end filter
          }
          ++nBlocks_;
        }
        break;
    }
    return false;
  }

  _di.filter_end = 0;  // end filter
  return false;
}

auto CAB_filter::Handle(int32_t /*ch*/, int64_t& /*pos*/) noexcept -> bool {  // decoding
  return true;
}
