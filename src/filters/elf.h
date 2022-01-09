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
 */
#ifndef _ELF_HDR_
#define _ELF_HDR_

#include <array>
#include <cstdint>
#include "filter.h"
class File_t;
class iEncoder_t;

class ELF_filter final : public iFilter_t {
public:
  explicit ELF_filter(File_t& stream, iEncoder_t* const coder, DataInfo_t& di) noexcept;
  virtual ~ELF_filter() noexcept override;

  ELF_filter() = delete;
  ELF_filter(const ELF_filter&) = delete;
  ELF_filter(ELF_filter&&) = delete;
  ELF_filter& operator=(const ELF_filter&) = delete;
  ELF_filter& operator=(ELF_filter&&) = delete;

  virtual auto Handle(int32_t ch) noexcept -> bool final;                // encoding
  virtual auto Handle(int32_t ch, int64_t& pos) noexcept -> bool final;  // decoding

private:
  static constexpr uint8_t mru_escape{0xFE};  // May NOT be 0x00 or 0xFF

  auto update_mru(int32_t* const mru, const int32_t addr) const noexcept -> int32_t;

  void detect(const int32_t ch) noexcept;

  File_t& _stream;
  iEncoder_t* const _coder;
  DataInfo_t& _di;

  bool _transform{false};
  int32_t : 24;  // Padding
  int32_t _location{0};
  uint32_t _length{0};
  int32_t _oldc{0};
  std::array<uint8_t, 8> _addr{};
  std::array<int32_t, 256> _call_mru{};  // Most recently used E8
  std::array<int32_t, 256> _jump_mru{};  // Most recently used E9 or 0F 8x
};

#endif /* _ELF_HDR_ */
