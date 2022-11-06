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
#include "filter.h"
#include "Progress.h"
#include "bmp.h"
#include "cab.h"
#include "elf.h"
#include "exe.h"
#include "gif.h"
#include "gzp.h"
#include "pbm.h"
#include "pdf.h"
#include "pkz.h"
#include "png.h"
#include "sgi.h"
#include "tga.h"
#include "tif.h"
#include "wav.h"

iFilter_t::~iFilter_t() noexcept = default;

Header_t::Header_t(const Buffer_t& __restrict buf, DataInfo_t& __restrict di, const bool encode) noexcept
    : _buf{buf},  //
      _di{di},
      _encode{encode} {}

Header_t::~Header_t() noexcept = default;

auto Header_t::Scan(int32_t ch) noexcept -> Filter {
  Filter type{ScanBMP(ch)};
  // clang-format off
  if (Filter::NOFILTER == type) { type = ScanCAB(ch); }
  if (Filter::NOFILTER == type) { type = ScanELF(ch); }
  if (Filter::NOFILTER == type) { type = ScanEXE(ch); }
  if (Filter::NOFILTER == type) { type = ScanGIF(ch); }
  if (Filter::NOFILTER == type) { type = ScanGZP(ch); }
  if (Filter::NOFILTER == type) { type = ScanPBM(ch); }
  if (Filter::NOFILTER == type) { type = ScanPDF(ch); }
  if (Filter::NOFILTER == type) { type = ScanPKZ(ch); }
  if (Filter::NOFILTER == type) { type = ScanPNG(ch); }
  if (Filter::NOFILTER == type) { type = ScanSGI(ch); }
  if (Filter::NOFILTER == type) { type = ScanTGA(ch); }
  if (Filter::NOFILTER == type) { type = ScanTIF(ch); }
  if (Filter::NOFILTER == type) { type = ScanWAV(ch); }
  // clang-format on
  return type;
}

Filter_t::Filter_t(const Buffer_t& __restrict buf, const int64_t original_length, File_t& stream, iEncoder_t* const encoder) noexcept
    : _buf{buf},  //
      _original_length{original_length},
      _stream{stream},
      _encoder{encoder},
      _header{new Header_t{buf, _di, nullptr != encoder}} {}

Filter_t::~Filter_t() noexcept {
  if (nullptr != _filter) {
    delete _filter;
    _filter = nullptr;
  }
  delete _header;
  _header = nullptr;
}

auto Filter_t::Create(const Filter& type) noexcept -> iFilter_t* {
  switch (type) {
    // clang-format off
    case Filter::BMP: return new BMP_filter(_stream, _encoder, _di);
    case Filter::CAB: return new CAB_filter(_stream, _encoder, _di);
    case Filter::ELF: return new ELF_filter(_stream, _encoder, _di);
    case Filter::EXE: return new EXE_filter(_stream, _encoder, _di);
    case Filter::GIF: return new GIF_filter(_stream, _encoder, _di, _buf, _original_length);
    case Filter::GZP: return new GZP_filter(_stream, _encoder, _di, _original_length);
    case Filter::PBM: return new PBM_filter(_stream, _encoder, _di);
    case Filter::PDF: return new PDF_filter(_stream, _encoder, _di, _buf);
    case Filter::PKZ: return new PKZ_filter(_stream, _encoder, _di, _buf);
    case Filter::PNG: return new PNG_filter(_stream, _encoder, _di, _buf);
    case Filter::SGI: return new SGI_filter(_stream, _encoder, _di);
    case Filter::TGA: return new TGA_filter(_stream, _encoder, _di);
    case Filter::TIF: return new TIF_filter(_stream, _encoder, _di);
    case Filter::WAV: return new WAV_filter(_stream, _encoder, _di);
      // clang-format on

    case Filter::NOFILTER:
    default:
      return nullptr;
  }
}

auto Filter_t::Scan(int32_t ch) noexcept -> bool {  // encoding
  if (nullptr == _filter) {
    const Filter type = _header->Scan(ch);
    if (Filter::NOFILTER != type) {
      Progress_t::FoundType(type);
      _filter = Create(type);
    }
  }
  if (nullptr != _filter) {
    if (0 == _di.offset_to_start) {
      if (0 == _di.filter_end) {
        delete _filter;
        _filter = nullptr;
        return false;
      }
      --_di.filter_end;
      return _filter->Handle(ch);
    }
    --_di.offset_to_start;
  }
  return false;
}

auto Filter_t::Scan(int32_t ch, int64_t& pos) noexcept -> bool {  // decoding
  if (nullptr == _filter) {
    const Filter type = _header->Scan(ch);
    if (Filter::NOFILTER != type) {
      Progress_t::FoundType(type);
      _filter = Create(type);
    }
  } else {
    if (0 == _di.offset_to_start) {
      if (0 == _di.filter_end) {
        delete _filter;
        _filter = nullptr;
        return false;
      }
      --_di.filter_end;
      return _filter->Handle(ch, pos);
    }
    --_di.offset_to_start;
  }
  return false;
}
