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
#include "tga.h"
#include <cstdint>
#include "Buffer.h"
#include "File.h"
#include "filter.h"
#include "iEncoder.h"

auto Header_t::ScanTGA(int32_t /*ch*/) noexcept -> Filter {
  // ------------------------------------------------------------------------
  // TGA header
  // -------+------+---------------------------------------------------------
  // offset | size | description
  // -------+------+---------------------------------------------------------
  //      0 |   1  | Size of ID field that follows 18 byte header (0 usually)
  //      1 |   1  | Type of colour map 0=none, 1=has palette
  //      2 |   1  | Type of image 0=none, 1=indexed, 2=RGB, 3=Grey, +8=RLE packed
  //      3 |   2  | Colour map origin
  //      5 |   2  | Colour map length
  //      7 |   1  | Number of bits per palette entry 15,16,24,32
  //      8 |   2  | Image x origin
  //     10 |   2  | Image y origin
  //     12 |   2  | Image width in pixels
  //     14 |   2  | Image height in pixels
  //     16 |   1  | Image bits per pixel 8,16,24,32
  //     17 |   1  | Image descriptor byte
  //     18 |   ...

  static constexpr uint32_t offset{18};

  if (((0 == _buf(offset - 0)) || (0x1A == _buf(offset - 0))) &&  //
      (0 == _buf(offset - 1)) &&                                  //
      (2 == _buf(offset - 2)) &&                                  //
      (0 == m4(offset - 3)) &&                                    //
      (0 == m4(offset - 8))) {
    const auto bits_per_palette{_buf(offset - 7)};
    if ((0x00 == bits_per_palette) || (0x18 == bits_per_palette)) {
      const auto bits_per_pixel{_buf(offset - 16)};
      if ((8 == bits_per_pixel) || (24 == bits_per_pixel) || (32 == bits_per_pixel)) {
        const auto width{i2(offset - 12)};
        const auto height{i2(offset - 14)};
        if ((width > 0) && (width < 0x4000) && (height > 0) && (height < 0x4000)) {
          _di.bytes_per_pixel = uint32_t(bits_per_pixel) / 8;
          _di.filter_end = int32_t(_di.bytes_per_pixel * width * height);
          _di.image_width = 0;
          _di.offset_to_start = 0;
#if 0
          fprintf(stderr, "TGA %ux%ux%u   \n", width, height, _di.bytes_per_pixel);
          fflush(stderr);
#endif
          return Filter::TGA;
        }
      }
    }
  }

  return Filter::NOFILTER;
}

TGA_filter::TGA_filter(File_t& stream, iEncoder_t* const coder, const DataInfo_t& di)
    : _stream{stream},  //
      _coder{coder},
      _di{di} {}

TGA_filter::~TGA_filter() noexcept {
  if (nullptr != _coder) {  // encoding
    for (uint32_t n{0}; n < _length; ++n) {
      _coder->Compress(_rgba[n]);
    }
  } else {  // decoding
    for (uint32_t n{0}; n < _length; ++n) {
      _stream.putc(_rgba[n]);
    }
  }
}

auto TGA_filter::Handle(int32_t ch) noexcept -> bool {  // encoding
  _rgba[_length++] = int8_t(ch);

  if (_length >= _di.bytes_per_pixel) {
    _length = 0;

    if (1 == _di.bytes_per_pixel) {
      const auto pixel{_rgba[0]};
      _coder->Compress(pixel - _prev_rgba[0]);
      _prev_rgba[0] = pixel;
    } else {
      const auto b{_rgba[0]};
      const auto g{_rgba[1]};
      const auto r{_rgba[2]};
      const auto x{g};
      const auto y{int8_t(g - r)};
      const auto z{int8_t(g - b)};
      _coder->Compress(x - _prev_rgba[0]);
      _coder->Compress(y - _prev_rgba[1]);
      _coder->Compress(z - _prev_rgba[2]);
      _prev_rgba[0] = x;
      _prev_rgba[1] = y;
      _prev_rgba[2] = z;
      if (4 == _di.bytes_per_pixel) {
        _coder->Compress(_rgba[3] - _prev_rgba[3]);  // Delta encode alpha channel
        _prev_rgba[3] = _rgba[3];
      }
    }
  }

  return true;
}

auto TGA_filter::Handle(int32_t ch, int64_t& /*pos*/) noexcept -> bool {  // decoding
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
      if (4 == _di.bytes_per_pixel) {
        _prev_rgba[3] += _rgba[3];  // Delta decode alpha channel
        _stream.putc(_prev_rgba[3]);
      }
    }
  }

  return true;
}
