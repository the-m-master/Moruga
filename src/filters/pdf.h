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
 * @class PDF_filter
 * @brief Handling the PDF filter
 *
 * Handling the PDF filter
 */
class PDF_filter final : public iFilter_t {
public:
  explicit PDF_filter(File_t& stream, iEncoder_t* const coder, DataInfo_t& di) noexcept;
  virtual ~PDF_filter() noexcept override;

  PDF_filter() = delete;
  PDF_filter(const PDF_filter&) = delete;
  PDF_filter(PDF_filter&&) = delete;
  PDF_filter& operator=(const PDF_filter&) = delete;
  PDF_filter& operator=(PDF_filter&&) = delete;

  virtual auto Handle(int32_t ch) noexcept -> bool final;                // encoding
  virtual auto Handle(int32_t ch, int64_t& pos) noexcept -> bool final;  // decoding

private:
  File_t& _stream;
  iEncoder_t* const _coder;
  DataInfo_t& _di;
  int32_t _block_length{0};
  uint32_t _length{0};
  File_t* _data{nullptr};
};
