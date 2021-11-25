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
#ifndef _WAV_HDR_
#define _WAV_HDR_

#include <array>
#include <cstdint>
#include "filter.h"
class File_t;
class iEncoder_t;

class WAV_filter final : public iFilter_t {
public:
  explicit WAV_filter(File_t& stream, iEncoder_t& coder, DataInfo_t& di);
  virtual ~WAV_filter() noexcept override;

  WAV_filter() = delete;
  WAV_filter(const WAV_filter&) = delete;
  WAV_filter(WAV_filter&&) = delete;
  WAV_filter& operator=(const WAV_filter&) = delete;
  WAV_filter& operator=(WAV_filter&&) = delete;

  virtual auto Handle(int32_t ch) noexcept -> bool final;                // encoding
  virtual auto Handle(int32_t ch, int64_t& pos) noexcept -> bool final;  // decoding

private:
  void seekData(const int32_t c) noexcept;

  File_t& _stream;
  iEncoder_t& _coder;
  DataInfo_t& _di;

  uint32_t _data{0};
  uint32_t _cycle{0};
  uint32_t _getLength{0};
  std::array<int8_t, 36> _delta{};
};

#endif /* _WAV_HDR_ */
