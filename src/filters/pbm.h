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
#ifndef _PBM_HDR_
#define _PBM_HDR_

#include <array>
#include <cstdint>
#include "filter.h"
class File_t;
class iEncoder_t;

class PBM_filter final : public iFilter_t {
public:
  explicit PBM_filter(File_t& stream, iEncoder_t* const coder, DataInfo_t& di);
  virtual ~PBM_filter() noexcept override;

  PBM_filter() = delete;
  PBM_filter(const PBM_filter&) = delete;
  PBM_filter(PBM_filter&&) = delete;
  PBM_filter& operator=(const PBM_filter&) = delete;
  PBM_filter& operator=(PBM_filter&&) = delete;

  virtual auto Handle(int32_t ch) noexcept -> bool final;                // encoding
  virtual auto Handle(int32_t ch, int64_t& pos) noexcept -> bool final;  // decoding

private:
  File_t& _stream;
  iEncoder_t* const _coder;
  const DataInfo_t& _di;
  uint32_t _length{0};
  std::array<int8_t, 4> _rgba{};
  std::array<int8_t, 4> _prev_rgba{};
  int32_t : 32;  // Padding
};

#endif /* _PBM_HDR_ */
