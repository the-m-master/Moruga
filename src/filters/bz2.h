/* Filter, is a binary preparation for encoding/decoding
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

#include <cstdint>
#include "filter.h"
class File_t;
class iEncoder_t;

/**
 * @class BZ2_filter
 * @brief Handling BZip2 files
 *
 * Handling BZip2 files
 */
class BZ2_filter final : public iFilter_t {
public:
  explicit BZ2_filter(File_t& stream, iEncoder_t* const coder, DataInfo_t& di, const int64_t original_length) noexcept;
  virtual ~BZ2_filter() noexcept override;

  BZ2_filter() = delete;
  BZ2_filter(const BZ2_filter&) = delete;
  BZ2_filter(BZ2_filter&&) = delete;
  BZ2_filter& operator=(const BZ2_filter&) = delete;
  BZ2_filter& operator=(BZ2_filter&&) = delete;

  static constexpr auto BZ2_HEADER{UINT32_C(9)};

  virtual auto Handle(int32_t ch) noexcept -> bool final;                // encoding
  virtual auto Handle(int32_t ch, int64_t& pos) noexcept -> bool final;  // decoding

private:
  const int64_t _original_length;
  File_t& _stream;
  iEncoder_t* const _coder;
  DataInfo_t& _di;
  int32_t _block_length{0};
  uint32_t _length{0};
  File_t* _data{nullptr};
};
