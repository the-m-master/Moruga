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
#include "wav.h"
#include <cstdint>
#include "File.h"
#include "filter.h"
#include "iEncoder.h"

auto Header_t::ScanWAV(int32_t /*ch*/) noexcept -> Filter {
  // ------------------------------------------------------------------------
  // WAV header
  // -------+------+---------------------------------------------------------
  // offset | size | description
  // -------+------+---------------------------------------------------------
  //      0 |   4  | The ASCII text string 'RIFF'
  //      4 |   4  | Length of File (incl. Header) in Bytes, reduced by 8
  //      8 |   4  | The ASCII text string 'WAVE'
  //     12 |   4  | The ASCII text string 'fmt ' (The space is also included)
  //     16 |   4  | fix: Dec. 16 = Hex 10 00 00 00
  //     20 |   2  | Type of WAVE format. This is a PCM header = 01 (linear quantisation)
  //     22 |   2  | No. of Channels: 1 = Mono, 2 = Stereo
  //     24 |   4  | Samples pro second
  //     28 |   4  | Bytes pro second
  //     32 |   2  | Block alignment (number of bytes in elementary quantisation)
  //     34 |   2  | Bits pro sample
  //     36 |   4  | ASCII "data"
  //     40 |   4  | Length of the following data field in bytes
  //     44 |   ...

  static constexpr uint32_t offset{44};

  if (('RIFF' == m4(offset - 0)) && ('WAVE' == m4(offset - 8))) {
    const auto length{int32_t(i4(offset - 4))};
    const auto nChannels{i2(offset - 22)};
    const auto bitsProSample{i2(offset - 34)};
    if ((length > 0) && (nChannels >= 1) && (nChannels <= 8)) {
      if ((8 == bitsProSample) || (16 == bitsProSample) || (24 == bitsProSample) || (32 == bitsProSample)) {
        _di.cycles = uint32_t((nChannels * bitsProSample) / 8);
        if ('data' == m4(offset - 36)) {
          _di.filter_end = int32_t(i4(offset - 40));
          _di.seekdata = false;
        } else {
          _di.filter_end = length;
          _di.seekdata = true;
        }
#if 0
        fprintf(stderr, "WAV %d, %d, %d   \n", nChannels, bitsProSample, length);
        fflush(stderr);
#endif
        return Filter::WAV;
      }
    }
  }

  return Filter::NOFILTER;
}

WAV_filter::WAV_filter(File_t& stream, iEncoder_t& coder, DataInfo_t& di)
    : _stream{stream},  //
      _coder{coder},
      _di{di} {}

WAV_filter::~WAV_filter() noexcept = default;

void WAV_filter::seekData(const int32_t c) noexcept {
  _data = (_data << 8) | uint32_t(c);
  if (_getLength) {
    --_getLength;
    if (0 == _getLength) {
      auto* const tmp = reinterpret_cast<uint8_t*>(&_data);
      const int32_t length{(tmp[0] << 24) | (tmp[1] << 16) | (tmp[2] << 8) | (tmp[3])};
      if (_di.filter_end > length) {
        _di.filter_end = length;
      } else {  // Reset, failure
        _di.filter_end = 0;
        _di.cycles = 0;
        _cycle = 0;
        _data = 0;
      }
      _di.seekdata = false;
    }
  } else if ('data' == _data) {
    _getLength = 4;
  }
}

auto WAV_filter::Handle(int32_t ch) noexcept -> bool {  // encoding
  if (_di.seekdata) {
    seekData(ch);
    _coder.Compress(ch);
  } else {
    const auto org{int8_t(ch)};
    _coder.Compress(org - _delta[_cycle]);
    _delta[_cycle] = org;
    _cycle++;
    if (_cycle >= _di.cycles) {
      _cycle = 0;
    }
  }
  return true;
}

auto WAV_filter::Handle(int32_t ch, int64_t& /*pos*/) noexcept -> bool {  // decoding
  if (_di.seekdata) {
    seekData(ch);
    _stream.putc(ch);
  } else {
    const auto org{int8_t(int8_t(ch) + _delta[_cycle])};
    _stream.putc(org);
    _delta[_cycle] = org;
    _cycle++;
    if (_cycle >= _di.cycles) {
      _cycle = 0;
    }
  }
  return true;
}
