/* Copyright (c) 2019-2022 Marwijn Hessel
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
#include "gzip.h"
#include <zconf.h>
#include <zlib.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <tuple>
#include "File.h"
#include "filter.h"
#include "gzip/gzip.h"
#include "iEncoder.h"

static auto GZipEncodeCompare(File_t& in, const uint32_t size, File_t& out) noexcept -> std::tuple<bool, int64_t, uint8_t> {
  const auto safe_ipos{in.Position()};
  const auto safe_opos{out.Position()};
  for (uint8_t level{9}; level >= 1; --level) {
    File_t deflate_tmp /*("_deflate_tmp_.bin", "wb+")*/;
    if (GZip_OK == gzip::zip(in, size, deflate_tmp, level)) {
      int64_t length{deflate_tmp.Size()};
      deflate_tmp.Rewind();
      bool success{true};
      for (int64_t len{length}; len-- > 0;) {
        const int32_t a{out.getc()};
        const int32_t b{deflate_tmp.getc()};
        if (a != b) {
          success = false;
          break;
        }
      }
      if (success) {
        return {true, length, level};
      }
      in.Seek(safe_ipos);  // Failure, try with an other compression level
      out.Seek(safe_opos);
    }
  }
  return {false, 0, 0};
}

class ZHeader_t final {
public:
  explicit ZHeader_t(const uint16_t header) : zh_{parse_zlib_header(header)} {}
  explicit ZHeader_t(const uint8_t clevel, const uint8_t memLevel, const uint8_t zh, const uint8_t dc) : clevel_{clevel}, memLevel_{memLevel}, zh_{int8_t(zh)}, diffCount_{dc} {
    const int32_t windowBits{(zh_ == -1) ? 0 : MAX_WBITS + 10 + zh_ / 4};
    windowBits_ = windowBits - MAX_WBITS;
  }
  explicit ZHeader_t(const uint8_t clevel, const uint8_t memLevel, const uint8_t zh, const uint8_t dc, File_t& in) : ZHeader_t{clevel, memLevel, zh, dc} {
    for (uint32_t n{0}, old_pos{0}; n < diffCount_; ++n) {
      diffPos_[n] = in.get32() + old_pos;
      old_pos = diffPos_[n];
    }
    for (uint32_t n{0}; n < diffCount_; ++n) {
      diffByte_[n] = uint8_t(in.getc());
    }
  }
  ~ZHeader_t() noexcept = default;

  ZHeader_t(const ZHeader_t&) = delete;
  ZHeader_t(ZHeader_t&&) = delete;
  auto operator=(const ZHeader_t&) -> ZHeader_t& = delete;
  auto operator=(ZHeader_t&&) -> ZHeader_t& = delete;

  static constexpr auto DIFF_COUNT_LIMIT{1u << 7};

  [[nodiscard]] auto clevel() const noexcept -> int32_t {
    return clevel_;
  }

  [[nodiscard]] auto memLevel() const noexcept -> int32_t {
    return memLevel_;
  }

  [[nodiscard]] auto zh() const noexcept -> int32_t {
    return zh_;
  }

  [[nodiscard]] auto diffCount() const noexcept -> uint32_t {
    return diffCount_;
  }

  [[nodiscard]] auto diffPos(const uint32_t idx) const noexcept -> uint32_t {
    return diffPos_[idx];
  }

  void diffPos(const uint32_t idx, const uint32_t pos) noexcept {
    diffPos_[idx] = pos;
  }

  [[nodiscard]] auto diffByte(const uint32_t idx) const noexcept -> uint8_t {
    return diffByte_[idx];
  }

  void diffByte(const uint32_t idx, const uint8_t ch) noexcept {
    diffByte_[idx] = ch;
  }

  [[nodiscard]] auto windowBits() const noexcept -> int32_t {
    return windowBits_;
  }

  [[nodiscard]] auto HeaderLength() const noexcept -> uint32_t {
    // 1 clevel
    // 1 memLevel
    // 1 zh
    // 1 diffCount
    // 4 diffPos[]
    // 1 diffByte[]

    return 4u + uint32_t((diffCount_ * sizeof(uint32_t)) + (diffCount_ * sizeof(uint8_t)));
  }

  void Encode(iEncoder_t& coder, const uint32_t decoded_length) const noexcept {
    const auto hlen{HeaderLength()};
    const auto diffCount{diffCount_};
    coder.CompressN(32, decoded_length + hlen);
    coder.Compress(clevel_);                               // clevel
    coder.Compress(memLevel_);                             // mem_level
    coder.Compress(zh_);                                   // zlib_header
    coder.Compress(uint8_t(diffCount));                    // diffCount
    for (uint32_t n{0}, old_pos{0}; n < diffCount; ++n) {  // diffPos[]
      const auto pos{diffPos_[n]};                         //
      coder.CompressN(32, pos - old_pos);                  // delta encoded position
      old_pos = pos;                                       //
    }                                                      //
    for (uint32_t n{0}; n < diffCount; ++n) {              // diffByte[]
      coder.Compress(diffByte_[n]);                        //
    }                                                      //
  }

  auto Config() noexcept -> std::tuple<int32_t, int32_t, int32_t> {
    const int32_t windowBits{(-1 == zh_) ? 0 : MAX_WBITS + 10 + zh_ / 4};
    const int32_t ctype{zh_ % 4};
    const int32_t maxclevel{!windowBits ? 9 : (3 == ctype) ? 9 : (2 == ctype) ? 6 : (1 == ctype) ? 5 : 1};
    const int32_t minclevel{!windowBits ? 1 : (3 == ctype) ? 7 : (2 == ctype) ? 6 : (1 == ctype) ? 2 : 1};

    windowBits_ = windowBits - MAX_WBITS;
    return {minclevel, maxclevel, windowBits_};
  }

private:
  int32_t clevel_{0};
  int32_t memLevel_{0};
  const int32_t zh_;
  uint32_t diffCount_{0};
  std::array<uint32_t, DIFF_COUNT_LIMIT> diffPos_{};
  std::array<uint8_t, DIFF_COUNT_LIMIT> diffByte_{};

  int32_t windowBits_{0};

  [[nodiscard]] auto parse_zlib_header(const uint16_t header) const noexcept -> int32_t {
    // clang-format off
    switch (header) {
      case 0x2815: return 0;  case 0x2853: return 1;  case 0x2891: return 2;  case 0x28CF: return 3;
      case 0x3811: return 4;  case 0x384F: return 5;  case 0x388D: return 6;  case 0x38CB: return 7;
      case 0x480D: return 8;  case 0x484B: return 9;  case 0x4889: return 10; case 0x48C7: return 11;
      case 0x5809: return 12; case 0x5847: return 13; case 0x5885: return 14; case 0x58C3: return 15;
      case 0x6805: return 16; case 0x6843: return 17; case 0x6881: return 18; case 0x68DE: return 19;
      case 0x7801: return 20; case 0x785E: return 21; case 0x789C: return 22; case 0x78DA: return 23;
      default:     break;
    }
    // clang-format on
    return -1;
  }
};

class ZLib_t {
public:
  ZLib_t(const uint16_t header) : zheader_{header} {}
  ZLib_t(const uint8_t clevel, const uint8_t memLevel, const uint8_t zh, const uint8_t dc, File_t& in) : zheader_{clevel, memLevel, zh, dc, in} {}
  ~ZLib_t() noexcept = default;

  ZLib_t(const ZLib_t&) = delete;
  ZLib_t(ZLib_t&&) = delete;
  auto operator=(const ZLib_t&) -> ZLib_t& = delete;
  auto operator=(ZLib_t&&) -> ZLib_t& = delete;

  [[nodiscard]] auto Inflate(File_t& stream, const uint32_t block_length, File_t& inflate_tmp) noexcept -> std::tuple<bool, uint32_t> {  // decode
    memset(&strm_, 0, sizeof(strm_));
    if (Z_OK != inflateInit2(&strm_, (-1 == zheader_.zh()) ? -MAX_WBITS : MAX_WBITS)) {
      return {false, 0};
    }
    int32_t state{Z_STREAM_END};
    uint32_t length{block_length};
    for (uint32_t i{0}; i < length; i += BLOCK_SIZE) {
      const uint32_t block_size{(std::min)(length - i, BLOCK_SIZE)};
      if (block_size != stream.Read(zin_.data(), block_size)) {
        state = Z_STREAM_ERROR;
        break;
      }
      strm_.next_in = zin_.data();
      strm_.avail_in = block_size;
      do {
        strm_.next_out = zout_.data();
        strm_.avail_out = BLOCK_SIZE;
        state = inflate(&strm_, Z_FINISH);
        if (Z_STREAM_END == state) {
          length = strm_.total_in;  // True for PKZip, but not for GZ length of file is not known
        }
        assert(BLOCK_SIZE >= strm_.avail_out);
        const size_t have{BLOCK_SIZE - strm_.avail_out};
        if (have != inflate_tmp.Write(zout_.data(), have)) {
          state = Z_STREAM_ERROR;
          break;
        }
      } while ((0 == strm_.avail_out) && (Z_BUF_ERROR == state));
      if ((Z_BUF_ERROR != state) && (Z_STREAM_END != state)) {
        break;
      }
    }
    inflateEnd(&strm_);
    return {(Z_OK == state) || (Z_STREAM_END == state), strm_.total_out};
  }

  [[nodiscard]] auto EncodeCompare(File_t& in, const size_t size, File_t& original) noexcept -> std::tuple<bool, int64_t, int32_t, int32_t> {
    const auto safe_ipos{in.Position()};
    const auto safe_opos{original.Position()};
    const auto [minclevel, maxclevel, windowBits]{zheader_.Config()};

    for (int32_t memLevel{1}; memLevel <= 9; ++memLevel) {
      for (int32_t clevel{minclevel}; clevel <= maxclevel; ++clevel) {
        const auto [deflate_state, deflate_length]{Deflate(in, size, original, true)};
        if ((Z_OK == deflate_state) && (deflate_length > 0)) {
          return {true, deflate_length, clevel, memLevel};
        }
        in.Seek(safe_ipos);  // Failure, try with an other compression level
        original.Seek(safe_opos);
      }
    }
    return {false, 0, 0, 0};
  }

  auto Deflate(File_t& source, const size_t size, File_t& dest, const bool compare) noexcept -> std::tuple<int32_t, uint32_t> {  // encode
    const int32_t state{Deflate_(source, size, dest, compare)};
    deflateEnd(&strm_);
    return {state, strm_.total_out};
  }

  void HeaderEncode(iEncoder_t& coder, const uint32_t decoded_length) const noexcept {
    zheader_.Encode(coder, decoded_length);
  }

private:
  static constexpr auto BLOCK_SIZE{1u << 16};

  ZHeader_t zheader_;
  z_stream strm_{};

  std::array<uint8_t, BLOCK_SIZE> zin_{};
  std::array<uint8_t, BLOCK_SIZE> zout_{};

  [[nodiscard]] auto Deflate_(File_t& source, const size_t size, File_t& dest, const bool compare) noexcept -> int32_t {  // encode
    memset(&strm_, 0, sizeof(strm_));
    int32_t state{deflateInit2(&strm_, zheader_.clevel(), Z_DEFLATED, zheader_.windowBits(), zheader_.memLevel(), Z_DEFAULT_STRATEGY)};
    if (Z_OK != state) {
      return state;
    }

    uint32_t position{0};
    uint32_t next_pos{~0u};
    uint32_t next_count{0};
    uint32_t diff_count{0};
    if (compare) {
      diff_count = 0;
    } else {
      if (next_count < diff_count) {
        next_pos = zheader_.diffPos(next_count);
      }
    }
    for (auto insize{size}; insize > 0;) {
      strm_.avail_in = FileRead(source, insize, zin_.data(), BLOCK_SIZE);
      strm_.next_in = zin_.data();

      const int32_t flush{((0 == strm_.avail_in) || (0 == insize)) ? Z_FINISH : Z_NO_FLUSH};

      do {
        strm_.avail_out = BLOCK_SIZE;
        strm_.next_out = zout_.data();
        state = deflate(&strm_, flush);
        if (Z_STREAM_ERROR == state) {
          return state;
        }
        assert(BLOCK_SIZE >= strm_.avail_out);
        const uint32_t have{BLOCK_SIZE - strm_.avail_out};
        if (compare) {
          for (uint32_t n{0}; n < have; ++n) {
            const int32_t ch{dest.getc()};
            if (zout_[n] != ch) {
              if (diff_count < ZHeader_t::DIFF_COUNT_LIMIT) {
                zheader_.diffPos(diff_count, position + n);
                zheader_.diffByte(diff_count, uint8_t(ch));
                ++diff_count;
              } else {
                return Z_ERRNO;
              }
            }
          }
        } else {
          while ((next_count < diff_count) && (next_pos >= position) && (next_pos < (position + have))) {
            assert(next_count < diff_count);
            zout_[next_pos - position] = zheader_.diffByte(next_count);  // Correct output
            next_pos = zheader_.diffPos(++next_count);
          }
          if (have != dest.Write(zout_.data(), have)) {
            return Z_ERRNO;
          }
        }
        position += have;
      } while (strm_.avail_out == 0);
      if (0 != strm_.avail_in) {
        return Z_ERRNO;
      }
    }
    if (Z_STREAM_END != state) {
      return Z_ERRNO;
    }
    return Z_OK;
  }

  [[nodiscard]] auto FileRead(File_t& source, size_t& insize, void* buf, size_t size) const noexcept -> uint32_t {
    if (size < insize) {
      insize -= size;
    } else {
      size = insize;
      insize = 0;
    }
    const size_t len{source.Read(buf, size)};
    if ((0 == len) || (size_t(EOF) == len)) {
      return 0;
    }
    return uint32_t(len);
  }
};

auto EncodeGZip(File_t& in, const int64_t size, File_t& out) noexcept -> bool {
  const auto clevel{uint8_t(in.getc())};     // clevel
  const auto mem_level{uint8_t(in.getc())};  // mem_level
  const auto zh{uint8_t(in.getc())};         // zlib_header
  const auto dc{uint8_t(in.getc())};         // diffCount
  if (!mem_level && !zh) {
    if (GZip_OK == gzip::zip(in, uint32_t(size - 4), out, clevel)) {
      return true;
    }
  } else {
    ZLib_t zlib{clevel, mem_level, zh, dc, in};
    const auto [state, length]{zlib.Deflate(in, size_t(size) - (4 + (dc * sizeof(uint32_t)) + (dc * sizeof(uint8_t))), out, false)};
    if (Z_OK == state) {
      return true;
    }
  }
  return false;
}

auto DecodeEncodeCompare(File_t& stream, iEncoder_t* const coder, const int64_t safe_pos, const int64_t block_length) noexcept -> int64_t {
  if (block_length > 0) {
    stream.Seek(safe_pos);
    File_t inflate_tmp /*("_inflate_tmp_.bin", "wb+")*/;
    auto length{uint32_t(block_length)};

    if (GZip_OK == gzip::unzip(stream, inflate_tmp, length)) {  // Try to decode using GZIP
      inflate_tmp.Rewind();
      stream.Seek(safe_pos);
      const auto decoded_length{uint32_t(inflate_tmp.Size())};
      const auto [encode_succes, encode_length, clevel]{GZipEncodeCompare(inflate_tmp, decoded_length, stream)};
      if (encode_succes) {
        if (nullptr != coder) {
          ZHeader_t zheader{clevel, 0, 0, 0};
          zheader.Encode(*coder, decoded_length);
          inflate_tmp.Rewind();
          for (int32_t c; EOF != (c = inflate_tmp.getc());) {
            coder->Compress(c);
          }
        }
        return length;  // Success
      }
    }

    stream.Seek(safe_pos);
    const int32_t h1{stream.getc()};
    const int32_t h2{stream.getc()};
    stream.Seek(safe_pos);

    ZLib_t zlib{uint16_t((h1 << 8) | h2)};
    const auto [decode_succes, decoded_length]{zlib.Inflate(stream, uint32_t(block_length), inflate_tmp)};  // Try to decode using ZLIB
    if (decode_succes) {
      inflate_tmp.Rewind();
      stream.Seek(safe_pos);
      const auto [encode_succes, encode_length, clevel, mem_level]{zlib.EncodeCompare(inflate_tmp, decoded_length, stream)};
      if (encode_succes) {
        if (nullptr != coder) {
          zlib.HeaderEncode(*coder, decoded_length);
          inflate_tmp.Rewind();
          for (int32_t c; EOF != (c = inflate_tmp.getc());) {
            coder->Compress(c);
          }
        }
        return length;  // Success
      }
    }

#if 0
    fprintf(stderr, "fail %u, %d/%d/%d\n", uint32_t(block_length), 0, 0, 0);
#endif
  }

  if (nullptr != coder) {
    coder->CompressN(32, iFilter_t::_DEADBEEF);  // Failure...
  }
  stream.Seek(safe_pos);
  return 0;
}
