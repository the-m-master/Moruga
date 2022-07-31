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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file LICENSE.
 * If not, see <https://www.gnu.org/licenses/>
 *
 * https://github.com/the-m-master/Moruga
 */
#include "sgi.h"
#include <climits>
#include <cstdint>
#include <cstdlib>
#include "Buffer.h"
#include "File.h"
#include "filter.h"
#include "iEncoder.h"

// https://wiki.tcl-lang.org/page/SGI+IMAGE+HEADER

auto Header_t::ScanSGI(int32_t /*ch*/) noexcept -> Filter {
  // ------------------------------------------------------------------------
  // SGI image file format (RGB)
  // -------+------+---------------------------------------------------------
  // offset | size | description
  // -------+------+---------------------------------------------------------
  //      0 |    2 | IRIS image file magic number
  //      2 |    1 | Storage format, VERBATIM:0, RLE:1
  //      3 |    1 | Number of bytes per channel
  //      4 |    2 | Number of dimensions, 1,2 or 3
  //        |      | If this value is 1, the image file consists of only 1 channel and only 1 scanline.
  //        |      | The length of this scanline is given by the value of XSIZE below.
  //        |      | If this value is 2, the file consists of a single channel with a number of scanlines.
  //        |      | The width and height of the image are given by the values of XSIZE and YSIZE below.
  //        |      | If this value is 3, the file consists of a number of channels.
  //        |      | The width and height of the image are given by the values of
  //        |      | XSIZE and YSIZE below. The number of channels is given by the
  //        |      | value of ZSIZE below.
  //      6 |    2 | XSIZE - The width of the image in pixels
  //      8 |    2 | YSIZE - The height of the image in pixels
  //     10 |    2 | ZSIZE - The number of channels in the image
  //        |      | B/W (greyscale) images are stored as 2 dimensional images with a ZSIZE or 1.
  //        |      | RGB colour images are stored as 3 dimensional images with a ZSIZE of 3.
  //        |      | An RGB image with an ALPHA channel is stored as a 3 dimensional image with a ZSIZE of 4.
  //        |      | There are no inherent limitations in the SGI image file format that would preclude the creation of image files with more than 4 channels.
  //     12 |    4 | Minimum pixel value
  //     16 |    4 | Maximum pixel value
  //     20 |    4 | Ignored, these 4 bytes of data should be Set to 0
  //     24 |   80 | Image name
  //    104 |    4 | Colour map ID
  //    108 |  404 | Ignored
  //    512 |   ...

  static constexpr uint32_t offset{512};

  if ((0x01DA == m2(offset - 0)) && (1 == _buf(offset - 2)) && (1 == _buf(offset - 3)) && (3 == m2(offset - 4))) {
    const auto xsize{m2(offset - 6)};
    const auto ysize{m2(offset - 8)};
    const auto zsize{m2(offset - 10)};
    if ((xsize > 0) && (xsize < 0x4000) && (ysize > 0) && (ysize < 0x4000) && ((3 == zsize) || (4 == zsize))) {
      _di.image_width = xsize;
      _di.image_height = ysize;
      _di.bytes_per_pixel = zsize;
      _di.filter_end = INT_MAX;
      _di.offset_to_start = static_cast<int32_t>(_di.image_height * _di.bytes_per_pixel * sizeof(uint32_t) * 2);  // Skip row start/size

#if 0
      fprintf(stdout, "SGI %ux%ux%u   \n", xsize, ysize, _di.bytes_per_pixel);
      fflush(stdout);
#endif
      return Filter::SGI;
    }
  }

  return Filter::NOFILTER;
}

SGI_filter::SGI_filter(File_t& stream, iEncoder_t* const coder, DataInfo_t& di) noexcept
    : _stream{stream},  //
      _coder{coder},
      _di{di} {
  _length = _di.image_width * _di.image_height * _di.bytes_per_pixel;
  _base = static_cast<uint32_t*>(calloc(1, _length));
  _dst = reinterpret_cast<uint8_t*>(_base);
}

SGI_filter::~SGI_filter() noexcept {
  free(_base);
  _base = nullptr;
}

auto SGI_filter::Handle(int32_t ch) noexcept -> bool {  // encoding
  for (;;) {
    const auto length{static_cast<uint32_t>(reinterpret_cast<intptr_t>(_dst) - reinterpret_cast<intptr_t>(_base))};
    if (length >= _length) {
      break;
    }

    uint8_t pixel{(0 == length) ? static_cast<uint8_t>(ch) : static_cast<uint8_t>(_stream.getc())};
    int32_t count{pixel & 0x7F};
    if (count > 0) {
      if (pixel & 0x80) {
        while (count-- > 0) {  // Copy literals
          *_dst++ = static_cast<uint8_t>(_stream.getc());
        }
      } else {
        pixel = static_cast<uint8_t>(_stream.getc());
        while (count-- > 0) {  // Multiply pixel
          *_dst++ = pixel;
        }
      }
    }
  }

#if 0
  File_t txt("sgi.raw", "wb+");
  txt.Write(_base, static_cast<size_t>(reinterpret_cast<intptr_t>(_dst) - reinterpret_cast<intptr_t>(_base)));
#endif

  _dst = reinterpret_cast<uint8_t*>(_base);
  for (uint32_t n{0}; n < _length; ++n) {
    const uint8_t rgba{*_dst++};
    _coder->Compress(rgba - _prev_rgba);  // Delta encode all channels
    _prev_rgba = rgba;
  }

  _di.offset_to_start = 0;
  _di.filter_end = 0;
  return true;
}

auto SGI_filter::Handle(int32_t ch, int64_t& pos) noexcept -> bool {  // decoding
  if (_length > 0) {
    --_length;
    _prev_rgba += ch;  // Delta decode all channels
    *_dst++ = static_cast<uint8_t>(_prev_rgba);
    --pos;
  }
  if (0 == _length) {
    const uint8_t* src{reinterpret_cast<const uint8_t*>(_base)};

    uint32_t length{_di.image_height * _di.bytes_per_pixel};
    while (length-- > 0) {
      const uint8_t* const end{src + _di.image_width};
      while (src < end) {
        const uint8_t* sptr{src};
        src += 2;
        while ((src < end) && ((src[-2] != src[-1]) || (src[-1] != src[0]))) {
          ++src;
        }
        src -= 2;
        auto count{static_cast<int32_t>(src - sptr)};
        while (count > 0) {  // Copy literals
          int32_t todo{(count > 126) ? 126 : count};
          count -= todo;
          _stream.putc(0x80 | todo);
          while (todo-- > 0) {
            _stream.putc(*sptr++);
          }
        }
        sptr = src;
        const uint8_t cc{*src++};
        while ((src < end) && (*src == cc)) {
          ++src;
        }
        count = static_cast<int32_t>(src - sptr);
        while (count > 0) {  // Multiply pixel
          const int32_t todo{(count > 126) ? 126 : count};
          count -= todo;
          _stream.putc(todo);
          _stream.putc(cc);
        }
      }
      _stream.putc(0);
    }

    pos = _stream.Position();

    _di.offset_to_start = 0;
    _di.filter_end = 0;
  }

  return true;
}
