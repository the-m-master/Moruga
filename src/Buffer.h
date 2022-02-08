/* Buffer, history handling using buffer
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
#ifndef _BUFFER_HDR_
#define _BUFFER_HDR_

#include <cassert>
#include "File.h"

#define ISPOWEROF2(x) (((x) > 0) && (!((x) & ((x)-1))))

class Buffer_t final {
public:
  explicit Buffer_t(const uint64_t max_size) noexcept
      : _mask{static_cast<uint32_t>(((max_size > mem_limit) ? mem_limit : max_size) - UINT64_C(1))},  //
        _buffer{static_cast<uint8_t*>(std::calloc(static_cast<size_t>(_mask) + UINT64_C(1), sizeof(uint8_t)))} {
    assert(ISPOWEROF2(max_size));
    // fprintf(stdout, "%" PRIu64 " KiB for Buffer_t\n", (max_size * sizeof(uint8_t)) / UINT64_C(1024));
  }
  ~Buffer_t() noexcept {
    std::free(_buffer);
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

  ALWAYS_INLINE constexpr void Add(uint8_t ch) noexcept {
    _buffer[_pos++ & _mask] = ch;
  }

  [[nodiscard]] ALWAYS_INLINE constexpr auto Pos() const noexcept -> uint32_t {
    return _pos;
  }

private:
  // Increasing the buffer limit above the file size is not useful
  static constexpr auto mem_limit{UINT64_C(0x40000000)};  // 1 GiB

  const uint32_t _mask;
  uint32_t _pos{0};  // Number of input bytes read (is wrapped)
  uint8_t* const __restrict _buffer;
};

#endif  // _BUFFER_HDR_
