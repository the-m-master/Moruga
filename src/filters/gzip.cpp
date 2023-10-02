/* Copyright (c) 2019-2023 Marwijn Hessel
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
#include <bzlib.h>
#include <zconf.h>
#include <zlib.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <tuple>
#include "File.h"
#include "bz2.h"
#include "filter.h"
#include "gzip/gzip.h"
#include "iEncoder.h"

namespace {
  static constexpr auto DIFF_COUNT_LIMIT{1u << 7};      // Default 128, range 0 .. 255
  static constexpr auto BLOCK_SIZE{UINT32_C(1) << 16};  // Default block size of 65536 bytes for ZLib and BZip2

};  // namespace

/**
 * @struct Config_t
 * @brief Storage class for configuration(s)
 *
 * Storage class for general PKZIP/GZip/BZip2 configuration(s)
 */
struct Config_t final {
  int8_t clevel{0};      // 1..9 for ZLib/GZip -1..-9 fir BZip2
  int8_t windowBits{0};  // -15 or 15 ZLib otherwise 0
  uint8_t memLevel{0};   // 1..9 for ZLib otherwise 0
  uint8_t diffCount{0};

  void Read(File_t& stream) noexcept {
    clevel = static_cast<int8_t>(stream.getc());
    windowBits = static_cast<int8_t>(stream.getc());
    memLevel = static_cast<uint8_t>(stream.getc());
    diffCount = static_cast<uint8_t>(stream.getc());

    assert(((clevel >= -9) && (clevel <= -1)) || ((clevel >= 1) && (clevel <= 9)));
    assert((MAX_WBITS == windowBits) || (-MAX_WBITS == windowBits) || (0 == windowBits));
    assert(((memLevel >= 1) && (memLevel <= 9)) || (0 == memLevel));
    assert((diffCount >= 0) && (diffCount < DIFF_COUNT_LIMIT));
  }

  [[nodiscard]] constexpr auto Length() const noexcept -> size_t {
    return sizeof(Config_t::clevel) +        //
           sizeof(Config_t::windowBits) +    //
           sizeof(Config_t::memLevel) +      //
           sizeof(Config_t::diffCount) +     //
           (diffCount * sizeof(uint32_t)) +  // diffPos[]
           (diffCount * sizeof(uint8_t));    // diffByte[]
  }
};

/**
 * @struct ZHeader_t
 * @brief Handler for ZLib configuration
 *
 * Handler for ZLib configuration
 */
class ZHeader_t final {
public:
  ZHeader_t() = default;
  explicit ZHeader_t(const Config_t& config) : config_{config} {}
  explicit ZHeader_t(const Config_t& config, File_t& in) : ZHeader_t{config} {
    for (uint32_t n{0}, old_pos{0}; n < config_.diffCount; ++n) {
      diffPos_[n] = in.get32() + old_pos;
      old_pos = diffPos_[n];
    }
    for (uint32_t n{0}; n < config_.diffCount; ++n) {
      diffByte_[n] = static_cast<uint8_t>(in.getc());
    }
  }
  ~ZHeader_t() noexcept = default;

  ZHeader_t(const ZHeader_t&) = delete;
  ZHeader_t(ZHeader_t&&) = delete;
  auto operator=(const ZHeader_t&) -> ZHeader_t& = delete;
  auto operator=(ZHeader_t&&) -> ZHeader_t& = delete;

  [[nodiscard]] auto Config() noexcept -> Config_t& {
    return config_;
  }

  void DiffCount(const uint8_t diffCount) noexcept {
    config_.diffCount = diffCount;
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

  void Encode(iEncoder_t& coder, const uint32_t decoded_length) const noexcept {
    const auto hlen{static_cast<uint32_t>(config_.Length())};
    coder.CompressN(32, decoded_length + hlen);
    coder.Compress(config_.clevel);
    coder.Compress(config_.windowBits);
    coder.Compress(config_.memLevel);
    coder.Compress(config_.diffCount);
    for (uint32_t n{0}, old_pos{0}; n < config_.diffCount; ++n) {  // diffPos[]
      const auto pos{diffPos_[n]};                                 //
      coder.CompressN(32, pos - old_pos);                          // delta encoded position
      old_pos = pos;                                               //
    }                                                              //
    for (uint32_t n{0}; n < config_.diffCount; ++n) {              // diffByte[]
      coder.Compress(diffByte_[n]);                                //
    }                                                              //
  }

private:
  Config_t config_{};
  std::array<uint32_t, DIFF_COUNT_LIMIT> diffPos_{};
  std::array<uint8_t, DIFF_COUNT_LIMIT> diffByte_{};
};

namespace {
  auto GZipEncodeCompare(File_t& in, const uint32_t size, File_t& out) noexcept -> std::tuple<ZHeader_t*, uint32_t, uint8_t> {
    const auto safe_ipos{in.Position()};
    const auto safe_opos{out.Position()};
    for (int8_t clevel{9}; clevel >= 1; --clevel) {  // --best .. --fast
      Config_t config{};
      config.clevel = clevel;
      ZHeader_t* zheader{new ZHeader_t{config}};
      const File_t deflate_tmp /*("_deflate_tmp_.bin", "wb+")*/;
      if (GZip_OK == gzip::Zip(in, size, deflate_tmp, static_cast<uint8_t>(clevel))) {
        const auto length{uint32_t(deflate_tmp.Size())};
        deflate_tmp.Rewind();
        uint32_t diff_count{0};
        bool success{true};
        for (uint32_t position{0}; position < length; ++position) {
          const auto a{out.getc()};
          const auto b{deflate_tmp.getc()};
          if (a != b) {
            if (diff_count < DIFF_COUNT_LIMIT) {
              zheader->diffPos(diff_count, position);
              zheader->diffByte(diff_count, static_cast<uint8_t>(a));
              ++diff_count;
            } else {
              success = false;  // To many differences...
              break;
            }
          }
        }
        if (success) {
          zheader->DiffCount(static_cast<uint8_t>(diff_count));
          return {zheader, length, clevel};
        }
      }
      delete zheader;
      zheader = nullptr;
      in.Seek(safe_ipos);  // Failure, try with an other compression level
      out.Seek(safe_opos);
    }
    return {nullptr, 0, 0};
  }
};  // namespace

/**
 * @struct ZLib_t
 * @brief Handler for ZLib calls
 *
 * Handler for ZLib calls
 */
class ZLib_t final {
public:
  ZLib_t() = default;
  ZLib_t(const Config_t& config, File_t& in) : zheader_{config, in} {}
  ~ZLib_t() noexcept = default;

  ZLib_t(const ZLib_t&) = delete;
  ZLib_t(ZLib_t&&) = delete;
  auto operator=(const ZLib_t&) -> ZLib_t& = delete;
  auto operator=(ZLib_t&&) -> ZLib_t& = delete;

  [[nodiscard]] auto Inflate(File_t& stream, const uint32_t block_length, File_t& inflate_tmp, const int32_t windowBits, const int32_t flush) noexcept -> std::tuple<bool, uint32_t> {  // decode
    memset(&strm_, 0, sizeof(strm_));
    if (Z_OK != inflateInit2(&strm_, windowBits)) {
      return {false, 0};
    }
    zheader_.Config().windowBits = static_cast<int8_t>(windowBits);
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
        state = inflate(&strm_, (BLOCK_SIZE == block_size) ? flush : Z_FINISH);
        if (Z_STREAM_END == state) {
          length = static_cast<uint32_t>(strm_.total_in);  // True for PKZip, but not for GZ length of file is not known
        }
        assert(BLOCK_SIZE >= strm_.avail_out);
        const size_t have{BLOCK_SIZE - strm_.avail_out};
        if (have > 0) {
          if (have != inflate_tmp.Write(zout_.data(), have)) {
            state = Z_STREAM_ERROR;
            break;
          }
        }
      } while ((0 == strm_.avail_out) && (Z_BUF_ERROR == state));
      if ((Z_BUF_ERROR != state) && (Z_STREAM_END != state)) {
        break;
      }
    }
    inflateEnd(&strm_);
    return {(Z_OK == state) || (Z_STREAM_END == state), strm_.total_out};
  }

  [[nodiscard]] auto EncodeCompare(File_t& in, const size_t size, File_t& original) noexcept -> std::tuple<bool, int64_t> {
    const auto safe_ipos{in.Position()};
    const auto safe_opos{original.Position()};

    for (int8_t clevel{9}; clevel >= 1; --clevel) {  // --best .. --fast
      for (uint8_t memLevel{9}; memLevel >= 1; --memLevel) {
        zheader_.Config().clevel = clevel;
        zheader_.Config().memLevel = memLevel;
        const auto [deflate_state, deflate_length]{Deflate(in, size, original, true)};
        if ((Z_OK == deflate_state) && (deflate_length > 0)) {
          return {true, deflate_length};
        }
        in.Seek(safe_ipos);  // Failure, try with an other compression level
        original.Seek(safe_opos);
      }
    }
    return {false, 0};
  }

  [[nodiscard]] auto Deflate(File_t& source, const size_t size, File_t& dest, const bool compare) noexcept -> std::tuple<int32_t, uint32_t> {  // encode
    const int32_t state{Deflate_(source, size, dest, compare)};
    deflateEnd(&strm_);
    return {state, strm_.total_out};
  }

  void HeaderEncode(iEncoder_t& coder, const uint32_t decoded_length) const noexcept {
    zheader_.Encode(coder, decoded_length);
  }

private:
  ZHeader_t zheader_{};
  int32_t : 32;  // Padding
  z_stream strm_{};

  std::array<uint8_t, BLOCK_SIZE> zin_{};
  std::array<uint8_t, BLOCK_SIZE> zout_{};

  [[nodiscard]] auto Deflate_(File_t& source, const size_t size, File_t& dest, const bool compare) noexcept -> int32_t {  // encode
    memset(&strm_, 0, sizeof(strm_));
    const auto clevel{zheader_.Config().clevel};  // 1..9
    const auto windowBits{(0 == zheader_.Config().windowBits) ? MAX_WBITS : zheader_.Config().windowBits};
    const auto memLevel{zheader_.Config().memLevel};
    int32_t state{deflateInit2(&strm_, clevel, Z_DEFLATED, windowBits, memLevel, Z_DEFAULT_STRATEGY)};
    if (Z_OK != state) {
      return state;
    }

    uint32_t position{0};
    uint32_t next_pos{UINT32_C(~0)};
    uint32_t next_count{0};
    uint32_t diff_count{0};
    if (!compare) {
      diff_count = zheader_.Config().diffCount;
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
        const auto have{BLOCK_SIZE - strm_.avail_out};
        if (have > 0) {
          if (compare) {
            for (uint32_t n{0}; n < have; ++n) {
              const auto a{dest.getc()};
              const auto b{zout_[n]};
              if (a != b) {
                if (diff_count < DIFF_COUNT_LIMIT) {
                  zheader_.diffPos(diff_count, position + n);
                  zheader_.diffByte(diff_count, static_cast<uint8_t>(a));
                  ++diff_count;
                } else {
                  return Z_ERRNO;  // To many differences...
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
        }
      } while (strm_.avail_out == 0);
      if (0 != strm_.avail_in) {
        return Z_ERRNO;
      }
    }
    if (Z_STREAM_END != state) {
      return Z_ERRNO;
    }
    if (compare) {
      zheader_.DiffCount(static_cast<uint8_t>(diff_count));
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

/**
 * @struct BZip2_t
 * @brief Handler for BZip2 calls
 *
 * Handler for BZip2 calls
 */
class BZip2_t final {
public:
  BZip2_t() = default;
  BZip2_t(const Config_t& config, File_t& in) : zheader_{config, in} {}
  ~BZip2_t() noexcept = default;

  BZip2_t(const BZip2_t&) = delete;
  BZip2_t(BZip2_t&&) = delete;
  auto operator=(const BZip2_t&) -> BZip2_t& = delete;
  auto operator=(BZip2_t&&) -> ZLib_t& = delete;

  [[nodiscard]] auto Decompress(File_t& stream, const uint32_t block_length, File_t& inflate_tmp) noexcept -> std::tuple<bool, uint32_t> {  // decode
    memset(&strm_, 0, sizeof(strm_));
    if (BZ_OK != BZ2_bzDecompressInit(&strm_, 0, 0)) {
      return {false, 0};
    }
    uint32_t dsize{0};
    int32_t state{BZ_OK};
    for (uint32_t i{0}; i < block_length; i += BLOCK_SIZE) {
      const uint32_t block_size{(std::min)(block_length - i, BLOCK_SIZE)};
      strm_.avail_in = static_cast<uint32_t>(stream.Read(zin_.data(), block_size));
      if (0 == i) {
        const auto bzlevel{static_cast<int8_t>(zin_[3] - '0')};
        if ((bzlevel >= 1) && (bzlevel <= 9)) {
          zheader_.Config().clevel = -bzlevel;
        } else {
          state = BZ_IO_ERROR;
          break;
        }
      }
      if (strm_.avail_in == 0) {
        break;
      }
      strm_.next_in = reinterpret_cast<char*>(zin_.data());
      do {
        strm_.avail_out = BLOCK_SIZE;
        strm_.next_out = reinterpret_cast<char*>(zout_.data());
        state = BZ2_bzDecompress(&strm_);
        if ((BZ_OK != state) && (BZ_STREAM_END != state)) {
          BZ2_bzDecompressEnd(&strm_);
          return {false, 0};
        }
        const auto part{BLOCK_SIZE - strm_.avail_out};
        if (part != inflate_tmp.Write(zout_.data(), part)) {
          state = BZ_IO_ERROR;
          break;
        }
        dsize += part;
      } while (strm_.avail_out == 0);
      if (BZ_STREAM_END == state) {
        break;
      }
    }
    BZ2_bzDecompressEnd(&strm_);
    return {BZ_STREAM_END == state, dsize};
  }

  [[nodiscard]] auto EncodeCompare(File_t& in, const uint64_t size, File_t& dest, const bool compare) noexcept -> std::tuple<bool, int64_t> {
    const auto [encode_succes, encoded_length]{EncodeCompare_(in, size, dest, compare)};
    BZ2_bzCompressEnd(&strm_);
    return {encode_succes, encoded_length};
  }

  void HeaderEncode(iEncoder_t& coder, const uint32_t decoded_length) const noexcept {
    zheader_.Encode(coder, decoded_length);
  }

private:
  ZHeader_t zheader_{};
  int32_t : 32;  // Padding
  bz_stream strm_{};

  std::array<uint8_t, BLOCK_SIZE> zin_{};
  std::array<uint8_t, BLOCK_SIZE> zout_{};

  [[nodiscard]] auto EncodeCompare_(File_t& in, const uint64_t size, File_t& dest, const bool compare) noexcept -> std::tuple<bool, int64_t> {
    memset(&strm_, 0, sizeof(strm_));
    const auto bzlevel{(std::abs)(zheader_.Config().clevel)};  // -1..-9 -> 1..9
    if (BZ_OK != BZ2_bzCompressInit(&strm_, bzlevel, 0, 0)) {
      return {false, 0};
    }
    uint64_t usize{size};
    uint32_t position{0};
    uint32_t next_pos{UINT32_C(~0)};
    uint32_t next_count{0};
    uint32_t diff_count{0};
    int32_t status{BZ_OK};
    do {
      strm_.avail_in = static_cast<uint32_t>(in.Read(zin_.data(), (std::min)(uint64_t(BLOCK_SIZE), usize)));
      status = usize < BLOCK_SIZE ? BZ_FINISH : BZ_RUN;
      usize = usize - strm_.avail_in;
      strm_.next_in = reinterpret_cast<char*>(zin_.data());
      do {
        strm_.avail_out = BLOCK_SIZE;
        strm_.next_out = reinterpret_cast<char*>(zout_.data());
        const auto ret{BZ2_bzCompress(&strm_, status)};
        if (ret < BZ_OK) {
          return {false, 0};  // Any kind of failure
        }
        const auto have{BLOCK_SIZE - strm_.avail_out};
        if (have > 0) {
          if (compare) {
            for (uint32_t n{0}; n < have; ++n) {
              const auto a{dest.getc()};
              const auto b{zout_[n]};
              if (a != b) {
                if (diff_count < DIFF_COUNT_LIMIT) {
                  zheader_.diffPos(diff_count, position + n);
                  zheader_.diffByte(diff_count, static_cast<uint8_t>(a));
                  ++diff_count;
                } else {
                  return {false, 0};  // To many differences...
                }
              }
            }
          } else {
            while ((next_count < diff_count) && (next_pos >= position) && (next_pos < (position + have))) {
              assert(next_count < diff_count);
              zout_[next_pos - position] = zheader_.diffByte(next_count);  // Correct output
              next_pos = zheader_.diffPos(++next_count);
            }
            const auto skip_header{(0 == position) ? BZ2_filter::BZ2_HEADER : UINT32_C(0)};
            if ((have - skip_header) != dest.Write(zout_.data() + skip_header, have - skip_header)) {
              return {false, 0};
            }
          }
          position += have;
        }
      } while (0 != strm_.avail_in);
    } while (BZ_FINISH != status);
    if (compare) {
      zheader_.DiffCount(static_cast<uint8_t>(diff_count));
    }
    return {BZ_FINISH == status, (static_cast<int64_t>(strm_.total_out_hi32) << 32) | strm_.total_out_lo32};
  }
};

auto EncodeGZip(File_t& in, const int64_t size_, File_t& out) noexcept -> bool {
  const auto safe_pos{out.Position()};

  Config_t config{};
  config.Read(in);

  const auto size{size_t(size_) - config.Length()};
  if (config.clevel > 0) {
    if (!config.memLevel) {
      const auto header{std::make_unique<ZHeader_t>(config, in)};
      if (GZip_OK == gzip::Zip(in, uint32_t(size), out, static_cast<uint8_t>(config.clevel))) {
        if (config.diffCount > 0) {
          const auto done_pos{out.Position()};
          for (uint32_t n{0}; n < config.diffCount; ++n) {
            const auto pos{header->diffPos(n)};
            out.Seek(safe_pos + pos);
            const auto byte{header->diffByte(n)};
            out.putc(byte);
          }
          out.Seek(done_pos);
        }
        return true;
      }
    } else {
      const auto zlib{std::make_unique<ZLib_t>(config, in)};
      const auto [state, length]{zlib->Deflate(in, size, out, false)};
      if (Z_OK == state) {
        return true;
      }
    }
  } else {
    const auto bzip{std::make_unique<BZip2_t>(config, in)};
    const auto [state, length]{bzip->EncodeCompare(in, size, out, false)};
    if (state) {
      return true;
    }
  }
  return false;
}

auto DecodeEncodeCompare(File_t& stream,                        //
                         iEncoder_t* const coder,               //
                         const int64_t safe_pos,                //
                         const int64_t compressed_data_length,  //
                         const uint32_t uncompressed_data_length) noexcept -> int64_t {
  if (compressed_data_length > 0) {
    stream.Seek(safe_pos);
    File_t inflate_tmp /*("_inflate_tmp_.bin", "wb+")*/;
    auto length{uint32_t(compressed_data_length)};

#if 0
        __________________.__
       /  _____/\____    /|__|_____
      /   \  ___  /     / |  \____ \
      \    \_\  \/     /_ |  |  |_> >
       \______  /_______ \|__|   __/
              \/        \/   |__|
#endif
    {
      if (GZip_OK == gzip::Unzip(stream, inflate_tmp, length)) {  // Try to decode using GZIP
        inflate_tmp.Rewind();
        stream.Seek(safe_pos);
        auto decoded_length{uint32_t(inflate_tmp.Size())};
        if ((0 != uncompressed_data_length) && (decoded_length != uncompressed_data_length)) {
          decoded_length = uncompressed_data_length;
        }
        const auto [zheader, encoded_length, clevel]{GZipEncodeCompare(inflate_tmp, decoded_length, stream)};
        if (nullptr != zheader) {
          if (nullptr != coder) {
            zheader->Encode(*coder, decoded_length);
            inflate_tmp.Rewind();
            for (int32_t ch; EOF != (ch = inflate_tmp.getc());) {
              coder->Compress(ch);
            }
          }
          delete zheader;
          return length;  // Success
        }
      }
    }

#if 0
      __________.____    ._____.
      \____    /|    |   |__\_ |__
        /     / |    |   |  || __ \
       /     /_ |    |___|  || \_\ \
      /_______ \|_______ \__||___  /
              \/        \/       \/
#endif
    {
      const auto zlib{std::make_unique<ZLib_t>()};

      struct Table_t final {
        const int8_t windowBits;
        const uint8_t flush;
      };

      constexpr std::array<const Table_t, 3> table{{{MAX_WBITS, Z_FINISH}, {-MAX_WBITS, Z_FINISH}, {0, Z_NO_FLUSH}}};

      for (const auto& elem : table) {
        inflate_tmp.Rewind();
        stream.Seek(safe_pos);
        const auto [decode_succes, decoded_length]{zlib->Inflate(stream, uint32_t(compressed_data_length), inflate_tmp, elem.windowBits, elem.flush)};  // Try to decode using ZLIB
        if (decode_succes) {
          inflate_tmp.Rewind();
          stream.Seek(safe_pos);
          const auto [encode_succes, encoded_length]{zlib->EncodeCompare(inflate_tmp, decoded_length, stream)};
          if (encode_succes) {
            if (nullptr != coder) {
              zlib->HeaderEncode(*coder, decoded_length);
              inflate_tmp.Rewind();
              for (int32_t ch; EOF != (ch = inflate_tmp.getc());) {
                coder->Compress(ch);
              }
            }
            return length;  // Success
          }
        }
      }
    }

#if 0
      ____________________.__       ________
      \______   \____    /|__|_____ \_____  \
       |    |  _/ /     / |  \____ \ /  ____/
       |    |   \/     /_ |  |  |_> >       \
       |______  /_______ \|__|   __/\_______ \
              \/        \/   |__|           \/
#endif
    if (safe_pos >= BZ2_filter::BZ2_HEADER) {
      const auto bzip{std::make_unique<BZip2_t>()};
      inflate_tmp.Rewind();
      stream.Seek(safe_pos - BZ2_filter::BZ2_HEADER);
      const auto [decode_succes, decoded_length]{bzip->Decompress(stream, uint32_t(compressed_data_length), inflate_tmp)};  // Try to decode using BZip2
      if (decode_succes) {
        inflate_tmp.Rewind();
        stream.Seek(safe_pos - BZ2_filter::BZ2_HEADER);
        const auto [encode_succes, encoded_length]{bzip->EncodeCompare(inflate_tmp, decoded_length, stream, true)};
        if (encode_succes) {
          if (nullptr != coder) {
            bzip->HeaderEncode(*coder, decoded_length);
            inflate_tmp.Rewind();
            for (int32_t ch; EOF != (ch = inflate_tmp.getc());) {
              coder->Compress(ch);
            }
          }
          return length;  // Success
        }
      }
    }

#if 0
    fprintf(stderr, "Failure in <%s> at %" PRIu64 ", length %" PRIu64 "\n", GetInFileName(), safe_pos, compressed_data_length);
#endif
  }

  if (nullptr != coder) {
    coder->CompressN(32, iFilter_t::_DEADBEEF);  // Failure...
  }
  stream.Seek(safe_pos);
  return 0;
}
