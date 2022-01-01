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
#ifndef _LZX_HDR_
#define _LZX_HDR_

#include <cstdint>
#include "filter.h"
class File_t;
class iEncoder_t;

class LZX_filter final : public iFilter_t {
public:
  explicit LZX_filter(File_t& stream, iEncoder_t* const coder, DataInfo_t& di);
  virtual ~LZX_filter() noexcept override;

  LZX_filter() = delete;
  LZX_filter(const LZX_filter&) = delete;
  LZX_filter(LZX_filter&&) = delete;
  LZX_filter& operator=(const LZX_filter&) = delete;
  LZX_filter& operator=(LZX_filter&&) = delete;

  virtual auto Handle(int32_t ch) noexcept -> bool final;                // encoding
  virtual auto Handle(int32_t ch, int64_t& pos) noexcept -> bool final;  // decoding

private:
  File_t& _stream;
  iEncoder_t* const _coder;
  DataInfo_t& _di;
};

#endif /* _LZX_HDR_ */
