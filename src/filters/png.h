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
class Buffer_t;
class File_t;
class iEncoder_t;

/**
 * @class PNG_filter
 * @brief Handling the PNG filter
 *
 * Handling the PNG filter
 */
class PNG_filter final : public iFilter_t {
public:
  explicit PNG_filter(File_t& stream, iEncoder_t* const coder, DataInfo_t& di, const Buffer_t& __restrict buf) noexcept;
  virtual ~PNG_filter() noexcept override;

  PNG_filter() = delete;
  PNG_filter(const PNG_filter&) = delete;
  PNG_filter(PNG_filter&&) = delete;
  PNG_filter& operator=(const PNG_filter&) = delete;
  PNG_filter& operator=(PNG_filter&&) = delete;

  virtual auto Handle(int32_t ch) noexcept -> bool final;                // encoding
  virtual auto Handle(int32_t ch, int64_t& pos) noexcept -> bool final;  // decoding

private:
  const Buffer_t& __restrict _buf;
  File_t& _stream;
  iEncoder_t* const _coder;
  DataInfo_t& _di;
  int32_t _block_length{0};
  uint32_t _length{0};
  File_t* _data{nullptr};
};
