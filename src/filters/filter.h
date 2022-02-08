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
 *
 * https://github.com/the-m-master/Moruga
 */
#ifndef _FILTER_HDR_
#define _FILTER_HDR_

#include <cstdint>
#include "Buffer.h"
#include "IntegerXXL.h"
class File_t;
class iEncoder_t;

enum class Filter {
  NOFILTER = 0,
  BMP,
  ELF,
  EXE,
  GIF,
  GZP,
  LZX,
  PBM,
  PDF,
  PKZ,
  PNG,
  SGI,
  TGA,
  TIF,
  WAV,
};

class iFilter_t {
public:
  explicit iFilter_t() noexcept = default;
  virtual ~iFilter_t() noexcept;
  iFilter_t(const iFilter_t&) = delete;
  iFilter_t(iFilter_t&&) = delete;
  iFilter_t& operator=(const iFilter_t&) = delete;
  iFilter_t& operator=(iFilter_t&&) = delete;

  [[nodiscard]] virtual auto Handle(int32_t ch) noexcept -> bool = 0;                // encoding
  [[nodiscard]] virtual auto Handle(int32_t ch, int64_t& pos) noexcept -> bool = 0;  // decoding

  static auto Create(const Filter& type) noexcept -> iFilter_t*;

  static const auto _DEADBEEF{UINT32_C(0xDEADBEEF)};
};

struct DataInfo_t {
  uint128_t tag{0};

  int32_t offset_to_start{0};
  int32_t filter_end{0};
  uint32_t bytes_per_pixel{0};
  uint32_t padding_bytes{0};
  uint32_t image_width{0};
  uint32_t image_height{0};

  uint32_t pkzippos{0};
  int32_t pkziplen{0};
  uint32_t cycles{0};

  bool lzw_encoded{false};
  bool seekdata{false};
  uint8_t clss{0};
  uint8_t flags{0};  // GZ flags

  int32_t location{0};

  uint32_t resetIntervalBits{0};
  uint8_t windowSizeBits{0};

  int32_t : 24;  // Padding
  int32_t : 32;  // Padding
  int32_t : 32;  // Padding
  int32_t : 32;  // Padding
};

class Header_t final {
public:
  explicit Header_t(const Buffer_t& __restrict buf, DataInfo_t& __restrict di, const bool encode) noexcept;
  virtual ~Header_t() noexcept;

  Header_t() = delete;
  Header_t(const Header_t&) = delete;
  Header_t(Header_t&&) = delete;
  Header_t& operator=(Header_t&) = delete;
  Header_t& operator=(Header_t&&) = delete;

  auto ScanBMP(int32_t ch) noexcept -> Filter;
  auto ScanELF(int32_t ch) noexcept -> Filter;
  auto ScanEXE(int32_t ch) noexcept -> Filter;
  auto ScanGIF(int32_t ch) noexcept -> Filter;
  auto ScanGZP(int32_t ch) noexcept -> Filter;
  auto ScanLZX(int32_t ch) noexcept -> Filter;
  auto ScanPBM(int32_t ch) noexcept -> Filter;
  auto ScanPDF(int32_t ch) noexcept -> Filter;
  auto ScanPKZ(int32_t ch) noexcept -> Filter;
  auto ScanPNG(int32_t ch) noexcept -> Filter;
  auto ScanSGI(int32_t ch) noexcept -> Filter;
  auto ScanTGA(int32_t ch) noexcept -> Filter;
  auto ScanTIF(int32_t ch) noexcept -> Filter;
  auto ScanWAV(int32_t ch) noexcept -> Filter;
  auto Scan(int32_t ch) noexcept -> Filter;

private:
  // clang-format off

  // 16-bits little endian (Intel) number at buf(i-1)..buf(i)
  [[nodiscard]] constexpr auto i2(const uint32_t i) const noexcept -> uint16_t { return static_cast<uint16_t>(_buf(i) | (_buf(i - 1) << 8)); }

  // 16-bits big endian (Motorola) number at buf(i-1)..buf(i)
  [[nodiscard]] constexpr auto m2(const uint32_t i) const noexcept -> uint16_t { return static_cast<uint16_t>(_buf(i - 1) | (_buf(i) << 8)); }

  // 32-bits little endian (Intel) number at buf(i-3)..buf(i)
  [[nodiscard]] constexpr auto i4(const uint32_t i) const noexcept -> uint32_t { return static_cast<uint32_t>(_buf(i) | (_buf(i - 1) << 8) | (_buf(i - 2) << 16) | (_buf(i - 3) << 24)); }

  // 32-bits big endian (Motorola) number at buf(i-3)..buf(i)
  [[nodiscard]] constexpr auto m4(const uint32_t i) const noexcept -> uint32_t { return static_cast<uint32_t>(_buf(i - 3) | (_buf(i - 2) << 8) | (_buf(i - 1) << 16) | (_buf(i) << 24)); }

  // 64-bits little endian (Intel) number at buf(i-7)..buf(i)
  [[nodiscard]] constexpr auto i8(const uint32_t i) const noexcept -> uint64_t { return static_cast<uint64_t>(i4(i)) | (static_cast<uint64_t>(i4(i - 4)) << 32); }

  // clang-format on

  const Buffer_t& __restrict _buf;
  DataInfo_t& __restrict _di;
  const bool _encode;
  int32_t : 24;  // Padding
  int32_t : 32;  // Padding
};

class Filter_t final {
public:
  explicit Filter_t(const Buffer_t& __restrict buf, const int64_t original_length, File_t& stream, iEncoder_t* encoder) noexcept;
  virtual ~Filter_t() noexcept;

  Filter_t() = delete;
  Filter_t(const Filter_t&) = delete;
  Filter_t(Filter_t&&) = delete;
  Filter_t& operator=(const Filter_t&) = delete;
  Filter_t& operator=(Filter_t&&) = delete;

  auto Scan(int32_t ch) noexcept -> bool;                // encoding
  auto Scan(int32_t ch, int64_t& pos) noexcept -> bool;  // decoding

private:
  auto Create(const Filter& filter) noexcept -> iFilter_t*;

  const Buffer_t& __restrict _buf;
  const int64_t _original_length;
  File_t& _stream;
  iEncoder_t* const _encoder;
  iFilter_t* _filter{nullptr};
  Header_t* _header{nullptr};
  int32_t : 32;  // Padding
  int32_t : 32;  // Padding
  DataInfo_t _di{};
};

#endif  // _FILTER_HDR_
