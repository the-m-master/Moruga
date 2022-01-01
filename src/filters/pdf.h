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
#ifndef _PDF_HDR_
#define _PDF_HDR_

#include <cstdint>
#include "Buffer.h"
#include "filter.h"
class File_t;
class iEncoder_t;

class PDF_filter final : public iFilter_t {
public:
  explicit PDF_filter(File_t& stream, iEncoder_t* const coder, DataInfo_t& di, const Buffer_t& __restrict buf);
  virtual ~PDF_filter() noexcept override;

  PDF_filter() = delete;
  PDF_filter(const PDF_filter&) = delete;
  PDF_filter(PDF_filter&&) = delete;
  PDF_filter& operator=(const PDF_filter&) = delete;
  PDF_filter& operator=(PDF_filter&&) = delete;

  virtual auto Handle(int32_t ch) noexcept -> bool final;                // encoding
  virtual auto Handle(int32_t ch, int64_t& pos) noexcept -> bool final;  // decoding

private:
  // clang-format off

  // 32-bits big endian (Motorola) number at buf(i-3)..buf(i)
  [[nodiscard]] constexpr auto m4(const uint32_t i) const noexcept -> uint32_t { return uint32_t(_buf(i - 3) | (_buf(i - 2) << 8) | (_buf(i - 1) << 16) | (_buf(i) << 24)); }

  // clang-format on

  const Buffer_t& __restrict _buf;
  File_t& _stream;
  iEncoder_t* const _coder;
  DataInfo_t& _di;
  int32_t _block_length{0};
  uint32_t _length{0};
  File_t* _data{nullptr};
};

#endif /* _PDF_HDR_ */
