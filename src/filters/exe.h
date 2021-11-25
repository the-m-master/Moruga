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
 * along with this program; see the file LICENSE.
 * If not, see <https://www.gnu.org/licenses/>
 */
#ifndef _EXE_HDR_
#define _EXE_HDR_

#include <array>
#include <cstdint>
#include "filter.h"
class File_t;
class iEncoder_t;

class EXE_filter final : public iFilter_t {
public:
  explicit EXE_filter(File_t& stream, iEncoder_t& coder, const DataInfo_t& di);
  virtual ~EXE_filter() noexcept override;

  EXE_filter() = delete;
  EXE_filter(const EXE_filter&) = delete;
  EXE_filter(EXE_filter&&) = delete;
  EXE_filter& operator=(const EXE_filter&) = delete;
  EXE_filter& operator=(EXE_filter&&) = delete;

  virtual auto Handle(int32_t ch) noexcept -> bool final;                // encoding
  virtual auto Handle(int32_t ch, int64_t& pos) noexcept -> bool final;  // decoding

private:
  void detect(const int32_t ch) noexcept;

  File_t& _stream;
  iEncoder_t& _coder;

  bool _transform{false};
  int32_t : 24;  // Padding
  int32_t _location{0};
  uint32_t _length{0};
  int32_t _oldc{0};
  std::array<uint8_t, 8> _addr{};
};

#endif /* _EXE_HDR_ */
