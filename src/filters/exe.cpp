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
#include "exe.h"
#include <cassert>
#include <cstdint>
#include "Buffer.h"
#include "File.h"
#include "filter.h"
#include "iEncoder.h"

#if defined(__linux__)
#define IMAGE_SIZEOF_SHORT_NAME 8
#define IMAGE_NUMBEROF_DIRECTORY_ENTRIES 16
#endif

struct IMAGE_FILE_HEADER_t {
  uint16_t Machine;
  uint16_t NumberOfSections;
  uint32_t TimeDateStamp;
  uint32_t PointerToSymbolTable;
  uint32_t NumberOfSymbols;
  uint16_t SizeOfOptionalHeader;
  uint16_t Characteristics;
};
static_assert(20 == sizeof(IMAGE_FILE_HEADER_t), "Alignment issue in IMAGE_FILE_HEADER_t");

struct IMAGE_DATA_DIRECTORY_t {
  uint32_t VirtualAddress;
  uint32_t Size;
};
static_assert(8 == sizeof(IMAGE_DATA_DIRECTORY_t), "Alignment issue in IMAGE_DATA_DIRECTORY_t");

struct IMAGE_OPTIONAL_HEADER32_t {
  uint16_t Magic;
  uint8_t MajorLinkerVersion;
  uint8_t MinorLinkerVersion;
  uint32_t SizeOfCode;
  uint32_t SizeOfInitializedData;
  uint32_t SizeOfUninitializedData;
  uint32_t AddressOfEntryPoint;
  uint32_t BaseOfCode;
  uint32_t BaseOfData;
  uint32_t ImageBase;
  uint32_t SectionAlignment;
  uint32_t FileAlignment;
  uint16_t MajorOperatingSystemVersion;
  uint16_t MinorOperatingSystemVersion;
  uint16_t MajorImageVersion;
  uint16_t MinorImageVersion;
  uint16_t MajorSubsystemVersion;
  uint16_t MinorSubsystemVersion;
  uint32_t Win32VersionValue;
  uint32_t SizeOfImage;
  uint32_t SizeOfHeaders;
  uint32_t CheckSum;
  uint16_t Subsystem;
  uint16_t DllCharacteristics;
  uint32_t SizeOfStackReserve;
  uint32_t SizeOfStackCommit;
  uint32_t SizeOfHeapReserve;
  uint32_t SizeOfHeapCommit;
  uint32_t LoaderFlags;
  uint32_t NumberOfRvaAndSizes;
  std::array<IMAGE_DATA_DIRECTORY_t, IMAGE_NUMBEROF_DIRECTORY_ENTRIES> DataDirectory;
};
static_assert(224 == sizeof(IMAGE_OPTIONAL_HEADER32_t), "Alignment issue in IMAGE_OPTIONAL_HEADER32_t");

struct IMAGE_NT_HEADERS32_t {
  uint32_t Signature;
  IMAGE_FILE_HEADER_t FileHeader;
  IMAGE_OPTIONAL_HEADER32_t OptionalHeader;
};
static_assert(248 == sizeof(IMAGE_NT_HEADERS32_t), "Alignment issue in IMAGE_NT_HEADERS32_t");

struct IMAGE_OPTIONAL_HEADER64_t {
  uint16_t Magic;
  uint8_t MajorLinkerVersion;
  uint8_t MinorLinkerVersion;
  uint32_t SizeOfCode;
  uint32_t SizeOfInitializedData;
  uint32_t SizeOfUninitializedData;
  uint32_t AddressOfEntryPoint;
  uint32_t BaseOfCode;
  uint64_t ImageBase;
  uint32_t SectionAlignment;
  uint32_t FileAlignment;
  uint16_t MajorOperatingSystemVersion;
  uint16_t MinorOperatingSystemVersion;
  uint16_t MajorImageVersion;
  uint16_t MinorImageVersion;
  uint16_t MajorSubsystemVersion;
  uint16_t MinorSubsystemVersion;
  uint32_t Win32VersionValue;
  uint32_t SizeOfImage;
  uint32_t SizeOfHeaders;
  uint32_t CheckSum;
  uint16_t Subsystem;
  uint16_t DllCharacteristics;
  uint64_t SizeOfStackReserve;
  uint64_t SizeOfStackCommit;
  uint64_t SizeOfHeapReserve;
  uint64_t SizeOfHeapCommit;
  uint32_t LoaderFlags;
  uint32_t NumberOfRvaAndSizes;
  std::array<IMAGE_DATA_DIRECTORY_t, IMAGE_NUMBEROF_DIRECTORY_ENTRIES> DataDirectory;
};
static_assert(240 == sizeof(IMAGE_OPTIONAL_HEADER64_t), "Alignment issue in IMAGE_OPTIONAL_HEADER64_t");

struct IMAGE_NT_HEADERS64_t {
  uint32_t Signature;
  IMAGE_FILE_HEADER_t FileHeader;
  IMAGE_OPTIONAL_HEADER64_t OptionalHeader;
};
static_assert(264 == sizeof(IMAGE_NT_HEADERS64_t), "Alignment issue in IMAGE_NT_HEADERS64_t");

struct IMAGE_SECTION_HEADER_t {
  std::array<uint8_t, IMAGE_SIZEOF_SHORT_NAME> Name;
  union {
    uint32_t PhysicalAddress;
    uint32_t VirtualSize;
  } Misc;
  uint32_t VirtualAddress;
  uint32_t SizeOfRawData;
  uint32_t PointerToRawData;
  uint32_t PointerToRelocations;
  uint32_t PointerToLinenumbers;
  uint16_t NumberOfRelocations;
  uint16_t NumberOfLinenumbers;
  uint32_t Characteristics;
};
static_assert(40 == sizeof(IMAGE_SECTION_HEADER_t), "Alignment issue in IMAGE_SECTION_HEADER_t");

// clang-format off
#if defined(_WIN32) || defined(_WIN64)
static_assert(sizeof(IMAGE_FILE_HEADER_t)       == sizeof(IMAGE_FILE_HEADER),       "Alignment issue in IMAGE_FILE_HEADER_t"      );
static_assert(sizeof(IMAGE_DATA_DIRECTORY_t)    == sizeof(IMAGE_DATA_DIRECTORY),    "Alignment issue in IMAGE_DATA_DIRECTORY_t"   );
static_assert(sizeof(IMAGE_OPTIONAL_HEADER32_t) == sizeof(IMAGE_OPTIONAL_HEADER32), "Alignment issue in IMAGE_OPTIONAL_HEADER32_t");
static_assert(sizeof(IMAGE_NT_HEADERS32_t)      == sizeof(IMAGE_NT_HEADERS32),      "Alignment issue in IMAGE_NT_HEADERS32_t"     );
static_assert(sizeof(IMAGE_OPTIONAL_HEADER64_t) == sizeof(IMAGE_OPTIONAL_HEADER64), "Alignment issue in IMAGE_OPTIONAL_HEADER64_t");
static_assert(sizeof(IMAGE_NT_HEADERS64_t)      == sizeof(IMAGE_NT_HEADERS64),      "Alignment issue in IMAGE_NT_HEADERS64_t"     );
static_assert(sizeof(IMAGE_SECTION_HEADER_t)    == sizeof(IMAGE_SECTION_HEADER),    "Alignment issue in IMAGE_SECTION_HEADER_t"   );
#endif
// clang-format on

auto Header_t::ScanEXE(int32_t /*ch*/) noexcept -> Filter {
  //            BRIEF VIEW OF PE FILE
  //          .----------------------.
  //          |                      |
  //          |        Header        |
  //          |                      |
  //          |----------------------|
  //          |                      |
  //          |   Various Sections   |
  //          |        .....         |
  //          |        .....         |
  //  .------>|       .RELOC         |
  //  | .---->|       .IDATA         |
  //  | | .-->|       .DATA          |
  //  | | | .>|       .TEXT          |
  //  | | | | |----------------------|
  //  '-|-|-|-|                      | <--- Each entry in section table have pointer
  //    '-|-|-|        Section       |      offsets to actual sections
  //      '-|-|    Header or Table   |
  //        '-|                      |      ---.----------------.
  //          |----------------------|-----/   |   PE Optional  |  1) ImageBase
  //          |                      |         |    Header      |
  //          |                      |         |                |
  //          |      NT Headers      |         |----------------|
  //          |                      |         |     COFF/PE    |  1) NumberOfSections
  //          |                      |         |   Header Info  |  2) SizeOfOptionalHeader
  //          |----------------------|-----    |----------------|
  //          |        UNUSED        |     \   |   PE Signature |
  //          |----------------------|      ---'----------------'
  //          |      MS-DOS stub     |
  //          |----------------------|
  //          |        UNUSED        |
  //          |----------------------|
  //          |     MS-DOS Header    | <-- Here at 0x3C location we have the offset of NT Header
  //          '----------------------'

  static constexpr uint32_t offset{0x400};
  static constexpr uint16_t PROCESSOR_AMD{0x8664};

  if ('MZ\x90\x00' == m4(offset - 0)) {
    const auto lfanew{i2(offset - 60)};
    if ((lfanew < offset) && ('PE\x00\x00' == m4(offset - lfanew))) {
      const auto machine{i2(offset - lfanew - 4)};

      uint32_t number_of_sections{0};
      int32_t sizeof_headers{0};

      uint32_t i{_buf.Pos() - (offset - lfanew)};

      if (PROCESSOR_AMD == machine) {
        const IMAGE_NT_HEADERS64_t* _ntHeader = reinterpret_cast<IMAGE_NT_HEADERS64_t*>(&_buf[i]);
        i += sizeof(IMAGE_NT_HEADERS64_t);

        number_of_sections = _ntHeader->FileHeader.NumberOfSections;
        sizeof_headers = int32_t(_ntHeader->OptionalHeader.SizeOfHeaders);
      } else {
        const IMAGE_NT_HEADERS32_t* _ntHeader = reinterpret_cast<IMAGE_NT_HEADERS32_t*>(&_buf[i]);
        i += sizeof(IMAGE_NT_HEADERS32_t);

        number_of_sections = _ntHeader->FileHeader.NumberOfSections;
        sizeof_headers = int32_t(_ntHeader->OptionalHeader.SizeOfHeaders);
      }

      if (number_of_sections < 32) {
        int32_t size{sizeof_headers};
        for (uint32_t n{0}; n < number_of_sections; ++n) {
          size += int32_t(reinterpret_cast<IMAGE_SECTION_HEADER_t*>(&_buf[i])->SizeOfRawData);
          i += sizeof(IMAGE_SECTION_HEADER_t);
        }

        const auto offset_to_start{sizeof_headers - int32_t(offset)};
        const auto filter_end{size - int32_t(offset)};

        if ((size > 0) && (offset_to_start >= 0) && (filter_end > 0)) {
          _di.location = offset;
          _di.offset_to_start = offset_to_start;
          _di.filter_end = filter_end;
#if 0
          fprintf(stderr, "EXE %u at %u\n", size, _buf.Pos());
          fflush(stderr);
#endif
          return Filter::EXE;
        }
      }
    }
  }

  return Filter::NOFILTER;
}

EXE_filter::EXE_filter(File_t& stream, iEncoder_t* const coder, const DataInfo_t& di)
    : _stream{stream},  //
      _coder{coder},
      _location{di.location} {}

EXE_filter::~EXE_filter() noexcept = default;

void EXE_filter::detect(const int32_t ch) noexcept {
  if (!_transform) {
    if ((0xE8 == ch) || (0xE9 == ch) || ((0x0F == _oldc) && (0x80 == (0xF0 & ch)))) {
      _transform = true;
    }
    _oldc = ch;
  }
}

auto EXE_filter::Handle(int32_t ch) noexcept -> bool {  // encoding
  bool status{false};

  detect(ch);

  if (_transform) {
    _addr[_length++] = uint8_t(ch);
    if (_length >= 5) {
      _length = 0;
      _transform = false;
      _oldc = 0;

      //    E8 aa aa aa 00/FF --> CALL near, relative
      //    E9 aa aa aa 00/FF --> JMP near, relative
      // 0F 8x aa aa aa 00/FF --> JA,JNA,JAE,JNAE,JB,JNB,JBE,JNBE,JC,JNC,JE,JNE,JG,JNG,JGE,JNGE,JL,JNL,JLE,JNLE,JO,JNO,JP,JNP,JPE,JPOJS,JNS,JZ,JNZ...
      //
      // Transform: add location to 'a'
      if ((0x00 == _addr[4]) || (0xFF == _addr[4])) {
        assert((0xE8 == _addr[0]) || (0xE9 == _addr[0]) || (0x80 == (0xF0 & _addr[0])));

        const int32_t addr{(_addr[1] | (_addr[2] << 8) | (_addr[3] << 16) | (_addr[4] << 24)) + _location};

        _addr[1] = uint8_t(addr);
        _addr[2] = uint8_t(addr >> 16);
        _addr[3] = uint8_t(addr >> 8);
        _addr[4] = uint8_t(0 - (1 & (addr >> 24)));
      }

      _coder->Compress(_addr[0]);
      _coder->Compress(_addr[1]);
      _coder->Compress(_addr[2]);
      _coder->Compress(_addr[3]);
      _coder->Compress(_addr[4]);
    }
    status = true;
  }

  ++_location;
  return status;
}

auto EXE_filter::Handle(int32_t ch, int64_t& /*pos*/) noexcept -> bool {  // decoding
  bool status{false};

  detect(ch);

  if (_transform) {
    _addr[_length++] = uint8_t(ch);
    if (_length >= 5) {
      _length = 0;
      _transform = false;
      _oldc = 0;

      //    E8 aa aa aa 00/FF --> CALL near, relative
      //    E9 aa aa aa 00/FF --> JMP near, relative
      // 0F 8x aa aa aa 00/FF --> JA,JNA,JAE,JNAE,JB,JNB,JBE,JNBE,JC,JNC,JE,JNE,JG,JNG,JGE,JNGE,JL,JNL,JLE,JNLE,JO,JNO,JP,JNP,JPE,JPOJS,JNS,JZ,JNZ...
      //
      // Transform: subtract location from 'a'
      if ((0x00 == _addr[4]) || (0xFF == _addr[4])) {
        assert((0xE8 == _addr[0]) || (0xE9 == _addr[0]) || (0x80 == (0xF0 & _addr[0])));

        const int32_t addr{(_addr[1] | (_addr[2] << 16) | (_addr[3] << 8) | (_addr[4] << 24)) - _location};

        _addr[1] = uint8_t(addr);
        _addr[2] = uint8_t(addr >> 8);
        _addr[3] = uint8_t(addr >> 16);
        _addr[4] = uint8_t(0 - (1 & (addr >> 24)));
      }

      _stream.putc(_addr[0]);
      _stream.putc(_addr[1]);
      _stream.putc(_addr[2]);
      _stream.putc(_addr[3]);
      _stream.putc(_addr[4]);
    }
    status = true;
  }

  ++_location;
  return status;
}
