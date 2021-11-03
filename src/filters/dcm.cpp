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
#include "dcm.h"
#include <cassert>
#include <cstdint>
#include "File.h"
#include "filter.h"
#include "iEncoder.h"

auto Header_t::ScanDCM(int32_t /*ch*/) noexcept -> Filter {
  // ------------------------------------------------------------------------
  // DICOM header
  // -------+------+---------------------------------------------------------
  // offset | size | description
  // -------+------+---------------------------------------------------------
  //      0 |  128 | A fixed 128 byte field available for application profile
  //        |      | or implementation specified use.
  //        |      | If not used by an application profile or a specific
  //        |      | implementation all bytes shall be set to zero.
  //    128 |   4  | Four bytes containing the character string "DICM".
  //        |      | This Prefix is intended to be used to recognise that this
  //        |      | File is or is not a DICOM File.
  //    132 |   ...

  static constexpr uint32_t offset{136};

  if ('DICM' == m4(offset - 128)) {
    if (0x20000000 == m4(offset - 132)) {
      return Filter::DCM;
    }
  }

  return Filter::NOFILTER;
}

DCM_filter::DCM_filter(File_t& stream, iEncoder_t& coder, const DataInfo_t& di)
    : _stream{stream},  //
      _coder{coder},
      _di{di} {}

DCM_filter::~DCM_filter() noexcept = default;

auto DCM_filter::Handle(int32_t /*ch*/) noexcept -> bool {  // encoding
  return false;
}

auto DCM_filter::Handle(int32_t /*ch*/, int64_t& /*pos*/) noexcept -> bool {  // decoding
  return false;
}
