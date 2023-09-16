/* Buffer, history handling using buffer
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
#pragma once

#include <cassert>
#include <cstring>
#include "File.h"

#define ISPOWEROF2(x) (((x) > 1) && (!((x) & ((x)-1))))

/**
 * @class Buffer_t
 * @brief Handling the read source information
 *
 * Handling the read source information
 */
class Buffer_t final {
public:
  explicit Buffer_t() noexcept
      : _mask{1024 - 1},  // Initially claim one KiB, increase this with Resize later on
        _buffer{static_cast<uint8_t*>(std::calloc(_mask + 1, sizeof(uint8_t)))} {}
  ~Buffer_t() noexcept {
    std::free(_buffer);
    _buffer = nullptr;
  }
  Buffer_t(const Buffer_t&) = delete;
  Buffer_t(Buffer_t&&) = delete;
  auto operator=(const Buffer_t&) -> Buffer_t& = delete;
  auto operator=(Buffer_t&&) -> Buffer_t& = delete;

  // buf[i] returns a reference to the i'th byte with wrap
  [[nodiscard]] ALWAYS_INLINE constexpr auto operator[](const uint32_t i) const noexcept -> uint8_t& {
    return _buffer[i & _mask];
  }

  // buf(i) returns i'th byte back from pos
  [[nodiscard]] ALWAYS_INLINE constexpr auto operator()(const uint32_t i) const noexcept -> uint8_t {
    return _buffer[(_pos - i) & _mask];
  }

  ALWAYS_INLINE constexpr void Add(const uint8_t ch) noexcept {
    _buffer[_pos++ & _mask] = ch;
  }

  [[nodiscard]] ALWAYS_INLINE constexpr auto Pos() const noexcept -> uint32_t {
    return _pos;
  }

  void Resize(const uint64_t max_file_size, const uint64_t max_memory) noexcept {
    // Increasing the buffer size above the file length is not useful
    static constexpr auto mem_limit{UINT64_C(0x40000000)};  // 1 GiB

    auto max_size{uint64_t(_mask) + UINT64_C(1)};
    while (max_size < mem_limit) {
      if ((max_size >= max_file_size) || (max_size >= max_memory)) {
        break;
      }
      max_size += max_size;
    }
    uint8_t* const new_buf{static_cast<uint8_t*>(std::calloc(max_size, sizeof(uint8_t)))};
    memcpy(new_buf, _buffer, static_cast<size_t>(_mask) + UINT64_C(1));
    std::free(_buffer);
    _buffer = new_buf;
    _mask = static_cast<uint32_t>(max_size - UINT64_C(1));
  }

  // 16-bits little endian, number at buf(i-1)..buf(i)
  [[nodiscard]] constexpr auto i2(const uint32_t i) const noexcept -> uint16_t {
    return static_cast<uint16_t>(operator()(i) | (operator()(i - 1) << 8));
  }
  // 16-bits big endian, number at buf(i-1)..buf(i)
  [[nodiscard]] constexpr auto m2(const uint32_t i) const noexcept -> uint16_t {
    return static_cast<uint16_t>(operator()(i - 1) | (operator()(i) << 8));
  }

  // 32-bits little endian, number at buf(i-3)..buf(i)
  [[nodiscard]] constexpr auto i4(const uint32_t i) const noexcept -> uint32_t {
    return static_cast<uint32_t>(i2(i)) | (static_cast<uint32_t>(i2(i - 2)) << 16);
  }
  // 32-bits big endian, number at buf(i-3)..buf(i)
  [[nodiscard]] constexpr auto m4(const uint32_t i) const noexcept -> uint32_t {
    return static_cast<uint32_t>(m2(i - 2)) | (static_cast<uint32_t>(m2(i)) << 16);
  }

  // 64-bits little endian, number at buf(i-7)..buf(i)
  [[nodiscard]] constexpr auto i8(const uint32_t i) const noexcept -> uint64_t {
    return static_cast<uint64_t>(i4(i)) | (static_cast<uint64_t>(i4(i - 4)) << 32);
  }
  // 64-bits big endian, number at buf(i-7)..buf(i)
  [[nodiscard]] constexpr auto m8(const uint32_t i) const noexcept -> uint64_t {
    return static_cast<uint64_t>(m4(i - 4)) | (static_cast<uint64_t>(m4(i)) << 32);
  }

private:
  uint32_t _mask{0};
  uint32_t _pos{0};  // Number of input bytes read (NOT wrapped)
  uint8_t* __restrict _buffer{nullptr};
};
