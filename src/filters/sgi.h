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
 * along with this program; see the file COPYING3.
 * If not, see <https://www.gnu.org/licenses/>
 */
#ifndef _SGI_HDR_
#define _SGI_HDR_

#include <cstdint>
#include "filter.h"
class File_t;
class iEncoder_t;

class SGI_filter final : public iFilter_t {
public:
  explicit SGI_filter(File_t& stream, iEncoder_t& coder, DataInfo_t& di);
  virtual ~SGI_filter() noexcept override;

  SGI_filter() = delete;
  SGI_filter(const SGI_filter&) = delete;
  SGI_filter(SGI_filter&&) = delete;
  SGI_filter& operator=(const SGI_filter&) = delete;
  SGI_filter& operator=(SGI_filter&&) = delete;

  virtual auto Handle(int32_t ch) noexcept -> bool final;                // encoding
  virtual auto Handle(int32_t ch, int64_t& pos) noexcept -> bool final;  // decoding

private:
  File_t& _stream;
  iEncoder_t& _coder;
  DataInfo_t& _di;

  uint32_t* _base{nullptr};
  uint8_t* _dst{nullptr};
  uint32_t _length{0};
  int32_t _prev_rgba{0};
};

#endif /* _SGI_HDR_ */
