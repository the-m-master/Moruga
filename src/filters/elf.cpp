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
#include "elf.h"
#include <cassert>
#include <climits>
#include <cstdint>
#include <utility>
#include "Buffer.h"
#include "File.h"
#include "filter.h"
#include "iEncoder.h"

enum {
  ELFCLASSNONE = 0,
  ELFCLASS32 = 1,  // 32-bit object file
  ELFCLASS64 = 2   // 64-bit object file
};

enum {
  ET_NONE = 0,         // No file type
  ET_REL = 1,          // Relocatable file
  ET_EXEC = 2,         // Executable file
  ET_DYN = 3,          // Shared object file
  ET_CORE = 4,         // Core file
  ET_LOOS = 0xFE00,    // Beginning of operating system-specific codes
  ET_HIOS = 0xFEFF,    // Operating system-specific
  ET_LOPROC = 0xFF00,  // Beginning of processor-specific codes
  ET_HIPROC = 0xFFFF   // Processor-specific
};

enum {
  EM_NONE = 0,             // No machine
  EM_M32 = 1,              // AT&T WE 32100
  EM_SPARC = 2,            // SPARC
  EM_386 = 3,              // Intel 386
  EM_68K = 4,              // Motorola 68000
  EM_88K = 5,              // Motorola 88000
  EM_IAMCU = 6,            // Intel MCU
  EM_860 = 7,              // Intel 80860
  EM_MIPS = 8,             // MIPS R3000
  EM_S370 = 9,             // IBM System/370
  EM_MIPS_RS3_LE = 10,     // MIPS RS3000 Little-endian
  EM_PARISC = 15,          // Hewlett-Packard PA-RISC
  EM_VPP500 = 17,          // Fujitsu VPP500
  EM_SPARC32PLUS = 18,     // Enhanced instruction set SPARC
  EM_960 = 19,             // Intel 80960
  EM_PPC = 20,             // PowerPC
  EM_PPC64 = 21,           // PowerPC64
  EM_S390 = 22,            // IBM System/390
  EM_SPU = 23,             // IBM SPU/SPC
  EM_V800 = 36,            // NEC V800
  EM_FR20 = 37,            // Fujitsu FR20
  EM_RH32 = 38,            // TRW RH-32
  EM_RCE = 39,             // Motorola RCE
  EM_ARM = 40,             // ARM
  EM_ALPHA = 41,           // DEC Alpha
  EM_SH = 42,              // Hitachi SH
  EM_SPARCV9 = 43,         // SPARC V9
  EM_TRICORE = 44,         // Siemens TriCore
  EM_ARC = 45,             // Argonaut RISC Core
  EM_H8_300 = 46,          // Hitachi H8/300
  EM_H8_300H = 47,         // Hitachi H8/300H
  EM_H8S = 48,             // Hitachi H8S
  EM_H8_500 = 49,          // Hitachi H8/500
  EM_IA_64 = 50,           // Intel IA-64 processor architecture
  EM_MIPS_X = 51,          // Stanford MIPS-X
  EM_COLDFIRE = 52,        // Motorola ColdFire
  EM_68HC12 = 53,          // Motorola M68HC12
  EM_MMA = 54,             // Fujitsu MMA Multimedia Accelerator
  EM_PCP = 55,             // Siemens PCP
  EM_NCPU = 56,            // Sony nCPU embedded RISC processor
  EM_NDR1 = 57,            // Denso NDR1 microprocessor
  EM_STARCORE = 58,        // Motorola Star*Core processor
  EM_ME16 = 59,            // Toyota ME16 processor
  EM_ST100 = 60,           // STMicroelectronics ST100 processor
  EM_TINYJ = 61,           // Advanced Logic Corp. TinyJ embedded processor family
  EM_X86_64 = 62,          // AMD x86-64 architecture
  EM_PDSP = 63,            // Sony DSP Processor
  EM_PDP10 = 64,           // Digital Equipment Corp. PDP-10
  EM_PDP11 = 65,           // Digital Equipment Corp. PDP-11
  EM_FX66 = 66,            // Siemens FX66 microcontroller
  EM_ST9PLUS = 67,         // STMicroelectronics ST9+ 8/16 bit microcontroller
  EM_ST7 = 68,             // STMicroelectronics ST7 8-bit microcontroller
  EM_68HC16 = 69,          // Motorola MC68HC16 Microcontroller
  EM_68HC11 = 70,          // Motorola MC68HC11 Microcontroller
  EM_68HC08 = 71,          // Motorola MC68HC08 Microcontroller
  EM_68HC05 = 72,          // Motorola MC68HC05 Microcontroller
  EM_SVX = 73,             // Silicon Graphics SVx
  EM_ST19 = 74,            // STMicroelectronics ST19 8-bit microcontroller
  EM_VAX = 75,             // Digital VAX
  EM_CRIS = 76,            // Axis Communications 32-bit embedded processor
  EM_JAVELIN = 77,         // Infineon Technologies 32-bit embedded processor
  EM_FIREPATH = 78,        // Element 14 64-bit DSP Processor
  EM_ZSP = 79,             // LSI Logic 16-bit DSP Processor
  EM_MMIX = 80,            // Donald Knuth's educational 64-bit processor
  EM_HUANY = 81,           // Harvard University machine-independent object files
  EM_PRISM = 82,           // SiTera Prism
  EM_AVR = 83,             // Atmel AVR 8-bit microcontroller
  EM_FR30 = 84,            // Fujitsu FR30
  EM_D10V = 85,            // Mitsubishi D10V
  EM_D30V = 86,            // Mitsubishi D30V
  EM_V850 = 87,            // NEC v850
  EM_M32R = 88,            // Mitsubishi M32R
  EM_MN10300 = 89,         // Matsushita MN10300
  EM_MN10200 = 90,         // Matsushita MN10200
  EM_PJ = 91,              // picoJava
  EM_OPENRISC = 92,        // OpenRISC 32-bit embedded processor
  EM_ARC_COMPACT = 93,     // ARC International ARCompact processor (old spelling/synonym: EM_ARC_A5)
  EM_XTENSA = 94,          // Tensilica Xtensa Architecture
  EM_VIDEOCORE = 95,       // Alphamosaic VideoCore processor
  EM_TMM_GPP = 96,         // Thompson Multimedia General Purpose Processor
  EM_NS32K = 97,           // National Semiconductor 32000 series
  EM_TPC = 98,             // Tenor Network TPC processor
  EM_SNP1K = 99,           // Trebia SNP 1000 processor
  EM_ST200 = 100,          // STMicroelectronics (www.st.com) ST200
  EM_IP2K = 101,           // Ubicom IP2xxx microcontroller family
  EM_MAX = 102,            // MAX Processor
  EM_CR = 103,             // National Semiconductor CompactRISC microprocessor
  EM_F2MC16 = 104,         // Fujitsu F2MC16
  EM_MSP430 = 105,         // Texas Instruments embedded microcontroller msp430
  EM_BLACKFIN = 106,       // Analog Devices Blackfin (DSP) processor
  EM_SE_C33 = 107,         // S1C33 Family of Seiko Epson processors
  EM_SEP = 108,            // Sharp embedded microprocessor
  EM_ARCA = 109,           // Arca RISC Microprocessor
  EM_UNICORE = 110,        // Microprocessor series from PKU-Unity Ltd. and MPRC of Peking University
  EM_EXCESS = 111,         // eXcess: 16/32/64-bit configurable embedded CPU
  EM_DXP = 112,            // Icera Semiconductor Inc. Deep Execution Processor
  EM_ALTERA_NIOS2 = 113,   // Altera Nios II soft-core processor
  EM_CRX = 114,            // National Semiconductor CompactRISC CRX
  EM_XGATE = 115,          // Motorola XGATE embedded processor
  EM_C166 = 116,           // Infineon C16x/XC16x processor
  EM_M16C = 117,           // Renesas M16C series microprocessors
  EM_DSPIC30F = 118,       // Microchip Technology dsPIC30F Digital Signal Controller
  EM_CE = 119,             // Freescale Communication Engine RISC core
  EM_M32C = 120,           // Renesas M32C series microprocessors
  EM_TSK3000 = 131,        // Altium TSK3000 core
  EM_RS08 = 132,           // Freescale RS08 embedded processor
  EM_SHARC = 133,          // Analog Devices SHARC family of 32-bit DSP processors
  EM_ECOG2 = 134,          // Cyan Technology eCOG2 microprocessor
  EM_SCORE7 = 135,         // Sunplus S+core7 RISC processor
  EM_DSP24 = 136,          // New Japan Radio (NJR) 24-bit DSP Processor
  EM_VIDEOCORE3 = 137,     // Broadcom VideoCore III processor
  EM_LATTICEMICO32 = 138,  // RISC processor for Lattice FPGA architecture
  EM_SE_C17 = 139,         // Seiko Epson C17 family
  EM_TI_C6000 = 140,       // The Texas Instruments TMS320C6000 DSP family
  EM_TI_C2000 = 141,       // The Texas Instruments TMS320C2000 DSP family
  EM_TI_C5500 = 142,       // The Texas Instruments TMS320C55x DSP family
  EM_MMDSP_PLUS = 160,     // STMicroelectronics 64bit VLIW Data Signal Processor
  EM_CYPRESS_M8C = 161,    // Cypress M8C microprocessor
  EM_R32C = 162,           // Renesas R32C series microprocessors
  EM_TRIMEDIA = 163,       // NXP Semiconductors TriMedia architecture family
  EM_HEXAGON = 164,        // Qualcomm Hexagon processor
  EM_8051 = 165,           // Intel 8051 and variants
  EM_STXP7X = 166,         // STMicroelectronics STxP7x family of configurable and extensible RISC processors
  EM_NDS32 = 167,          // Andes Technology compact code size embedded RISC processor family
  EM_ECOG1 = 168,          // Cyan Technology eCOG1X family
  EM_ECOG1X = 168,         // Cyan Technology eCOG1X family
  EM_MAXQ30 = 169,         // Dallas Semiconductor MAXQ30 Core Micro-controllers
  EM_XIMO16 = 170,         // New Japan Radio (NJR) 16-bit DSP Processor
  EM_MANIK = 171,          // M2000 Reconfigurable RISC Microprocessor
  EM_CRAYNV2 = 172,        // Cray Inc. NV2 vector architecture
  EM_RX = 173,             // Renesas RX family
  EM_METAG = 174,          // Imagination Technologies META processor architecture
  EM_MCST_ELBRUS = 175,    // MCST Elbrus general purpose hardware architecture
  EM_ECOG16 = 176,         // Cyan Technology eCOG16 family
  EM_CR16 = 177,           // National Semiconductor CompactRISC CR16 16-bit microprocessor
  EM_ETPU = 178,           // Freescale Extended Time Processing Unit
  EM_SLE9X = 179,          // Infineon Technologies SLE9X core
  EM_L10M = 180,           // Intel L10M
  EM_K10M = 181,           // Intel K10M
  EM_AARCH64 = 183,        // ARM AArch64
  EM_AVR32 = 185,          // Atmel Corporation 32-bit microprocessor family
  EM_STM8 = 186,           // STMicroeletronics STM8 8-bit microcontroller
  EM_TILE64 = 187,         // Tilera TILE64 multicore architecture family
  EM_TILEPRO = 188,        // Tilera TILEPro multicore architecture family
  EM_CUDA = 190,           // NVIDIA CUDA architecture
  EM_TILEGX = 191,         // Tilera TILE-Gx multicore architecture family
  EM_CLOUDSHIELD = 192,    // CloudShield architecture family
  EM_COREA_1ST = 193,      // KIPO-KAIST Core-A 1st generation processor family
  EM_COREA_2ND = 194,      // KIPO-KAIST Core-A 2nd generation processor family
  EM_ARC_COMPACT2 = 195,   // Synopsys ARCompact V2
  EM_OPEN8 = 196,          // Open8 8-bit RISC soft processor core
  EM_RL78 = 197,           // Renesas RL78 family
  EM_VIDEOCORE5 = 198,     // Broadcom VideoCore V processor
  EM_78KOR = 199,          // Renesas 78KOR family
  EM_56800EX = 200,        // Freescale 56800EX Digital Signal Controller (DSC)
  EM_BA1 = 201,            // Beyond BA1 CPU architecture
  EM_BA2 = 202,            // Beyond BA2 CPU architecture
  EM_XCORE = 203,          // XMOS xCORE processor family
  EM_MCHP_PIC = 204,       // Microchip 8-bit PIC(r) family
  EM_INTEL205 = 205,       // Reserved by Intel
  EM_INTEL206 = 206,       // Reserved by Intel
  EM_INTEL207 = 207,       // Reserved by Intel
  EM_INTEL208 = 208,       // Reserved by Intel
  EM_INTEL209 = 209,       // Reserved by Intel
  EM_KM32 = 210,           // KM211 KM32 32-bit processor
  EM_KMX32 = 211,          // KM211 KMX32 32-bit processor
  EM_KMX16 = 212,          // KM211 KMX16 16-bit processor
  EM_KMX8 = 213,           // KM211 KMX8 8-bit processor
  EM_KVARC = 214,          // KM211 KVARC processor
  EM_CDP = 215,            // Paneve CDP architecture family
  EM_COGE = 216,           // Cognitive Smart Memory Processor
  EM_COOL = 217,           // iCelero CoolEngine
  EM_NORC = 218,           // Nanoradio Optimized RISC
  EM_CSR_KALIMBA = 219,    // CSR Kalimba architecture family
  EM_AMDGPU = 224,         // AMD GPU architecture
  EM_RISCV = 243,          // RISC-V
  EM_LANAI = 244,          // Lanai 32-bit processor
  EM_BPF = 247,            // Linux kernel bpf virtual machine
  EM_VE = 251,             // NEC SX-Aurora VE
  EM_CSKY = 252,           // C-SKY 32-bit processor
};

struct Elf_id_t {
  union {
    std::array<uint8_t, 16> ident;  // ELF "magic number"
    struct {
      uint8_t _mag0;   // 7F
      uint8_t _mag1;   //'E'
      uint8_t _mag2;   //'L'
      uint8_t _mag3;   //'F'
      uint8_t _class;  // 0: invalid, 1: 32-bit objects, 2: 64-bit objects
      uint8_t _data;
      uint8_t _version;
      uint8_t _pad;
      std::array<uint8_t, 8> _nident;
    };
  };
  uint16_t type;
  uint16_t machine;
  uint32_t version;
};
static_assert(24 == sizeof(Elf_id_t), "Alignment issue in Elf_id_t");

struct Elf32_entry_t {
  Elf_id_t id;
  uint32_t entry;  // Entry point
  uint32_t phoff;  // Program header table file offset
  uint32_t shoff;  // Section header table file offset
  uint32_t flags;
  uint16_t ehsize;
  uint16_t phentsize;
  uint16_t phnum;
  uint16_t shentsize;
  uint16_t shnum;
  uint16_t shstrndx;
};
static_assert(52 == sizeof(Elf32_entry_t), "Alignment issue in Elf32_entry_t");

struct Elf64_entry_t {
  Elf_id_t id;
  uint64_t entry;  // Entry point virtual address
  uint64_t phoff;  // Program header table file offset
  uint64_t shoff;  // Section header table file offset
  uint32_t flags;
  uint16_t ehsize;
  uint16_t phentsize;
  uint16_t phnum;
  uint16_t shentsize;
  uint16_t shnum;
  uint16_t shstrndx;
};
static_assert(64 == sizeof(Elf64_entry_t), "Alignment issue in Elf64_entry_t");

struct Elf32_section_t {
  uint32_t name;       // Section name, index in string tbl
  uint32_t type;       // Type of section
  uint32_t flags;      // Miscellaneous section attributes
  uint32_t addr;       // Section virtual addr at execution
  uint32_t offset;     // Section file offset
  uint32_t size;       // Size of section in bytes
  uint32_t link;       // Index of another section
  uint32_t info;       // Additional section information
  uint32_t addralign;  // Section alignment
  uint32_t entsize;    // Entry size if section holds table
};
static_assert(40 == sizeof(Elf32_section_t), "Alignment issue in Elf32_section_t");

struct Elf64_section_t {
  uint32_t name;       // Section name, index in string tbl
  uint32_t type;       // Type of section
  uint64_t flags;      // Miscellaneous section attributes
  uint64_t addr;       // Section virtual addr at execution
  int64_t offset;      // Section file offset
  int64_t size;        // Size of section in bytes
  uint32_t link;       // Index of another section
  uint32_t info;       // Additional section information
  uint64_t addralign;  // Section alignment
  uint64_t entsize;    // Entry size if section holds table
};
static_assert(64 == sizeof(Elf64_section_t), "Alignment issue in Elf64_section_t");

static constexpr uint32_t offset{64};

static const Elf32_entry_t* entry_32{nullptr};
static const Elf64_entry_t* entry_64{nullptr};

auto Header_t::ScanELF(int32_t /*ch*/) noexcept -> Filter {
  if (0x7F454C46 == m4(offset - 0)) {  // \x7FELF
    const uint8_t clss{_buf(offset - 4)};
    if (((ELFCLASS32 == clss) || (ELFCLASS64 == clss)) &&  // class
        (EM_X86_64 == i2(offset - 18)) &&                  // machine
        (1 == i4(offset - 20))) {                          // version
      const uint16_t type{i2(offset - 16)};
      if ((ET_NONE == type) || (ET_REL == type) || (ET_EXEC == type) || (ET_DYN == type) || (ET_CORE == type)) {
        uint32_t i{_buf.Pos() - offset};
        if (ELFCLASS64 == clss) {
          const Elf64_entry_t* entry = reinterpret_cast<Elf64_entry_t*>(&_buf[i]);
          _di.location = int32_t(entry->shoff + (entry->shstrndx * sizeof(Elf64_section_t)));
          entry_32 = nullptr;
          entry_64 = entry;
        } else {
          const Elf32_entry_t* entry = reinterpret_cast<Elf32_entry_t*>(&_buf[i]);
          _di.location = int32_t(entry->shoff + (entry->shstrndx * sizeof(Elf32_section_t)));
          entry_32 = entry;
          entry_64 = nullptr;
        }

        if (_di.location > 0) {
          _di.clss = clss;
          _di.offset_to_start = 0;
          _di.filter_end = INT_MAX;
          return Filter::ELF;
        }
      }
    }
  }

  return Filter::NOFILTER;
}

ELF_filter::ELF_filter(File_t& stream, iEncoder_t* const coder, DataInfo_t& di)
    : _stream{stream},  //
      _coder{coder},
      _di{di} {}

auto ELF_filter::update_mru(int32_t* const mru, const int32_t addr) const noexcept -> int32_t {
  int32_t index{0};
  int32_t needle{mru[0]};
  mru[_call_mru.size() - 1] = addr;
  while (addr != needle) {
    (std::swap)(needle, mru[++index]);
  }
  mru[0] = needle;
  return index;
}

ELF_filter::~ELF_filter() noexcept = default;

void ELF_filter::detect(const int32_t ch) noexcept {
  if ((0xE8 == ch) || (0xE9 == ch) || ((0x80 == (0xF0 & ch)) && (0x0F == _oldc))) {
    _transform = true;
  }
  _oldc = ch;
}

auto ELF_filter::Handle(int32_t ch) noexcept -> bool {  // encoding
  bool status{false};

  if (INT_MAX == (1 + _di.filter_end)) {
    const auto origin{_stream.Position()};
    const int64_t section_location{origin + _di.location - offset - 1};
    _stream.Seek(section_location);
    if (ELFCLASS64 == _di.clss) {
      Elf64_section_t section;
      _stream.Read(&section, sizeof(section));
      if ((1 == section.name) && (3 == section.type)) {
        const auto length{int32_t(section.offset + section.size)};
        _di.filter_end = length;
      } else {
#if 0
        int64_t section_offset{origin + section.offset - offset - 1};
        char* section_names = static_cast<char*>(calloc(1, size_t(section.size)));
        _stream.Seek(section_offset);
        _stream.Read(section_names, size_t(section.size));

        for (uint32_t idx{0}; idx < entry_64->shnum; idx++) {
          section_offset = origin + int64_t(entry_64->shoff) - offset - 1 + int64_t(idx * sizeof(section));
          _stream.Seek(section_offset);
          _stream.Read(&section, sizeof(section));
          if (section.size > 0) {
            const char* name = section_names + section.name;
            fprintf(stderr, "%s %" PRIu32 "%" PRIu64 "\n", name, section.type, section.size);

            section_offset = origin + section.offset - offset - 1;
            if (decodeEncodeCompare(_stream, nullptr, section_offset, section.size) > 0) {
              fprintf(stderr, "%" PRIu64 "\n", section.size);
            }
          }
        }
#endif
        _di.filter_end = 0;
      }
    } else {
      Elf32_section_t section;
      _stream.Read(&section, sizeof(section));
      if ((1 == section.name) && (3 == section.type)) {
        const auto length{int32_t(section.offset + section.size)};
        _di.filter_end = length;
      } else {
        _di.filter_end = 0;
      }
    }
    if (_di.filter_end > 0) {
      _coder->CompressN(32, _di.filter_end);

      _location = offset + 1;
    } else {
      _coder->CompressN(32, _DEADBEEF);

      _di.filter_end = 0;
    }
    _stream.Seek(origin);
  }

  detect(ch);

  if (_transform) {
    _addr[_length++] = uint8_t(ch);
    if (_length >= 5) {
      _length = 0;
      _transform = false;

      // Transform:    E8 aa aa aa 00/FF -> Seek 'a' in MRU, when found write index. Otherwise add location to 'a'
      // Transform:    E9 aa aa aa 00/FF -> Seek 'a' in MRU, when found write index. Otherwise add location to 'a'
      // Transform: 0F 8x aa aa aa 00/FF -> Seek 'a' in MRU, when found write index. Otherwise add location to 'a'
      if ((0x00 == _addr[4]) || (0xFF == _addr[4])) {
        assert((0xE8 == _addr[0]) || (0xE9 == _addr[0]) || (0x80 == (0xF0 & _addr[0])));
        int32_t addr{(_addr[4] << 24) | (_addr[3] << 16) | (_addr[2] << 8) | _addr[1]};

        int32_t* __restrict const mru{(0xE8 == _addr[0]) ? &_call_mru[0] : &_jump_mru[0]};
        const auto index{update_mru(mru, addr)};

        if (int32_t(_call_mru.size() - 1) != index) {
          _addr[1] = 0xFF;
          _addr[2] = 0xFF;
          _addr[3] = uint8_t(index);
          _addr[4] = mru_escape;
        } else {
          addr += _location;
          addr <<= 7;
          addr >>= 7;
          _addr[1] = uint8_t(addr >> 16);
          _addr[2] = uint8_t(addr >> 8);
          _addr[3] = uint8_t(addr);
          _addr[4] = uint8_t(addr >> 24);
        }
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

auto ELF_filter::Handle(int32_t ch, int64_t& pos) noexcept -> bool {  // decoding
  bool status{false};

  if (INT_MAX == (1 + _di.filter_end)) {
    status = true;
    _location = (_location << 8) | ch;
    ++_length;
    if (4 == _length) {
      _length = 0;
      pos -= 4;
      if (_DEADBEEF == uint32_t(_location)) {
        _di.filter_end = 0;
      } else {
        _di.filter_end = _location;
        _location = offset;
      }
    } else {
      ++_di.filter_end;
      return status;
    }
  }

  detect(ch);

  if (_transform) {
    _addr[_length++] = uint8_t(ch);
    if (_length >= 5) {
      _length = 0;
      _transform = false;

      // Transform:    E8 aa aa aa 00/FF -> Seek 'a' in MRU, when found use index. Otherwise subtract location from 'a'
      // Transform:    E9 aa aa aa 00/FF -> Seek 'a' in MRU, when found use index. Otherwise subtract location from 'a'
      // Transform: 0F 8x aa aa aa 00/FF -> Seek 'a' in MRU, when found use index. Otherwise subtract location from 'a'
      if ((0x00 == _addr[4]) || (0xFF == _addr[4]) || (mru_escape == _addr[4])) {
        assert((0xE8 == _addr[0]) || (0xE9 == _addr[0]) || (0x80 == (0xF0 & _addr[0])));
        auto addr{(_addr[1] << 16) | (_addr[2] << 8) | _addr[3] | (_addr[4] << 24)};

        int32_t* __restrict const mru{(0xE8 == _addr[0]) ? &_call_mru[0] : &_jump_mru[0]};

        bool valid{false};
        if (mru_escape == _addr[4]) {
          if ((0xFF == _addr[1]) && (0xFF == _addr[2])) {
            addr = mru[_addr[3]];
            valid = true;
          }
        } else {
          assert((0x00 == _addr[4]) || (0xFF == _addr[4]));
          addr -= _location;
          addr <<= 7;
          addr >>= 7;
          valid = true;
        }

        if (valid) {
          update_mru(mru, addr);

          _addr[1] = uint8_t(addr);
          _addr[2] = uint8_t(addr >> 8);
          _addr[3] = uint8_t(addr >> 16);
          _addr[4] = uint8_t(addr >> 24);
        }
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
