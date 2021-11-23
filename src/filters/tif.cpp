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
#include "tif.h"
#include <cstdint>
#include <cstring>
#include "File.h"
#include "filter.h"
#include "iEncoder.h"

#if 0  // TODO: rewrite TIF handling

class LZWDictionary final {
public:
  static constexpr int32_t hashSize{9221};
  static constexpr int32_t reset_code{256};
  static constexpr int32_t eof_code{257};

  LZWDictionary() {
    reset();
  }
  virtual ~LZWDictionary() noexcept = default;

  LZWDictionary(const LZWDictionary&) = delete;
  LZWDictionary(LZWDictionary&&) = delete;
  auto operator=(const LZWDictionary&) -> LZWDictionary& = delete;
  auto operator=(LZWDictionary&&) -> LZWDictionary& = delete;

  void reset() noexcept {
    memset(&_dictionary, 0xFF, sizeof(_dictionary));
    _table.fill(0xFF);
    for (uint32_t i{0}; i < 256; i++) {
      _table[-findEntry(-1, i) - 1] = int16_t(i);
      _dictionary[i].suffix = int16_t(i);
    }
    _index = 258;  // 2 extra codes, one for resetting the dictionary and one for signalling EOF
  }

  auto index() const noexcept -> int32_t {
    return _index;
  }

  auto findEntry(int32_t prefix, int32_t suffix) noexcept -> int32_t {
#if 1
    // Golden ratio of 2^32 (not a prime)
    static constexpr auto PHI32{int32_t(0x9E3779B9u)};  // 2654435769

    int32_t i{(PHI32 * prefix * suffix) >> (32 - 13)};
#else
    int32_t i = finalize64(hash(prefix, suffix), 13);
#endif
    int32_t offset = (i > 0) ? hashSize - i : 1;
    for (;;) {
      if (_table[i] < 0) {  // free slot?
        return -i - 1;
      }
      if (_dictionary[_table[i]].prefix == prefix && _dictionary[_table[i]].suffix == suffix) {  // is it the entry we want?
        return _table[i];
      }
      i -= offset;
      if (i < 0) {
        i += hashSize;
      }
    }
  }

  void addEntry(int32_t prefix, int32_t suffix, int32_t offset) noexcept {
    if (prefix == -1 || prefix >= _index || _index > 4095 || offset >= 0) {
      return;
    }
    _dictionary[_index].prefix = int16_t(prefix);
    _dictionary[_index].suffix = int16_t(suffix);
    _table[-offset - 1] = int16_t(_index);
    _index += _index < 4096;
  }

  auto dumpEntry(File_t& f, int32_t code) noexcept -> int32_t {
    int32_t n = 4095;
    while (code > 256 && n >= 0) {
      _buffer[n] = uint8_t(_dictionary[code].suffix);
      n--;
      code = _dictionary[code].prefix;
    }
    _buffer[n] = uint8_t(code);
    f.Write(&_buffer[n], 4096 - n);
    return code;
  }

private:
  struct Entry_t {
    int16_t prefix;
    int16_t suffix;
  };
  std::array<Entry_t, 4096> _dictionary;
  std::array<int16_t, hashSize> _table;
  std::array<uint8_t, 4096> _buffer;
  int32_t _index{0};
};

static void encode(File_t& in, File_t& out, uint64_t /*size*/, int32_t /*info*/, int32_t& /*headerSize*/) {
  LZWDictionary dic;
  int32_t parent{-1};
  int32_t code{0};
  int32_t buffer{0};
  int32_t bitsPerCode{9};
  int32_t bitsUsed{0};
  bool done{false};
  while (!done) {
    buffer = in.getc();
    if (buffer < 0) {
      return;  // 0;
    }
    for (int32_t j{0}; j < 8; j++) {
      code += code + ((buffer >> (7 - j)) & 1);
      bitsUsed++;
      if (bitsUsed >= bitsPerCode) {
        if (code == LZWDictionary::eof_code) {
          done = true;
          break;
        }
        if (code == LZWDictionary::reset_code) {
          dic.reset();
          parent = -1;
          bitsPerCode = 9;
        } else {
          if (code < dic.index()) {
            if (parent != -1) {
              dic.addEntry(parent, dic.dumpEntry(out, code), -1);
            } else {
              out.putc(code);
            }
          } else if (code == dic.index()) {
            int32_t a = dic.dumpEntry(out, parent);
            out.putc(a);
            dic.addEntry(parent, a, -1);
          } else {
            return;  // 0;
          }
          parent = code;
        }
        bitsUsed = 0;
        code = 0;
        if ((1 << bitsPerCode) == dic.index() + 1 && dic.index() < 4096) {
          bitsPerCode++;
        }
      }
    }
  }
}

static auto decodeLzw(File_t& in, File_t& out, uint64_t /*size*/, int32_t& headerSize) -> bool {
  LZWDictionary dic;
  int32_t parent{-1};
  int32_t code{0};
  int32_t bitsPerCode{9};
  int32_t bitsUsed{0};
  for (bool done{false}; !done;) {
    int32_t buffer{in.getc()};
    if (buffer < 0) {
      return false;  // Failure..
    }
    for (int32_t j{0}; j < 8; j++) {
      code += code + ((buffer >> (7 - j)) & 1);
      bitsUsed++;
      if (bitsUsed >= bitsPerCode) {
        if (code == LZWDictionary::eof_code) {
          done = true;
          break;
        }
        if (code == LZWDictionary::reset_code) {
          dic.reset();
          parent = -1;
          bitsPerCode = 9;
        } else {
          if (code < dic.index()) {
            if (parent != -1) {
              dic.addEntry(parent, dic.dumpEntry(out, code), -1);
            } else {
              out.putc(code);
            }
          } else if (code == dic.index()) {
            int32_t a = dic.dumpEntry(out, parent);
            out.putc(a);
            dic.addEntry(parent, a, -1);
          } else {
            return false;  // Failure..
          }
          parent = code;
        }
        bitsUsed = 0;
        code = 0;
        if ((1 << bitsPerCode) == dic.index() + 1 && dic.index() < 4096) {
          bitsPerCode++;
        }
      }
    }
  }
  return true;  // Success..
}

static void writeCode(File_t& f, const bool compare, int32_t* buffer, uint64_t* pos, int32_t* bitsUsed, const int32_t bitsPerCode, const int32_t code, uint64_t* diffFound) {
  *buffer <<= bitsPerCode;
  *buffer |= code;
  (*bitsUsed) += bitsPerCode;
  while ((*bitsUsed) > 7) {
    const uint8_t b = *buffer >> (*bitsUsed -= 8);
    (*pos)++;
#if 1
    if (compare) {
      if (b != f.getc()) {
        *diffFound = *pos;
      }
    } else {
      f.putc(b);
    }
#else
    if (mode == FDECOMPRESS) {
      f->putChar(b);
    } else if (mode == FCOMPARE && b != f->getchar()) {
      *diffFound = *pos;
    }
#endif
  }
}

static auto encodeLzw(File_t& in, File_t& out, const bool compare, uint64_t& diffFound) -> uint64_t {
  LZWDictionary dic;
  uint64_t pos{0};
  int32_t parent{-1};
  int32_t code{0};
  int32_t buffer{0};
  int32_t bitsPerCode{9};
  int32_t bitsUsed{0};
  writeCode(out, compare, &buffer, &pos, &bitsUsed, bitsPerCode, LZWDictionary::reset_code, &diffFound);
  while ((code = in.getc()) >= 0 && diffFound == 0) {
    int32_t index{dic.findEntry(parent, code)};
    if (index < 0) {  // entry not found
      writeCode(out, compare, &buffer, &pos, &bitsUsed, bitsPerCode, parent, &diffFound);
      if (dic.index() > 4092) {
        writeCode(out, compare, &buffer, &pos, &bitsUsed, bitsPerCode, LZWDictionary::reset_code, &diffFound);
        dic.reset();
        bitsPerCode = 9;
      } else {
        dic.addEntry(parent, code, index);
        if (dic.index() >= (1 << bitsPerCode)) {
          bitsPerCode++;
        }
      }
      parent = code;
    } else {
      parent = index;
    }
  }
  if (parent >= 0) {
    writeCode(out, compare, &buffer, &pos, &bitsUsed, bitsPerCode, parent, &diffFound);
  }
  writeCode(out, compare, &buffer, &pos, &bitsUsed, bitsPerCode, LZWDictionary::eof_code, &diffFound);
  if (bitsUsed > 0) {  // flush buffer
    pos++;
#if 1
    if (compare) {
      if (uint8_t(buffer) != out.getc()) {
        diffFound = pos;
      }
    } else {
      out.putc(uint8_t(buffer));
    }
#else
    if (mode == FDECOMPRESS) {
    } else if (mode == FCOMPARE && uint8_t(_buffer) != out->getchar()) {
      diffFound = pos;
    }
#endif
  }
  return pos;
}

#endif

auto Header_t::ScanTIF(int32_t /*ch*/) noexcept -> Filter {
  // ------------------------------------------------------------------------
  // TIFF header
  // -------+------+---------------------------------------------------------
  // offset | size | description
  // -------+------+---------------------------------------------------------
  //      0 |    2 | Endian: II -> Intel (little) / MM -> Motorola (big)
  //      2 |    2 | Version (must be 42)
  //      4 |    4 | Offset to first image file directory (IFD)
  //      8 |    2 | Number of tags
  //     10 |   ...
  // -------+------+---------------------------------------------------------
  // TIFF tags
  // -------+------+---------------------------------------------------------
  // offset | size | description
  // -------+------+---------------------------------------------------------
  //      2 |    2 | Field tag
  //      4 |    2 | Field type, 1=BYTE, 2=ASCII, 3=WORD, 4=DWORD, 5=Other
  //      6 |    4 | Field length
  //     10 |    4 | Field value
  //     14 |   ... (other tags or image data)

  static constexpr uint32_t offset{512};

  enum {
    IMAGE_WIDTH = 256,  //
    IMAGE_HEIGHT = 257,
    BITS_PER_SAMPLE = 258,
    COMPRESSION_TYPE = 259,
    PHOTOMETRIC = 262,
    STRIP_OFFSETS = 273,
    BYTES_PER_PIXEL = 277,
  };

  // Intel heading ...
  if (0x49492A00 == m4(offset - 0)) {
    const uint32_t ifd{i4(offset - 4)};  // Header must be the beginning (!) as headers normally do ... but with TIFF ...
    if (ifd < offset) {
      uint32_t width{0};
      uint32_t height{0};
      uint32_t bps{0};
      uint32_t cmp{0};
      uint32_t rgb{0};
      int32_t ots{0};
      const auto tags{i2(offset - ifd)};
      uint32_t i{offset};
      for (uint32_t n{0}, ntags{0}; (i > 18) && (n < tags) && (ntags < 7); i -= 12, ++n) {
        const auto tagFmt{i2(i - (ifd + 4))};
        if ((3 == tagFmt) || (4 == tagFmt)) {
          const auto tag{i2(i - (ifd + 2))};
          const auto tagLen{i4(i - (ifd + 6))};
          const auto tagVal{(3 == tagFmt) ? i2(i - ((ifd + 10))) : i4(i - (ifd + 10))};
          if (IMAGE_WIDTH == tag) {
            width = tagVal;
            ++ntags;
          } else if (IMAGE_HEIGHT == tag) {
            height = tagVal;
            ++ntags;
          } else if (BITS_PER_SAMPLE == tag) {
            bps = 1 == tagLen ? tagVal : 8;
            ++ntags;
          } else if (COMPRESSION_TYPE == tag) {  // 1=none, 2/3/4=ccitt, 5=lzw, 6=ojpeg, 7=jpeg
            cmp = tagVal;
            ++ntags;
          } else if (PHOTOMETRIC == tag) {  // 0/1=grey, 2=rgb, 3=palette
            rgb = tagVal;
            ++ntags;
          } else if ((STRIP_OFFSETS == tag) && (4 == tagFmt)) {
            ots = int32_t(tagVal);
            ++ntags;
          } else if (BYTES_PER_PIXEL == tag) {
            _di.bytes_per_pixel = tagVal;
            ++ntags;
          }
        }
      }
      if ((width > 0) && (width < 0x30000) &&    //
          (height > 0) && (height < 0x10000) &&  //
          (0 != bps) &&                          //
          ((1 == cmp) || (5 == cmp)) &&          //
          (2 == rgb) &&                          //
          ((3 == _di.bytes_per_pixel) || (4 == _di.bytes_per_pixel))) {
        //        _di.lzw_encoded = 5 == cmp;
        _di.filter_end = int32_t(width * height * _di.bytes_per_pixel);
        ots -= int32_t(offset);
        _di.offset_to_start = (ots < 0) ? 0 : ots;
#if 0
        fprintf(stderr, "iTIF %ux%ux%u  \n", width, height, _di.bytes_per_pixel);
        fflush(stderr);
#endif
        return Filter::TIF;
      }
    }
  }

  // Motorola heading ...
  if (0x4D4D002A == m4(offset - 0)) {
    const uint32_t ifd{m4(offset - 4)};  // Header must be the beginning (!) as headers normally do ... but with TIFF ...
    if (ifd < offset) {
      uint32_t width{0};
      uint32_t height{0};
      uint32_t bps{0};
      uint32_t cmp{0};
      uint32_t rgb{0};
      int32_t ots{0};
      const auto tags{m2(offset - ifd)};
      uint32_t i{offset};
      for (uint32_t n{0}, ntags{0}; (i > 18) && (n < tags) && (ntags < 7); i -= 12, ++n) {
        const auto tagFmt{m2(i - (ifd + 4))};
        if ((3 == tagFmt) || (4 == tagFmt)) {
          const auto tag{m2(i - (ifd + 2))};
          const auto tagLen{m4(i - (ifd + 6))};
          const auto tagVal{(3 == tagFmt) ? m2(i - (ifd + 10)) : m4(i - (ifd + 10))};
          if (IMAGE_WIDTH == tag) {
            width = tagVal;
            ++ntags;
          } else if (IMAGE_HEIGHT == tag) {
            height = tagVal;
            ++ntags;
          } else if (BITS_PER_SAMPLE == tag) {
            bps = 1 == tagLen ? tagVal : 8;
            ++ntags;
          } else if (COMPRESSION_TYPE == tag) {  // 1=none, 2/3/4=ccitt, 5=lzw, 6=ojpeg, 7=jpeg
            cmp = tagVal;
            ++ntags;
          } else if (PHOTOMETRIC == tag) {  // 0/1=grey, 2=rgb, 3=palette
            rgb = tagVal;
            ++ntags;
          } else if ((STRIP_OFFSETS == tag) && (4 == tagFmt)) {
            ots = int32_t(tagVal);
            ++ntags;
          } else if (BYTES_PER_PIXEL == tag) {
            _di.bytes_per_pixel = tagVal;
            ++ntags;
          }
        }
      }
      if ((width > 0) && (width < 0x30000) &&    //
          (height > 0) && (height < 0x10000) &&  //
          (0 != bps) &&                          //
          ((1 == cmp) || (5 == cmp)) &&          //
          (2 == rgb) &&                          //
          ((3 == _di.bytes_per_pixel) || (4 == _di.bytes_per_pixel))) {
        //        _di.lzw_encoded = 5 == cmp;
        _di.filter_end = int32_t(width * height * _di.bytes_per_pixel);
        ots -= int32_t(offset);
        _di.offset_to_start = (ots < 0) ? 0 : ots;
#if 0
        fprintf(stderr, "mTIF %ux%ux%u  \n", width, height, _di.bytes_per_pixel);
        fflush(stderr);
#endif
        return Filter::TIF;
      }
    }
  }

  return Filter::NOFILTER;
}

TIF_filter::TIF_filter(File_t& stream, iEncoder_t& coder, const DataInfo_t& di)
    : _stream{stream},  //
      _coder{coder},
      _di{di} {}

TIF_filter::~TIF_filter() noexcept = default;

auto TIF_filter::Handle(int32_t ch) noexcept -> bool {  // encoding
  if (_di.lzw_encoded) {
    // TODO
  } else {
    _rgba[_length++] = int8_t(ch);

    if (_length >= _di.bytes_per_pixel) {
      _length = 0;

      const auto b{_rgba[0]};
      const auto g{_rgba[1]};
      const auto r{_rgba[2]};
      _coder.Compress(g);
      _coder.Compress(g - r);
      _coder.Compress(g - b);
      if (4 == _di.bytes_per_pixel) {
        _coder.Compress(_rgba[3] - _old_a);  // Delta encode alpha channel
        _old_a = _rgba[3];
      }
    }
  }

  return true;
}

auto TIF_filter::Handle(int32_t ch, int64_t& /*pos*/) noexcept -> bool {  // decoding
  if (_di.lzw_encoded) {
    // TODO
  } else {
    _rgba[_length++] = int8_t(ch);

    if (_length >= _di.bytes_per_pixel) {
      _length = 0;

      const auto b{_rgba[0]};
      const auto g{_rgba[1]};
      const auto r{_rgba[2]};
      _stream.putc(b - r);
      _stream.putc(b);
      _stream.putc(b - g);
      if (4 == _di.bytes_per_pixel) {
        _old_a += _rgba[3];  // Delta decode alpha channel
        _stream.putc(_old_a);
      }
    }
  }

  return true;
}
