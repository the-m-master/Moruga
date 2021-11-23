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
#include "bmp.h"
#include <cassert>
#include <cstdint>
#include "File.h"
#include "filter.h"
#include "iEncoder.h"

auto Header_t::ScanBMP(int32_t /*ch*/) noexcept -> Filter {
  // ------------------------------------------------------------------------
  // BMP header (BITMAPINFOHEADER)
  // -------+------+---------------------------------------------------------
  // offset | size | description
  // -------+------+---------------------------------------------------------
  //      0 |   2  | Signature, must be BA,BM,CI,CP,IC or PT
  //      2 |   4  | Size of BMP file in bytes (unreliable)
  //      6 |   2  | Reserved, must be zero
  //      8 |   2  | Reserved, must be zero
  //     10 |   4  | Offset to start of image data in bytes
  //     14 |   4  | Size of BITMAPINFOHEADER structure (must be 40)
  //     18 |   4  | Image width in pixels
  //     22 |   4  | Image height in pixels
  //     26 |   2  | Number of planes in the image, must be 1
  //     28 |   2  | Number of bits per pixel (1, 4, 8, 24 or 32)
  //     30 |   4  | Compression type (0=RGB, 1=RLE8, 2=RLE4)
  //     34 |   4  | Size of image data in bytes (including padding)
  //     38 |   4  | Horizontal resolution in pixels per meter (unreliable)
  //     42 |   4  | Vertical resolution in pixels per meter (unreliable)
  //     46 |   4  | Number of colours in the palette, or zero
  //     50 |   4  | Number of important colours, or zero
  //     54 |   ...

  // ------------------------------------------------------------------------
  // BMP header (BITMAPV4HEADER)
  // -------+------+---------------------------------------------------------
  // offset | size | description
  // -------+------+---------------------------------------------------------
  //      0 |   2  | Signature, must be BA,BM,CI,CP,IC or PT
  //      2 |   4  | Size of BMP file in bytes (unreliable)
  //      6 |   2  | Reserved, must be zero
  //      8 |   2  | Reserved, must be zero
  //     10 |   4  | Offset to start of image data in bytes
  //     14 |   4  | Size of BITMAPV4HEADER structure
  //     18 |   4  | Image width in pixels
  //     22 |   4  | Image height in pixels
  //     26 |   2  | Number of planes in the image, must be 1
  //     28 |   2  | Number of bits per pixel (1, 4, 8, 24 or 32)
  //     30 |   4  | Compression type (0=RGB, 1=RLE8, 2=RLE4, 3=BITFIELDS, 4=JPEG, 5=PNG)
  //     34 |   4  | Size of image data in bytes (including padding)
  //     38 |   4  | Horizontal resolution in pixels per meter (unreliable)
  //     42 |   4  | Vertical resolution in pixels per meter (unreliable)
  //     46 |   4  | Number of colours in the palette, or zero
  //     50 |   4  | Number of important colours, or zero
  //     54 |   4  | Red channel bit mask (when BITFIELDS is specified)
  //     58 |   4  | Green channel bit mask (when BITFIELDS is specified)
  //     62 |   4  | Blue channel bit mask (when BITFIELDS is specified)
  //     66 |   4  | Alpha channel bit mask (when BITFIELDS is specified)
  //     70 |   4  | Type of colour Space
  //     74 |  36  | CIEXYZTRIPLE colour space endpoints
  //    110 |   4  | Red Gamma
  //    114 |   4  | Green Gamma
  //    118 |   4  | Blue Gamma
  //    122 |   ...

  static constexpr uint32_t offset{54};

  const auto sos{i4(offset - 14)};
  if ((0x28 == sos) || (0x6C == sos) || (0x7C == sos)) {
    const auto planes{i2(offset - 26)};
    if (1 == planes) {
      const auto bits_per_pixel{i2(offset - 28)};
      if ((8 == bits_per_pixel) || (24 == bits_per_pixel) || (32 == bits_per_pixel)) {
        const auto cmp{i4(offset - 30)};
        if ((0 == cmp) || (3 == cmp)) {  // RGB or BITFIELDS
          const auto sig{m2(offset - 0)};
          if ((('BA' == sig) || ('BM' == sig) || ('CI' == sig) || ('CP' == sig) || ('IC' == sig) || ('PT' == sig))) {
            const auto width{i4(offset - 18)};
            const auto height{i4(offset - 22)};
            if ((width > 0) && (width < 0x8000) && (height > 0) && (height < 0x8000)) {
              _di.bytes_per_pixel = bits_per_pixel / 8;  // 3 or 4 bytes
              _di.padding_bytes = (3 == _di.bytes_per_pixel) ? (width % 4) : 0;
              _di.image_width = width;
              _di.filter_end = int32_t((width * height * _di.bytes_per_pixel) + (_di.padding_bytes * height));
              _di.offset_to_start = int32_t(i4(offset - 10) - offset);
#if 0
              fprintf(stderr, "BMP %ux%ux%u   \n", width, height, _di.bytes_per_pixel);
              fflush(stderr);
#endif
              return Filter::BMP;
            }
          }
        }
      }
    }
  }

  return Filter::NOFILTER;
}

BMP_filter::BMP_filter(File_t& stream, iEncoder_t& coder, const DataInfo_t& di)
    : _stream{stream},  //
      _coder{coder},
      _di{di} {}

BMP_filter::~BMP_filter() noexcept {
  assert(_di.padding_bytes == _length);
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

auto BMP_filter::Handle(int32_t ch) noexcept -> bool {  // encoding
  _rgba[_length++] = int8_t(ch);

  if (_length >= _di.bytes_per_pixel) {
    _length = 0;

    if (_di.image_width > 0) {
      ++_width;
      if (_width > _di.image_width) {
        _width = 0;
        if (_di.padding_bytes > 0) {
          for (uint32_t n{_di.padding_bytes}; n--;) {
            _coder.Compress(_rgba[0]);
            _rgba[0] = _rgba[1];
            _rgba[1] = _rgba[2];
          }
          _length = _di.bytes_per_pixel - _di.padding_bytes;
          return true;
        }
      }
    }

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
      if (4 == _di.bytes_per_pixel) {
        _coder.Compress(_rgba[3] - _prev_rgba[3]);  // Delta encode alpha channel
        _prev_rgba[3] = _rgba[3];
      }
    }
  }

  return true;
}

auto BMP_filter::Handle(int32_t ch, int64_t& /*pos*/) noexcept -> bool {  // decoding
  _rgba[_length++] = int8_t(ch);

  if (_length >= _di.bytes_per_pixel) {
    _length = 0;

    if (_di.image_width > 0) {
      ++_width;
      if (_width > _di.image_width) {
        _width = 0;
        if (_di.padding_bytes > 0) {
          for (uint32_t n{_di.padding_bytes}; n--;) {
            _stream.putc(_rgba[0]);
            _rgba[0] = _rgba[1];
            _rgba[1] = _rgba[2];
          }
          _length = _di.bytes_per_pixel - _di.padding_bytes;
          return true;
        }
      }
    }

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
