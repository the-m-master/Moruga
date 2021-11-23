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
#include "pbm.h"
#include <cassert>
#include <cinttypes>
#include <climits>
#include <cstdint>
#include "Buffer.h"
#include "File.h"
#include "filter.h"
#include "iEncoder.h"

template <typename T>
constexpr auto is_white_space(const T c) noexcept -> bool {
  return ('\n' == c) || (' ' == c);
}

auto Header_t::ScanPBM(int32_t /*ch*/) noexcept -> Filter {
  // ------------------------------------------------------------------------
  // PBM, PGM, PNM and PPM header (https://www.fileformat.info/format/pbm/egff.htm)
  // ------------------------------------------------------------------------
  // Header consists of the following entries, each separated by white space
  // ------------------------------------------------------------------------
  //          P1 | PBM: Literally for ASCII version (ignored by this filter)
  //          P4 | PBM: Literally for binary version, eight pixels per byte
  //             | 0–1 (white & black)
  //          P2 | PGM: Literally for ASCII version (ignored by this filter)
  //          P5 | PGM: Literally for binary version, one pixel per byte
  //             | 0–255 (gray scale), 0–65535 (gray scale), variable, black-to-white range
  //          P3 | PPM: Literally for ASCII version (ignored by this filter)
  //          P6 | PPM: Literally for binary version, three bytes per pixel
  //             | 16777216 (0–255 for each RGB channel), some support for 0-65535 per channel
  // ImageWidth  | Width of image in pixels (ASCII decimal value)
  // ImageHeight | Height of image in pixels (ASCII decimal value)
  // MaxGrey     | Maximum gray/colour value (ASCII decimal value)

  // P4\a<witdh> <height>\a
  // P5\a<witdh> <height>\a<colours>\a
  // P6\a<witdh> <height>\a<colours>\a

  static constexpr uint32_t offset{32};
  static constexpr uint16_t P4{'P4'};
  static constexpr uint16_t P5{'P5'};
  static constexpr uint16_t P6{'P6'};

  const auto sig{m2(offset - 0)};
  if (((P4 == sig) || (P5 == sig) || (P6 == sig)) && is_white_space(_buf(offset - 2))) {
    uint32_t idx{offset - 3};
    while (is_white_space(_buf(idx)) && (idx >= 1)) {
      --idx;
    }
    uint32_t width{0};
    while ((_buf(idx) >= '0') && (_buf(idx) <= '9') && (idx >= 1)) {
      width = (width * 10) + (_buf(idx) - '0');
      --idx;
    }
    if (is_white_space(_buf(idx)) && (idx >= 1) && (width > 0) && (width < 0x8000)) {
      while (is_white_space(_buf(idx)) && (idx >= 1)) {
        --idx;
      }
      uint32_t height{0};
      while ((_buf(idx) >= '0') && (_buf(idx) <= '9') && (idx >= 1)) {
        height = (height * 10) + (_buf(idx) - '0');
        --idx;
      }
      if (is_white_space(_buf(idx)) && (height > 0) && (height < 0x8000)) {
        _di.image_width = width;
        _di.image_height = height;

        if (P4 == sig) {
          _di.bytes_per_pixel = 1;
          _di.offset_to_start = 0;  // start now!
          _di.filter_end = int32_t((width * height) / 8);
          return Filter::PBM;
        }

        while (is_white_space(_buf(idx)) && (idx >= 1)) {
          --idx;
        }
        uint32_t colours{0};
        while ((_buf(idx) >= '0') && (_buf(idx) <= '9') && (idx >= 1)) {
          colours = (colours * 10) + (_buf(idx) - '0');
          --idx;
        }
        if (((0x00FF == colours) || (0xFFFF == colours)) && is_white_space(_buf(idx))) {
          if (P5 == sig) {
            _di.bytes_per_pixel = 1;
            _di.offset_to_start = 0;  // start now!
          } else {
            _di.bytes_per_pixel = 3;
            _di.offset_to_start = int32_t((idx + 1) % _di.bytes_per_pixel);  // Sync with RGB
          }
          _di.filter_end = int32_t(width * height * _di.bytes_per_pixel);
          return Filter::PBM;
        }
      }
    }
  }

  return Filter::NOFILTER;
}

PBM_filter::PBM_filter(File_t& stream, iEncoder_t& coder, DataInfo_t& di)
    : _stream{stream},  //
      _coder{coder},
      _di{di} {}

PBM_filter::~PBM_filter() noexcept {
  if (nullptr != &_coder) {  // encoding
    for (uint32_t n{0}; n < _length; ++n) {
      _coder.Compress(_rgba[n]);
    }
  } else {  // decoding
    for (uint32_t n{0}; n < _length; ++n) {
      _stream.putc(_rgba[n]);
    }
  }
}

auto PBM_filter::Handle(int32_t ch) noexcept -> bool {  // encoding
  _rgba[_length++] = int8_t(ch);

  if (_length >= _di.bytes_per_pixel) {
    _length = 0;

    if (1 == _di.bytes_per_pixel) {
      const auto pixel{_rgba[0]};
      _coder.Compress(pixel - _prev_rgba[0]);
      _prev_rgba[0] = pixel;
    } else {
      const auto b{_rgba[0]};
      const auto g{_rgba[1]};
      const auto r{_rgba[2]};
      const auto x{g};
      const auto y{int8_t(g - r)};
      const auto z{int8_t(g - b)};
      _coder.Compress(x - _prev_rgba[0]);
      _coder.Compress(y - _prev_rgba[1]);
      _coder.Compress(z - _prev_rgba[2]);
      _prev_rgba[0] = x;
      _prev_rgba[1] = y;
      _prev_rgba[2] = z;
    }
  }

  return true;
}

auto PBM_filter::Handle(int32_t ch, int64_t& /*pos*/) noexcept -> bool {  // decoding
  _rgba[_length++] = int8_t(ch);

  if (_length >= _di.bytes_per_pixel) {
    _length = 0;

    if (1 == _di.bytes_per_pixel) {
      const auto pixel{_rgba[0]};
      _prev_rgba[0] += pixel;
      _stream.putc(_prev_rgba[0]);
    } else {
      const auto b{_rgba[0]};
      const auto g{_rgba[1]};
      const auto r{_rgba[2]};
      const auto x{int8_t(b - r)};
      const auto y{b};
      const auto z{int8_t(b - g)};
      _prev_rgba[0] += x;
      _prev_rgba[1] += y;
      _prev_rgba[2] += z;
      _stream.putc(_prev_rgba[0]);
      _stream.putc(_prev_rgba[1]);
      _stream.putc(_prev_rgba[2]);
    }
  }

  return true;
}
