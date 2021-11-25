/* GIF, encoding and decoding gif-lzw
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
 * along with this program; see the file LICENSE.
 * If not, see <https://www.gnu.org/licenses/>
 */
#ifndef _GIF_HDR_
#define _GIF_HDR_

#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include "filter.h"
class Buffer_t;
class File_t;
class iEncoder_t;

class Gif_t final {
public:
  explicit Gif_t(File_t& in, File_t& out);
  virtual ~Gif_t() noexcept;

  int64_t Decode() noexcept;
  int64_t Encode(int64_t size, const bool compare) noexcept;

private:
  Gif_t() = delete;
  Gif_t(const Gif_t& other) = delete;
  Gif_t(Gif_t&& other) = delete;
  Gif_t& operator=(const Gif_t& other) = delete;
  Gif_t& operator=(Gif_t&& other) = delete;

  auto FindMatch(const int32_t k) noexcept -> int32_t;
  auto WriteBlock(int32_t count, const bool compare) noexcept -> bool;
  auto WriteCode(const int32_t code, const bool compare) noexcept -> bool;

  static constexpr uint16_t LZW_TABLE_SIZE{9221};

  File_t& _in;
  File_t& _out;

  int64_t _diffFound{0};
  int64_t _outsize{1};
  size_t _diffPos{0};

  int32_t _bits{0};
  int32_t _block_size{0};
  int32_t _bsize{0};
  int32_t _buffer{0};
  int32_t _clearPos{0};
  int32_t _code{0};
  int32_t _codeSize{0};
  int32_t _offset{0};
  int32_t _shift{0};

  int32_t _bsize_index{0};
  std::string _bsizes{};

  std::array<int32_t, 4096> _dict{};
  std::array<int32_t, LZW_TABLE_SIZE> _table{};
  std::array<uint8_t, 4096> _output{};
  int32_t : 32;  // Padding
};

class GIF_filter final : public iFilter_t {
public:
  explicit GIF_filter(File_t& stream, iEncoder_t& coder, DataInfo_t& di, const Buffer_t& __restrict buf, const int64_t original_length);
  virtual ~GIF_filter() noexcept override;

  GIF_filter() = delete;
  GIF_filter(const GIF_filter&) = delete;
  GIF_filter(GIF_filter&&) = delete;
  GIF_filter& operator=(const GIF_filter&) = delete;
  GIF_filter& operator=(GIF_filter&&) = delete;

  virtual auto Handle(int32_t ch) noexcept -> bool final;                // encoding
  virtual auto Handle(int32_t ch, int64_t& pos) noexcept -> bool final;  // decoding

private:
  enum Frames_t {
    PLAINTEXT_EXTENSION = 0x01,
    GRAPHIC_CONTROL = 0xF9,
    COMMENT_EXTENSION = 0xFE,
    APPLICATION_EXTENSION = 0xFF,

    EXTENSION_INTRODUCER = 0x21,  // !
    IMAGE_DESCRIPTOR = 0x2C,      // ,
    TRAILER = 0x3B                // ;
  };

  auto read_sub_blocks(bool& eof) const noexcept -> int32_t;
  auto get_frame(bool& eof) const noexcept -> int32_t;

  const Buffer_t& __restrict _buf;
  const int64_t _original_length;
  File_t& _stream;
  iEncoder_t& _coder;
  DataInfo_t& _di;

  File_t* _gif_raw{nullptr};
  int32_t _imageEnd{0};
  int32_t _length{0};  // Must be signed in case of GIF failure
  int32_t _gif_phase{0};
  int32_t _gif_length{0};

  bool _gif{true};
  int32_t : 24;  // Padding
  bool _frame_just_decoded{false};
  int32_t : 24;  // Padding
};

#endif /* _GIF_HDR_ */
