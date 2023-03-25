/* Moruga file compressor based on PAQ8 by Matt Mahoney
 * Release by Marwijn Hessel, May., 2023
 *
 * Copyright (C) 2008 Matt Mahoney, Serge Osnach, Alexander Ratushnyak,
 * Bill Pettis, Przemyslaw Skibinski, Matthew Fite, wowtiger, Andrew Paterson,
 * Jan Ondrus, Andreas Morphis, Pavel L. Holoborodko, KZ., Simon Berger,
 * Neill Corlett, Marwijn Hessel, Mat Chartier
 *
 * We would like to express our gratitude for the endless support of many
 * contributors who encouraged PAQ8 development with ideas, testing,
 * compiling and debugging.
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
#include <bits/std_abs.h>
#include <emmintrin.h>
#include <getopt.h>
#include <immintrin.h>
#include <mmintrin.h>
#include <unistd.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cinttypes>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include "Buffer.h"
#include "File.h"
#include "IntegerXXL.h"
#include "Progress.h"
#include "TxtPrep5.h"
#include "Utilities.h"
#include "filters/filter.h"
#include "iEncoder.h"
#include "iMonitor.h"

#if defined(_WIN32) || defined(_WIN64)
#  include <processthreadsapi.h>
#  include <psapi.h>
#endif

#ifndef DEFAULT_OPTION
#  define DEFAULT_OPTION 4
#endif

namespace {
  using namespace std::literals;

  int32_t level_{DEFAULT_OPTION};  // Compression level 0 to 12

  auto MEM(const int32_t offset = 22) noexcept -> uint64_t {
    return UINT64_C(1) << (offset + level_);
  }

  // Global variables
  int32_t verbose_{0};  // Set during application parameter parsing (not change during activity)
  uint32_t bcount_{7};  // Bit processed (7..0) bcount_=7-bpos
  uint32_t c0_{1};      // Last 0-7 bits of the partial byte with a leading 1 bit (1-255)
  uint32_t c1_{0};      // Last two higher 4-bit nibbles
  uint32_t c2_{0};      // Last two higher 4-bit nibbles
  uint64_t cx_{0};      // Last 8 whole bytes (buf(8)..buf(1)), packed
  uint64_t word_{0};    // checksum of last 0..9, a..z and A..Z, reset to zero otherwise
  uint32_t fails_{0};   //
  uint32_t tt_{0};      //
  uint32_t w5_{0};      //
  uint32_t x5_{0};      //

  const char* inFileName_{nullptr};
  const char* outFileName_{nullptr};

  // #define DEBUG_WRITE_ANALYSIS_ENCODER
  // #define DISABLE_TEXT_PREP
  // #define ENABLE_INTRINSICS
  // #define GENERATE_SQUASH_STRETCH
  // #define TUNING

#if defined(GENERATE_SQUASH_STRETCH)

#  include "Generation.h"  // IWYU pragma: keep

#else

  constexpr std::array<const uint16_t, 0x800> __squash{{
#  include "Squash.txt"   // IWYU pragma: keep
  }};

  [[nodiscard]] constexpr auto Squash(const int32_t pr) noexcept -> uint32_t {  // Conversion from -2048..2047 (clamped) into 0..4095
    if (pr <= ~0x7FF) {
      return 0x000;
    }
    if (pr >= 0x7FF) {
      return 0xFFF;
    }
    if (pr >= 0x000) {
      return 0xFFF - __squash[static_cast<size_t>(pr ^ 0x7FF)];  // 0x7FF - pr
    }
    return __squash[static_cast<size_t>(pr + 0x800)];
  }

  constexpr std::array<const int16_t, 0x800> __stretch{{
#  include "Stretch.txt"  // IWYU pragma: keep
  }};

  [[nodiscard]] constexpr auto Stretch(const uint32_t pr) noexcept -> int32_t {  // Conversion from 0..4095 into -2048..2047
    assert(pr < 0x1000);
    if (pr <= 0x7FF) {
      return -__stretch[static_cast<size_t>(pr ^ 0x7FF)];  // 0x7FF - pr
    }
    return __stretch[static_cast<size_t>(pr & 0x7FF)];  // pr - 0x800
  }

  [[nodiscard]] constexpr auto Stretch256(const int32_t pr) noexcept -> int32_t {  // Conversion from 0..1048575 into -2048..2047
    const auto d{static_cast<uint32_t>(pr) / 256};
#  if 0                   // Does not help much, delta between stretch[d] and stretch[d+1] is around the edges small
    assert(d < 0x1000);
    const int32_t w{pr & 0xFF};
    const int32_t v{(__stretch[d] * (256 - w)) + (__stretch[d + 1] * w)};
    return static_cast<int16_t>(((v > 0) ? (v + 128) : (v - 128)) / 256);
#  else
    return Stretch(d);
#  endif
  }

#endif  // GENERATE_SQUASH_STRETCH

  auto GetDimension(size_t size) noexcept -> std::string {
    static constexpr std::array<const std::string_view, 4> SIZE_DIMS{{"Byte"sv, "KiB"sv, "MiB"sv, "GiB"sv}};

    std::string_view size_dim{SIZE_DIMS[0]};  // Byte
    if (size > 9999999999) {                  //
      size_dim = SIZE_DIMS[3];                // GiB
      size = ((size / 536870912) + 1) / 2;    // 1/1073741824
    } else if (size > 9999999) {              //
      size_dim = SIZE_DIMS[2];                // MiB
      size = ((size / 524288) + 1) / 2;       // 1/1048576
    } else if (size > 9999) {                 //
      size_dim = SIZE_DIMS[1];                // KiB
      size = ((size / 512) + 1) / 2;          // 1/1024
    }

    std::array<char, 32> tmp{};
    snprintf(tmp.data(), tmp.size(), "%4" PRId64 " %s", size, size_dim.data());
    return tmp.data();
  }

  int32_t dp_shift_{14};

  std::array<std::array<int32_t, 256>, 12> smt_;
  std::array<uint32_t, 5> hh_{{0, 0, 0, 0, 0}};

  constexpr std::array<const std::array<uint8_t, 256>, 6> state_table_y0_                   //
      {{{{1,   3,   4,   7,   8,   9,   11,  15,  16,  17,  18,  20,  21,  22,  26,  31,    // 00-0F . . . . . . . . . . . . . . . .
          32,  32,  32,  32,  34,  34,  34,  34,  34,  34,  36,  36,  36,  36,  38,  41,    // 10-1F . . . . . . . . . . . . . . . .
          42,  42,  44,  44,  46,  46,  48,  48,  50,  53,  54,  54,  56,  56,  58,  58,    // 20-2F   ! " # $ % & ' ( ) * + , - . /
          60,  60,  62,  62,  50,  67,  68,  68,  70,  70,  72,  72,  74,  74,  76,  76,    // 30-3F 0 1 2 3 4 5 6 7 8 9 : ; < = > ?
          62,  62,  64,  83,  84,  84,  86,  86,  44,  44,  58,  58,  60,  60,  76,  76,    // 40-4F @ A B C D E F G H I J K L M N O
          78,  78,  80,  93,  94,  94,  96,  96,  48,  48,  88,  88,  80,  103, 104, 104,   // 50-5F P Q R S T U V W X Y Z [ \ ] ^ _
          106, 106, 62,  62,  88,  88,  80,  113, 114, 114, 116, 116, 62,  62,  88,  88,    // 60-6F ` a b c d e f g h i j k l m n o
          90,  123, 124, 124, 126, 126, 62,  62,  98,  98,  90,  133, 134, 134, 136, 136,   // 70-7F p q r s t u v w x y z { | } ~ .
          62,  62,  98,  98,  90,  143, 144, 144, 68,  68,  62,  62,  98,  98,  100, 149,   // 80-8F . . . . . . . . . . . . . . . .
          150, 150, 108, 108, 100, 153, 154, 108, 100, 157, 158, 108, 100, 161, 162, 108,   // 90-9F . . . . . . . . . . . . . . . .
          110, 165, 166, 118, 110, 169, 170, 118, 110, 173, 174, 118, 110, 177, 178, 118,   // A0-AF . . . . . . . . . . . . . . . .
          110, 181, 182, 118, 120, 185, 186, 128, 120, 189, 190, 128, 120, 193, 194, 128,   // B0-BF . . . . . . . . . . . . . . . .
          120, 197, 198, 128, 120, 201, 202, 128, 120, 205, 206, 128, 120, 209, 210, 128,   // C0-CF . . . . . . . . . . . . . . . .
          130, 213, 214, 138, 130, 217, 218, 138, 130, 221, 222, 138, 130, 225, 226, 138,   // D0-DF . . . . . . . . . . . . . . . .
          130, 229, 230, 138, 130, 233, 234, 138, 130, 237, 238, 138, 130, 241, 242, 138,   // E0-EF . . . . . . . . . . . . . . . .
          130, 245, 246, 138, 140, 249, 250, 80,  140, 253, 254, 80,  140, 253, 254, 80}},  // F0-FF . . . . . . . . . . . . . . . .

        {{2,   2,   6,   5,   9,   13,  14,  11,  17,  25,  21,  27,  19,  29,  30,  23,     // 00-0F . . . . . . . . . . . . . . . .
          33,  49,  37,  51,  41,  53,  45,  55,  35,  57,  43,  59,  39,  61,  62,  47,     // 10-1F . . . . . . . . . . . . . . . .
          65,  97,  69,  99,  73,  101, 77,  103, 81,  105, 85,  107, 89,  109, 93,  111,    // 20-2F   ! " # $ % & ' ( ) * + , - . /
          67,  113, 75,  115, 83,  117, 91,  119, 71,  121, 87,  123, 79,  125, 126, 95,     // 30-3F 0 1 2 3 4 5 6 7 8 9 : ; < = > ?
          65,  97,  69,  99,  73,  101, 77,  103, 81,  105, 85,  107, 89,  109, 93,  111,    // 40-4F @ A B C D E F G H I J K L M N O
          65,  97,  69,  99,  73,  101, 77,  103, 81,  105, 85,  107, 89,  109, 93,  111,    // 50-5F P Q R S T U V W X Y Z [ \ ] ^ _
          67,  113, 75,  115, 83,  117, 91,  119, 67,  113, 75,  115, 83,  117, 91,  119,    // 60-6F ` a b c d e f g h i j k l m n o
          71,  121, 87,  123, 71,  121, 87,  123, 79,  125, 79,  125, 79,  130, 128, 95,     // 70-7F p q r s t u v w x y z { | } ~ .
          132, 95,  134, 79,  136, 95,  138, 79,  140, 95,  142, 79,  144, 95,  146, 79,     // 80-8F . . . . . . . . . . . . . . . .
          148, 95,  150, 79,  152, 95,  154, 79,  156, 95,  158, 79,  156, 95,  160, 79,     // 90-9F . . . . . . . . . . . . . . . .
          162, 103, 164, 103, 166, 103, 168, 103, 170, 103, 172, 103, 174, 103, 176, 103,    // A0-AF . . . . . . . . . . . . . . . .
          178, 103, 180, 103, 182, 103, 184, 103, 186, 103, 188, 103, 190, 103, 192, 103,    // B0-BF . . . . . . . . . . . . . . . .
          194, 103, 196, 103, 198, 103, 200, 103, 202, 115, 204, 115, 206, 115, 208, 115,    // C0-CF . . . . . . . . . . . . . . . .
          210, 115, 212, 115, 214, 115, 216, 115, 218, 115, 220, 115, 222, 115, 224, 115,    // D0-DF . . . . . . . . . . . . . . . .
          226, 115, 228, 115, 230, 115, 232, 115, 234, 115, 236, 115, 238, 115, 240, 115,    // E0-EF . . . . . . . . . . . . . . . .
          242, 115, 244, 115, 246, 115, 248, 115, 250, 115, 252, 115, 254, 115, 254, 115}},  // F0-FF . . . . . . . . . . . . . . . .

        {{1,   3,   5,   7,   9,   11,  13,  15,  17,  19,  21,  23,  25,  27,  29,  31,     // 00-0F . . . . . . . . . . . . . . . .
          33,  35,  37,  39,  41,  43,  45,  47,  49,  51,  53,  55,  57,  59,  61,  63,     // 10-1F . . . . . . . . . . . . . . . .
          65,  67,  69,  71,  73,  75,  77,  79,  81,  83,  85,  87,  89,  91,  93,  95,     // 20-2F   ! " # $ % & ' ( ) * + , - . /
          97,  99,  101, 103, 105, 107, 109, 111, 113, 115, 117, 119, 121, 123, 125, 127,    // 30-3F 0 1 2 3 4 5 6 7 8 9 : ; < = > ?
          129, 131, 133, 135, 137, 139, 141, 143, 145, 147, 149, 151, 153, 155, 157, 159,    // 40-4F @ A B C D E F G H I J K L M N O
          161, 163, 165, 167, 169, 171, 173, 175, 177, 179, 181, 183, 185, 187, 189, 191,    // 50-5F P Q R S T U V W X Y Z [ \ ] ^ _
          193, 195, 197, 199, 201, 203, 205, 207, 209, 211, 213, 215, 217, 219, 221, 223,    // 60-6F ` a b c d e f g h i j k l m n o
          225, 227, 229, 231, 233, 235, 237, 239, 241, 243, 245, 247, 249, 251, 253, 127,    // 70-7F p q r s t u v w x y z { | } ~ .
          129, 131, 133, 135, 137, 139, 141, 143, 145, 147, 149, 151, 153, 155, 157, 159,    // 80-8F . . . . . . . . . . . . . . . .
          161, 163, 165, 167, 169, 171, 173, 175, 177, 179, 181, 183, 185, 187, 189, 191,    // 90-9F . . . . . . . . . . . . . . . .
          193, 195, 197, 199, 201, 203, 205, 207, 209, 211, 213, 215, 217, 219, 221, 223,    // A0-AF . . . . . . . . . . . . . . . .
          225, 227, 229, 231, 233, 235, 237, 239, 241, 243, 245, 247, 249, 251, 189, 255,    // B0-BF . . . . . . . . . . . . . . . .
          129, 131, 133, 135, 137, 139, 141, 143, 145, 147, 149, 151, 153, 155, 157, 159,    // C0-CF . . . . . . . . . . . . . . . .
          161, 163, 165, 167, 169, 171, 173, 175, 177, 179, 181, 183, 185, 187, 189, 191,    // D0-DF . . . . . . . . . . . . . . . .
          193, 195, 197, 199, 201, 203, 205, 207, 209, 211, 213, 215, 217, 219, 221, 223,    // E0-EF . . . . . . . . . . . . . . . .
          225, 227, 229, 231, 233, 235, 237, 239, 241, 243, 245, 247, 249, 251, 253, 255}},  // F0-FF . . . . . . . . . . . . . . . .

        {{1,   3,   5,   6,   9,   8,   10,  13,  12,  14,  15,  18,  17,  20,  19,  21,     // 00-0F . . . . . . . . . . . . . . . .
          24,  23,  26,  25,  27,  28,  31,  30,  33,  32,  35,  34,  36,  39,  38,  41,     // 10-1F . . . . . . . . . . . . . . . .
          40,  43,  42,  44,  45,  48,  47,  50,  49,  52,  51,  54,  53,  55,  58,  57,     // 20-2F   ! " # $ % & ' ( ) * + , - . /
          60,  59,  62,  61,  64,  63,  65,  66,  69,  68,  71,  70,  73,  72,  75,  74,     // 30-3F 0 1 2 3 4 5 6 7 8 9 : ; < = > ?
          77,  76,  78,  81,  80,  83,  82,  85,  84,  87,  86,  89,  88,  90,  91,  94,     // 40-4F @ A B C D E F G H I J K L M N O
          93,  96,  95,  98,  97,  100, 99,  102, 101, 104, 103, 105, 108, 107, 110, 109,    // 50-5F P Q R S T U V W X Y Z [ \ ] ^ _
          112, 111, 114, 113, 116, 115, 118, 117, 119, 105, 121, 120, 123, 122, 125, 124,    // 60-6F ` a b c d e f g h i j k l m n o
          127, 126, 129, 128, 131, 130, 133, 132, 134, 137, 38,  139, 138, 141, 140, 143,    // 70-7F p q r s t u v w x y z { | } ~ .
          142, 145, 144, 147, 146, 148, 149, 39,  47,  152, 151, 154, 153, 156, 155, 158,    // 80-8F . . . . . . . . . . . . . . . .
          157, 160, 159, 162, 161, 163, 48,  59,  166, 49,  168, 167, 170, 169, 172, 171,    // 90-9F . . . . . . . . . . . . . . . .
          174, 173, 175, 176, 48,  59,  179, 178, 181, 180, 183, 182, 185, 184, 187, 186,    // A0-AF . . . . . . . . . . . . . . . .
          188, 58,  72,  191, 190, 193, 192, 195, 194, 197, 196, 198, 199, 58,  72,  202,    // B0-BF . . . . . . . . . . . . . . . .
          201, 204, 203, 206, 205, 208, 207, 209, 69,  86,  212, 211, 214, 213, 216, 215,    // C0-CF . . . . . . . . . . . . . . . .
          217, 218, 69,  86,  221, 220, 223, 222, 225, 224, 226, 81,  101, 229, 228, 231,    // D0-DF . . . . . . . . . . . . . . . .
          230, 232, 233, 81,  101, 236, 235, 238, 237, 239, 94,  117, 242, 241, 243, 244,    // E0-EF . . . . . . . . . . . . . . . .
          94,  117, 247, 246, 248, 108, 132, 250, 251, 108, 246, 253, 121, 255, 121, 255}},  // F0-FF . . . . . . . . . . . . . . . .

        {{1,   4,   3,   6,   8,   7,   11,  10,  13,  12,  15,  17,  16,  19,  18,  22,     // 00-0F . . . . . . . . . . . . . . . .
          21,  24,  23,  26,  25,  28,  30,  29,  32,  31,  34,  33,  37,  36,  39,  38,     // 10-1F . . . . . . . . . . . . . . . .
          41,  40,  43,  42,  45,  47,  46,  49,  48,  51,  50,  53,  52,  56,  55,  58,     // 20-2F   ! " # $ % & ' ( ) * + , - . /
          57,  60,  59,  62,  61,  64,  63,  66,  68,  67,  70,  69,  72,  71,  74,  73,     // 30-3F 0 1 2 3 4 5 6 7 8 9 : ; < = > ?
          76,  75,  79,  78,  81,  80,  83,  82,  85,  84,  87,  86,  89,  88,  91,  93,     // 40-4F @ A B C D E F G H I J K L M N O
          92,  95,  94,  97,  96,  99,  98,  101, 100, 103, 102, 106, 105, 108, 107, 110,    // 50-5F P Q R S T U V W X Y Z [ \ ] ^ _
          109, 112, 111, 114, 113, 116, 115, 118, 117, 120, 122, 121, 124, 123, 126, 125,    // 60-6F ` a b c d e f g h i j k l m n o
          128, 127, 130, 129, 132, 131, 118, 133, 135, 134, 137, 136, 139, 138, 141, 140,    // 70-7F p q r s t u v w x y z { | } ~ .
          143, 142, 41,  144, 147, 146, 149, 151, 150, 153, 152, 155, 154, 157, 156, 159,    // 80-8F . . . . . . . . . . . . . . . .
          158, 74,  160, 161, 52,  164, 163, 166, 165, 168, 167, 170, 169, 172, 171, 85,     // 90-9F . . . . . . . . . . . . . . . .
          173, 174, 52,  176, 178, 177, 180, 179, 182, 181, 184, 183, 97,  185, 186, 63,     // A0-AF . . . . . . . . . . . . . . . .
          189, 188, 191, 190, 193, 192, 195, 194, 97,  196, 197, 63,  199, 201, 200, 203,    // B0-BF . . . . . . . . . . . . . . . .
          202, 205, 204, 110, 206, 207, 75,  210, 209, 212, 211, 214, 213, 124, 215, 216,    // C0-CF . . . . . . . . . . . . . . . .
          75,  218, 220, 219, 222, 221, 124, 223, 224, 88,  227, 226, 229, 228, 137, 230,    // D0-DF . . . . . . . . . . . . . . . .
          231, 88,  233, 235, 234, 151, 236, 237, 102, 240, 239, 151, 241, 242, 102, 244,    // E0-EF . . . . . . . . . . . . . . . .
          164, 245, 246, 117, 176, 248, 249, 117, 244, 251, 133, 253, 133, 255, 148, 255}},  // F0-FF . . . . . . . . . . . . . . . .

        {{10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 12, 14, 68, 70, 70, 70,      // 00-0F . . . . . . . . . . . . . . . .
          10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,      // 10-1F . . . . . . . . . . . . . . . .
          10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,      // 20-2F   ! " # $ % & ' ( ) * + , - . /
          10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,      // 30-3F 0 1 2 3 4 5 6 7 8 9 : ; < = > ?
          10, 10, 10, 10, 68, 70, 70, 70, 10, 10, 10, 10, 10, 10, 10, 10,      // 40-4F @ A B C D E F G H I J K L M N O
          10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,      // 50-5F P Q R S T U V W X Y Z [ \ ] ^ _
          10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,      // 60-6F ` a b c d e f g h i j k l m n o
          10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,      // 70-7F p q r s t u v w x y z { | } ~ .
          10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,      // 80-8F . . . . . . . . . . . . . . . .
          10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,      // 90-9F . . . . . . . . . . . . . . . .
          10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,      // A0-AF . . . . . . . . . . . . . . . .
          10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,      // B0-BF . . . . . . . . . . . . . . . .
          10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,      // C0-CF . . . . . . . . . . . . . . . .
          10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,      // D0-DF . . . . . . . . . . . . . . . .
          10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,      // E0-EF . . . . . . . . . . . . . . . .
          10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10}}}};  // F0-FF . . . . . . . . . . . . . . . .

  constexpr std::array<const std::array<uint8_t, 256>, 6> state_table_y1_                    //
      {{{{2,   5,   6,   10,  12,  13,  14,  19,  23,  24,  25,  27,  28,  29,  30,  33,     // 00-0F . . . . . . . . . . . . . . . .
          35,  35,  35,  35,  37,  37,  37,  37,  37,  37,  39,  39,  39,  39,  40,  43,     // 10-1F . . . . . . . . . . . . . . . .
          45,  45,  47,  47,  49,  49,  51,  51,  52,  43,  57,  57,  59,  59,  61,  61,     // 20-2F   ! " # $ % & ' ( ) * + , - . /
          63,  63,  65,  65,  66,  55,  57,  57,  73,  73,  75,  75,  77,  77,  79,  79,     // 30-3F 0 1 2 3 4 5 6 7 8 9 : ; < = > ?
          81,  81,  82,  69,  71,  71,  73,  73,  59,  59,  61,  61,  49,  49,  89,  89,     // 40-4F @ A B C D E F G H I J K L M N O
          91,  91,  92,  69,  87,  87,  45,  45,  99,  99,  101, 101, 102, 69,  87,  87,     // 50-5F P Q R S T U V W X Y Z [ \ ] ^ _
          57,  57,  109, 109, 111, 111, 112, 85,  87,  87,  57,  57,  119, 119, 121, 121,    // 60-6F ` a b c d e f g h i j k l m n o
          122, 85,  97,  97,  57,  57,  129, 129, 131, 131, 132, 85,  97,  97,  57,  57,     // 70-7F p q r s t u v w x y z { | } ~ .
          139, 139, 141, 141, 142, 95,  97,  97,  57,  57,  81,  81,  147, 147, 148, 95,     // 80-8F . . . . . . . . . . . . . . . .
          107, 107, 151, 151, 152, 95,  107, 155, 156, 95,  107, 159, 160, 105, 107, 163,    // 90-9F . . . . . . . . . . . . . . . .
          164, 105, 117, 167, 168, 105, 117, 171, 172, 105, 117, 175, 176, 105, 117, 179,    // A0-AF . . . . . . . . . . . . . . . .
          180, 115, 117, 183, 184, 115, 127, 187, 188, 115, 127, 191, 192, 115, 127, 195,    // B0-BF . . . . . . . . . . . . . . . .
          196, 115, 127, 199, 200, 115, 127, 203, 204, 115, 127, 207, 208, 125, 127, 211,    // C0-CF . . . . . . . . . . . . . . . .
          212, 125, 137, 215, 216, 125, 137, 219, 220, 125, 137, 223, 224, 125, 137, 227,    // D0-DF . . . . . . . . . . . . . . . .
          228, 125, 137, 231, 232, 125, 137, 235, 236, 125, 137, 239, 240, 125, 137, 243,    // E0-EF . . . . . . . . . . . . . . . .
          244, 135, 137, 247, 248, 135, 69,  251, 252, 135, 69,  255, 252, 135, 69,  255}},  // F0-FF . . . . . . . . . . . . . . . .

        {{3,   3,   4,   7,   12,  10,  8,   15,  24,  18,  26,  22,  28,  20,  16,  31,     // 00-0F . . . . . . . . . . . . . . . .
          48,  34,  50,  38,  52,  42,  54,  46,  56,  36,  58,  44,  60,  40,  32,  63,     // 10-1F . . . . . . . . . . . . . . . .
          96,  66,  98,  70,  100, 74,  102, 78,  104, 82,  106, 86,  108, 90,  110, 94,     // 20-2F   ! " # $ % & ' ( ) * + , - . /
          112, 68,  114, 76,  116, 84,  118, 92,  120, 72,  122, 88,  124, 80,  64,  127,    // 30-3F 0 1 2 3 4 5 6 7 8 9 : ; < = > ?
          96,  66,  98,  70,  100, 74,  102, 78,  104, 82,  106, 86,  108, 90,  110, 94,     // 40-4F @ A B C D E F G H I J K L M N O
          96,  66,  98,  70,  100, 74,  102, 78,  104, 82,  106, 86,  108, 90,  110, 94,     // 50-5F P Q R S T U V W X Y Z [ \ ] ^ _
          112, 68,  114, 76,  116, 84,  118, 92,  112, 68,  114, 76,  116, 84,  118, 92,     // 60-6F ` a b c d e f g h i j k l m n o
          120, 72,  122, 88,  120, 72,  122, 88,  124, 80,  124, 80,  131, 80,  64,  129,    // 70-7F p q r s t u v w x y z { | } ~ .
          64,  133, 80,  135, 64,  137, 80,  139, 64,  141, 80,  143, 64,  145, 80,  147,    // 80-8F . . . . . . . . . . . . . . . .
          64,  149, 80,  151, 64,  153, 80,  155, 64,  157, 80,  159, 64,  157, 80,  161,    // 90-9F . . . . . . . . . . . . . . . .
          104, 163, 104, 165, 104, 167, 104, 169, 104, 171, 104, 173, 104, 175, 104, 177,    // A0-AF . . . . . . . . . . . . . . . .
          104, 179, 104, 181, 104, 183, 104, 185, 104, 187, 104, 189, 104, 191, 104, 193,    // B0-BF . . . . . . . . . . . . . . . .
          104, 195, 104, 197, 104, 199, 104, 201, 116, 203, 116, 205, 116, 207, 116, 209,    // C0-CF . . . . . . . . . . . . . . . .
          116, 211, 116, 213, 116, 215, 116, 217, 116, 219, 116, 221, 116, 223, 116, 225,    // D0-DF . . . . . . . . . . . . . . . .
          116, 227, 116, 229, 116, 231, 116, 233, 116, 235, 116, 237, 116, 239, 116, 241,    // E0-EF . . . . . . . . . . . . . . . .
          116, 243, 116, 245, 116, 247, 116, 249, 116, 251, 116, 253, 116, 255, 116, 255}},  // F0-FF . . . . . . . . . . . . . . . .

        {{2,   4,   6,   8,   10,  12,  14,  16,  18,  20,  22,  24,  26,  28,  30,  32,     // 00-0F . . . . . . . . . . . . . . . .
          34,  36,  38,  40,  42,  44,  46,  48,  50,  52,  54,  56,  58,  60,  62,  64,     // 10-1F . . . . . . . . . . . . . . . .
          66,  68,  70,  72,  74,  76,  78,  80,  82,  84,  86,  88,  90,  92,  94,  96,     // 20-2F   ! " # $ % & ' ( ) * + , - . /
          98,  100, 102, 104, 106, 108, 110, 112, 114, 116, 118, 120, 122, 124, 126, 128,    // 30-3F 0 1 2 3 4 5 6 7 8 9 : ; < = > ?
          130, 132, 134, 136, 138, 140, 142, 144, 146, 148, 150, 152, 154, 156, 158, 160,    // 40-4F @ A B C D E F G H I J K L M N O
          162, 164, 166, 168, 170, 172, 174, 176, 178, 180, 182, 184, 186, 188, 190, 192,    // 50-5F P Q R S T U V W X Y Z [ \ ] ^ _
          194, 196, 198, 200, 202, 204, 206, 208, 210, 212, 214, 216, 218, 220, 222, 224,    // 60-6F ` a b c d e f g h i j k l m n o
          226, 228, 230, 232, 234, 236, 238, 240, 242, 244, 246, 248, 250, 252, 254, 128,    // 70-7F p q r s t u v w x y z { | } ~ .
          130, 132, 134, 136, 138, 140, 142, 144, 146, 148, 150, 152, 154, 156, 158, 160,    // 80-8F . . . . . . . . . . . . . . . .
          162, 164, 166, 168, 170, 172, 174, 176, 178, 180, 182, 184, 186, 188, 190, 192,    // 90-9F . . . . . . . . . . . . . . . .
          194, 196, 198, 200, 202, 204, 206, 208, 210, 212, 214, 216, 218, 220, 222, 224,    // A0-AF . . . . . . . . . . . . . . . .
          226, 228, 230, 232, 234, 236, 238, 240, 242, 244, 246, 248, 250, 252, 190, 192,    // B0-BF . . . . . . . . . . . . . . . .
          130, 132, 134, 136, 138, 140, 142, 144, 146, 148, 150, 152, 154, 156, 158, 160,    // C0-CF . . . . . . . . . . . . . . . .
          162, 164, 166, 168, 170, 172, 174, 176, 178, 180, 182, 184, 186, 188, 190, 192,    // D0-DF . . . . . . . . . . . . . . . .
          194, 196, 198, 200, 202, 204, 206, 208, 210, 212, 214, 216, 218, 220, 222, 224,    // E0-EF . . . . . . . . . . . . . . . .
          226, 228, 230, 232, 234, 236, 238, 240, 242, 244, 246, 248, 250, 252, 254, 192}},  // F0-FF . . . . . . . . . . . . . . . .

        {{2,   5,   4,   8,   7,   9,   12,  11,  14,  13,  17,  16,  19,  18,  20,  23,     // 00-0F . . . . . . . . . . . . . . . .
          22,  25,  24,  27,  26,  30,  29,  32,  31,  34,  33,  35,  38,  37,  40,  39,     // 10-1F . . . . . . . . . . . . . . . .
          42,  41,  44,  43,  47,  46,  49,  48,  51,  50,  53,  52,  54,  57,  56,  59,     // 20-2F   ! " # $ % & ' ( ) * + , - . /
          58,  61,  60,  63,  62,  65,  64,  68,  67,  70,  69,  72,  71,  74,  73,  76,     // 30-3F 0 1 2 3 4 5 6 7 8 9 : ; < = > ?
          75,  77,  80,  79,  82,  81,  84,  83,  86,  85,  88,  87,  90,  89,  93,  92,     // 40-4F @ A B C D E F G H I J K L M N O
          95,  94,  97,  96,  99,  98,  101, 100, 103, 102, 104, 107, 106, 109, 108, 111,    // 50-5F P Q R S T U V W X Y Z [ \ ] ^ _
          110, 113, 112, 115, 114, 117, 116, 119, 118, 120, 106, 122, 121, 124, 123, 126,    // 60-6F ` a b c d e f g h i j k l m n o
          125, 128, 127, 130, 129, 132, 131, 133, 136, 135, 138, 39,  140, 139, 142, 141,    // 70-7F p q r s t u v w x y z { | } ~ .
          144, 143, 146, 145, 148, 147, 38,  150, 151, 48,  153, 152, 155, 154, 157, 156,    // 80-8F . . . . . . . . . . . . . . . .
          159, 158, 161, 160, 162, 47,  164, 165, 60,  167, 50,  169, 168, 171, 170, 173,    // 90-9F . . . . . . . . . . . . . . . .
          172, 175, 174, 47,  177, 178, 60,  180, 179, 182, 181, 184, 183, 186, 185, 187,    // A0-AF . . . . . . . . . . . . . . . .
          57,  189, 190, 73,  192, 191, 194, 193, 196, 195, 198, 197, 57,  200, 201, 73,     // B0-BF . . . . . . . . . . . . . . . .
          203, 202, 205, 204, 207, 206, 208, 68,  210, 211, 87,  213, 212, 215, 214, 217,    // C0-CF . . . . . . . . . . . . . . . .
          216, 68,  219, 220, 87,  222, 221, 224, 223, 225, 80,  227, 228, 102, 230, 229,    // D0-DF . . . . . . . . . . . . . . . .
          232, 231, 80,  234, 235, 102, 237, 236, 238, 93,  240, 241, 118, 243, 242, 93,     // E0-EF . . . . . . . . . . . . . . . .
          245, 246, 118, 247, 107, 249, 250, 133, 107, 252, 247, 120, 254, 120, 254, 134}},  // F0-FF . . . . . . . . . . . . . . . .

        {{2,   3,   5,   7,   6,   9,   10,  12,  11,  14,  16,  15,  18,  17,  20,  21,     // 00-0F . . . . . . . . . . . . . . . .
          23,  22,  25,  24,  27,  29,  28,  31,  30,  33,  32,  35,  36,  38,  37,  40,     // 10-1F . . . . . . . . . . . . . . . .
          39,  42,  41,  44,  46,  45,  48,  47,  50,  49,  52,  51,  54,  55,  57,  56,     // 20-2F   ! " # $ % & ' ( ) * + , - . /
          59,  58,  61,  60,  63,  62,  65,  67,  66,  69,  68,  71,  70,  73,  72,  75,     // 30-3F 0 1 2 3 4 5 6 7 8 9 : ; < = > ?
          74,  77,  78,  80,  79,  82,  81,  84,  83,  86,  85,  88,  87,  90,  92,  91,     // 40-4F @ A B C D E F G H I J K L M N O
          94,  93,  96,  95,  98,  97,  100, 99,  102, 101, 104, 105, 107, 106, 109, 108,    // 50-5F P Q R S T U V W X Y Z [ \ ] ^ _
          111, 110, 113, 112, 115, 114, 117, 116, 119, 121, 120, 123, 122, 125, 124, 127,    // 60-6F ` a b c d e f g h i j k l m n o
          126, 129, 128, 131, 130, 133, 132, 119, 134, 136, 135, 138, 137, 140, 139, 142,    // 70-7F p q r s t u v w x y z { | } ~ .
          141, 144, 143, 42,  145, 148, 150, 149, 152, 151, 154, 153, 156, 155, 158, 157,    // 80-8F . . . . . . . . . . . . . . . .
          160, 159, 75,  51,  162, 163, 165, 164, 167, 166, 169, 168, 171, 170, 173, 172,    // 90-9F . . . . . . . . . . . . . . . .
          86,  51,  175, 177, 176, 179, 178, 181, 180, 183, 182, 185, 184, 98,  62,  187,    // A0-AF . . . . . . . . . . . . . . . .
          188, 190, 189, 192, 191, 194, 193, 196, 195, 98,  62,  198, 200, 199, 202, 201,    // B0-BF . . . . . . . . . . . . . . . .
          204, 203, 206, 205, 111, 74,  208, 209, 211, 210, 213, 212, 215, 214, 125, 74,     // C0-CF . . . . . . . . . . . . . . . .
          217, 219, 218, 221, 220, 223, 222, 125, 87,  225, 226, 228, 227, 230, 229, 138,    // D0-DF . . . . . . . . . . . . . . . .
          87,  232, 234, 233, 236, 235, 152, 101, 238, 239, 241, 240, 152, 101, 243, 245,    // E0-EF . . . . . . . . . . . . . . . .
          244, 165, 116, 247, 248, 177, 116, 250, 245, 132, 252, 132, 254, 147, 254, 147}},  // F0-FF . . . . . . . . . . . . . . . .

        {{11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 13, 15, 69, 69, 69, 71,      // 00-0F . . . . . . . . . . . . . . . .
          11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11,      // 10-1F . . . . . . . . . . . . . . . .
          11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11,      // 20-2F   ! " # $ % & ' ( ) * + , - . /
          11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11,      // 30-3F 0 1 2 3 4 5 6 7 8 9 : ; < = > ?
          11, 11, 11, 11, 69, 69, 69, 71, 11, 11, 11, 11, 11, 11, 11, 11,      // 40-4F @ A B C D E F G H I J K L M N O
          11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11,      // 50-5F P Q R S T U V W X Y Z [ \ ] ^ _
          11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11,      // 60-6F ` a b c d e f g h i j k l m n o
          11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11,      // 70-7F p q r s t u v w x y z { | } ~ .
          11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11,      // 80-8F . . . . . . . . . . . . . . . .
          11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11,      // 90-9F . . . . . . . . . . . . . . . .
          11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11,      // A0-AF . . . . . . . . . . . . . . . .
          11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11,      // B0-BF . . . . . . . . . . . . . . . .
          11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11,      // C0-CF . . . . . . . . . . . . . . . .
          11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11,      // D0-DF . . . . . . . . . . . . . . . .
          11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11,      // E0-EF . . . . . . . . . . . . . . . .
          11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11}}}};  // F0-FF . . . . . . . . . . . . . . . .

  constexpr std::array<const uint8_t, 256> WRT_mxr{{0,  30, 28, 0,  0,  0,  10, 0,  0,  4,  28, 0,  0,  0,  0,  0,     // 00-0F . . . . . . . . . . . . . . . .
                                                    4,  0,  8,  0,  0,  0,  0,  0,  10, 20, 0,  0,  0,  0,  0,  0,     // 10-1F . . . . . . . . . . . . . . . .
                                                    4,  20, 26, 20, 22, 28, 26, 22, 26, 20, 8,  30, 22, 30, 24, 30,    // 20-2F   ! " # $ % & ' ( ) * + , - . /
                                                    2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  20, 8,  0,  8,  0,  24,    // 30-3F 0 1 2 3 4 5 6 7 8 9 : ; < = > ?
                                                    18, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,    // 40-4F @ A B C D E F G H I J K L M N O
                                                    10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 8,  18, 6,  26, 30,    // 50-5F P Q R S T U V W X Y Z [ \ ] ^ _
                                                    18, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,    // 60-6F ` a b c d e f g h i j k l m n o
                                                    10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 30, 8,  26, 18, 20,    // 70-7F p q r s t u v w x y z { | } ~ .
                                                    12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12,    // 80-8F . . . . . . . . . . . . . . . .
                                                    12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12,    // 90-9F . . . . . . . . . . . . . . . .
                                                    12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12,    // A0-AF . . . . . . . . . . . . . . . .
                                                    12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12,    // B0-BF . . . . . . . . . . . . . . . .
                                                    12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12,    // C0-CF . . . . . . . . . . . . . . . .
                                                    14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14,    // D0-DF . . . . . . . . . . . . . . . .
                                                    14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14,    // E0-EF . . . . . . . . . . . . . . . .
                                                    16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16}};  // F0-FF . . . . . . . . . . . . . . . .

  constexpr auto limits_15a(const uint32_t idx) noexcept -> uint8_t {
    switch (idx) {  // clang-format off
      default:   return 0;
      case 0x0A:
      case 0x0B: return 24;
      case 0x0C:
      case 0x0D:
      case 0x0E:
      case 0x0F: return 16;
      case 'E':              // 0x45
      case 'F':  return 176; // 0x46
      case 'D':              // 0x44
      case 'G':  return 2;   // 0x47
    }  // clang-format on
  }

  constexpr auto limits_15b(const uint32_t idx) noexcept -> uint8_t {
    switch (idx) {  // clang-format off
      default:   return 0;
      case 0x0A:
      case 0x0B: return 18;
      case 0x0C:
      case 0x0D:
      case 0x0E:
      case 0x0F: return 12;
      case 'E':              // 0x45
      case 'F':  return 168; // 0x46
      case 'D':              // 0x44
      case 'G':  return 1;   // 0x47
    }  // clang-format on
  }

  template <typename T>
  [[nodiscard]] constexpr auto clamp12(const T& val) noexcept -> T {
    return std::clamp(val, T{~0x7FF}, T{0x7FF});  // -2048..2047
  }
};  // namespace

auto GetInFileName() noexcept -> const char* {
  return inFileName_;
}

auto GetOutFileName() noexcept -> const char* {
  return outFileName_;
}

/**
 * @class APM_t
 * @brief Adaptive probability maps (APM)
 *
 * Adaptive probability maps (APM)
 */
class APM_t final {
public:
  explicit APM_t(const uint64_t n, const uint32_t scale, const uint32_t start = 8) noexcept
      : N{(n * 24) + 1},  //
        _mask{static_cast<uint32_t>(n - 1)},
        _map{static_cast<Map_t*>(std::calloc(N, sizeof(Map_t)))} {
    assert(ISPOWEROF2(n));
    if (verbose_) {
      fprintf(stdout, "%s for APM_t\n", GetDimension(N * sizeof(Map_t)).c_str());
    }

    for (uint32_t i{0}; i < _dt.size(); ++i) {
      const auto dt{scale / (i + 4)};
      assert(dt < 0x10000);
      _dt[i] = static_cast<int16_t>(dt);
    }

    for (auto i{N}; i--;) {
      const auto pr{(8 == start) ? ((((i % 24) * 2) + 1) * 4096) / (24 * 2) :  //
                        /*        */ ((i % 24) * 4096) / (24 - 1)};
      const auto prediction{Squash(static_cast<int32_t>(pr) - 2048) * (UINT32_C(1) << 10)};  // Conversion from -2048..2047 (clamped) into 0..4095
      _map[i].prediction = MASK_22_BITS & prediction;
      _map[i].count = MASK_10_BITS & start;

      assert((_map[i].value >> 10) == prediction);
      assert((_map[i].value & MASK_10_BITS) == start);
    }
  }
  virtual ~APM_t() noexcept;

  APM_t() = delete;
  APM_t(const APM_t&) = delete;
  APM_t(APM_t&&) = delete;
  auto operator=(const APM_t&) -> APM_t& = delete;
  auto operator=(APM_t&&) -> APM_t& = delete;

  [[nodiscard]] auto Predict(const bool bit, const int32_t pr, const uint32_t cx) noexcept -> uint32_t {
    Update(bit);
    return Predict(pr, cx);
  }

private:
  void Update(const bool bit) noexcept {
    auto& map{_map[_ctx]};
    const auto count{map.count};
    const auto err{((bit << 22) - map.prediction) / 8};
    map.value = static_cast<uint32_t>(static_cast<int32_t>(map.value) + ((err * _dt[count]) & -0x400)) + (count < 0x3FF);
  }

  [[nodiscard]] auto Predict(const int32_t prediction, const uint32_t context) noexcept -> uint32_t {
    assert(prediction >= -2048);
    assert(prediction < 2048);
    assert((context & _mask) < (N / 24));
    const auto pr{static_cast<uint32_t>(prediction + 2048) * (24 - 1)};  // Conversion from -2048..2047 into 0..94185
    const auto cx{(24 * (context & _mask)) + (pr / 4096)};
    assert(cx < (N - 1));
    _ctx = cx;
    const auto weight{0xFFF & pr};  // interpolation weight of next element
    if (0 == weight) {
      return _map[cx].value / 1048576;
    }
    if (weight / 2048) {
      ++_ctx;
    }
    assert(_ctx < N);
#if 0
    const auto vx{_map[cx + 0].prediction / 8};
    const auto vy{_map[cx + 1].prediction / 8};
    if (vx == vy) {
      return vx / 128;
    }
    const auto py{((vx * 4096) - ((vx - vy) * weight)) >> 19};  // Calculate new prediction
#else
    const auto vx{static_cast<uint64_t>(_map[cx + 0].value)};   // Prediction is needed, count is shifted out later on
    const auto vy{static_cast<uint64_t>(_map[cx + 1].value)};   // Prediction is needed, count is shifted out later on
    const auto py{((vx * 4096) - ((vx - vy) * weight)) >> 32};  // Calculate new prediction, lose count
#endif
    assert(py < 0x1000);
    return static_cast<uint32_t>(py);
  }

  union Map_t {
    struct {
      uint32_t count : 10;
      uint32_t prediction : 22;
    };
    uint32_t value;
  };
  static_assert(4 == sizeof(Map_t), "Alignment issue in Map_t");

  static constexpr auto MASK_10_BITS{(UINT32_C(1) << 10) - 1};
  static constexpr auto MASK_22_BITS{(UINT32_C(1) << 22) - 1};

  std::array<int16_t, 0x400> _dt{};  // 10 bit curve 'scale/(i+4)'
  const uint64_t N;                  // Number of contexts
  const uint32_t _mask;              // ctx limit
  uint32_t _ctx{0};                  // Context of last prediction
  Map_t* const __restrict _map;      // ctx -> prediction
};
APM_t::~APM_t() noexcept {
  std::free(_map);
}

#if defined(ENABLE_INTRINSICS)

// https://gcc.gnu.org/onlinedocs/gcc/Vector-Extensions.html
// https://www.intel.com/content/www/us/en/docs/intrinsics-guide/index.html#

union Exchange_t final {
  explicit Exchange_t(const int16_t* const __restrict p) noexcept : i16{p} {}
  explicit Exchange_t(const int32_t* const __restrict p) noexcept : i32{p} {}
  const int16_t* const __restrict i16;
  const int32_t* const __restrict i32;
  __m64* const __restrict m64;     // __MMX__
  __m128i* const __restrict m128;  // __SSE2__
  __v8si* const __restrict i32x8;  // __AVX__
};

#endif  // ENABLE_INTRINSICS

/**
 * @class Mixer_t
 * @brief Combines models using a neural network
 *
 * Combines models using a neural network (using intrinsics functions if available)
 */
class Mixer_t final {
public:
  static constexpr uint32_t N_LAYERS{9};  // Number of neurons in the input layer

  static std::array<int32_t, N_LAYERS> tx_;

  Mixer_t() noexcept {
    tx_.fill(0);
    wx_.fill(0xA00);
  }

  ~Mixer_t() noexcept = default;

  Mixer_t(const Mixer_t&) = delete;
  Mixer_t(Mixer_t&&) = delete;
  auto operator=(const Mixer_t&) -> Mixer_t& = delete;
  auto operator=(Mixer_t&&) -> Mixer_t& = delete;

  void Update(const int32_t err) noexcept {
    assert((err + 4096) < 8192);
    train(&tx_[0], &wx_[ctx_], err);
  }

  [[nodiscard]] auto Predict() noexcept -> int32_t {
    const auto sum{dot_product(&tx_[0], &wx_[ctx_])};
    const auto pr{sum / (1 << dp_shift_)};
    return clamp12(pr);
  }

  void Context(const uint32_t ctx) noexcept {
    ctx_ = ctx;
  }

  void ScaleUp() noexcept {
    for (auto n{wx_.size()}; n--;) {
      wx_[n] = Utilities::safe_add(wx_[n], wx_[n]);
    }
  }

private:
  void train(const int32_t* const __restrict t, int32_t* const __restrict w, const int32_t err) const noexcept {
#if defined(ENABLE_INTRINSICS) && defined(__AVX__)
    if (7 & ctx_) {
      const auto* __restrict tt{t};
      auto* __restrict ww{w};
      for (auto n{N_LAYERS}; n--;) {
        *ww++ += (((*tt++ * err) >> 13) + 1) >> 1;
      }
    } else {
      const Exchange_t tt{t};
      Exchange_t ww{w};
      *ww.i32x8 += (((*tt.i32x8 * err) >> 13) + 1) >> 1;  // ww[0..7] += (((tt[0..7] * err) >> 13) + 1) >> 1
      w[8] += (((t[8] * err) >> 13) + 1) >> 1;
    }
#else
    const auto* __restrict tt{t};
    auto* __restrict ww{w};
    for (auto n{N_LAYERS}; n--;) {
      *ww++ += (((*tt++ * err) >> 13) + 1) >> 1;
    }
#endif
  }

  [[nodiscard]] auto dot_product(const int32_t* const __restrict t, const int32_t* const __restrict w) const noexcept -> int32_t {
    int32_t sum{0};
#if defined(ENABLE_INTRINSICS) && defined(__AVX__)
    if (7 & ctx_) {
      const auto* __restrict tt{t};
      const auto* __restrict ww{w};
      for (auto n{N_LAYERS}; n--;) {
        sum += *ww++ * *tt++;
      }
    } else {
      const Exchange_t tt{t};
      const Exchange_t ww{w};
      const auto x{*ww.i32x8 * *tt.i32x8};
      for (auto n{N_LAYERS - 1}; n--;) {
        sum += x[n];
      }
      sum += w[8] * t[8];
    }
#else
    const auto* __restrict tt{t};
    const auto* __restrict ww{w};
    for (auto n{N_LAYERS}; n--;) {
      sum += *ww++ * *tt++;
    }
#endif
    return sum;
  }

  alignas(32) std::array<int32_t, N_LAYERS * 1280> wx_{};

  uint32_t ctx_{0};
  int32_t : 32;  // Padding
  int32_t : 32;  // Padding
  int32_t : 32;  // Padding
  int32_t : 32;  // Padding
  int32_t : 32;  // Padding
  int32_t : 32;  // Padding
  int32_t : 32;  // Padding
};

alignas(32) std::array<int32_t, Mixer_t::N_LAYERS> Mixer_t::tx_;  // Range -2048..2047

/**
 * @class Blend_t
 * @brief Combines predictions using a neural network
 *
 * Combines predictions using a neural network (using intrinsics functions if available)
 */
template <const uint32_t N_LAYERS>
class Blend_t final {
  static_assert(ISPOWEROF2(N_LAYERS), "Number of layers must be a power of two");

public:
  explicit Blend_t(const uint32_t n, const int16_t weight) noexcept
      : _mask{n - 1},  //
        _weights{static_cast<int16_t*>(std::calloc(n * N_LAYERS, sizeof(int16_t)))} {
    assert(ISPOWEROF2(n));
    assert((2 * N_LAYERS) <= _pi.size());
    if (verbose_) {
      fprintf(stdout, "%s for Blend_t\n", GetDimension(n * N_LAYERS * sizeof(int16_t)).c_str());
    }
    for (auto i{n * N_LAYERS}; i--;) {
      _weights[i] = weight;
    }
  }

  virtual ~Blend_t() noexcept {
    std::free(_weights);
  }

  Blend_t() = delete;
  Blend_t(const Blend_t&) = delete;
  Blend_t(Blend_t&&) = delete;
  auto operator=(const Blend_t&) -> Blend_t& = delete;
  auto operator=(Blend_t&&) -> Blend_t& = delete;

  [[nodiscard]] auto Get() noexcept -> std::array<int16_t, N_LAYERS>& {
    return reinterpret_cast<std::array<int16_t, N_LAYERS>&>(*_new);
  }

  [[nodiscard]] auto Predict(const int32_t err, const uint32_t context) noexcept -> int32_t {
    if ((std::abs)(err) > 32) {
      const auto mismatch{std::clamp(err, SHRT_MIN, SHRT_MAX)};
      train(_prv, &_weights[_ctx], mismatch);
    }
    _ctx = (context & _mask) * N_LAYERS;
    const auto sum{dot_product(_new, &_weights[_ctx])};
    std::swap(_new, _prv);
    const auto pr{clamp12(sum >> 14)};
    return pr;
  }

private:
  void train(const int16_t* const __restrict t, int16_t* const __restrict w, const int32_t err_) const noexcept {
    assert((err_ >= SHRT_MIN) && (err_ <= SHRT_MAX));
#if defined(ENABLE_INTRINSICS) && defined(__SSE2__)
    const Exchange_t tt{t};
    Exchange_t ww{w};
    if constexpr (4 == N_LAYERS) {
      const auto one{_mm_set1_pi16(1)};                            //
      const auto err{_mm_set1_pi16(static_cast<int16_t>(err_))};   //
      auto var{_mm_mulhi_pi16(*tt.m64, err)};                      //              (t[0..3] * err) >> 16
      var = _mm_adds_pi16(var, one);                               //             ((t[0..3] * err) >> 16) + 1
      var = _mm_srai_pi16(var, 1);                                 //            (((t[0..3] * err) >> 16) + 1) >> 1
      *ww.m64 = _mm_adds_pi16(*ww.m64, var);                       // w[0..3] + ((((t[0..3] * err) >> 16) + 1) >> 1)
      _mm_empty();                                                 //
    } else {                                                       //
      const auto one{_mm_set1_epi16(1)};                           //
      const auto err{_mm_set1_epi16(static_cast<int16_t>(err_))};  //
      auto var{_mm_mulhi_epi16(*tt.m128, err)};                    //              (t[0..7] * err) >> 16
      var = _mm_adds_epi16(var, one);                              //             ((t[0..7] * err) >> 16) + 1
      var = _mm_srai_epi16(var, 1);                                //            (((t[0..7] * err) >> 16) + 1) >> 1
      *ww.m128 = _mm_adds_epi16(*ww.m128, var);                    // w[0..7] + ((((t[0..7] * err) >> 16) + 1) >> 1)
    }
#else
    const auto* __restrict tt{t};
    auto* __restrict ww{w};
    for (auto n{N_LAYERS}; n--;) {
      const int32_t wt{*ww + ((((*tt++ * err_) >> 16) + 1) >> 1)};
      *ww++ = static_cast<int16_t>(std::clamp(wt, SHRT_MIN, SHRT_MAX));
    }
#endif
  }

  [[nodiscard]] auto dot_product(const int16_t* const __restrict t, const int16_t* const __restrict w) const noexcept -> int32_t {
#if defined(ENABLE_INTRINSICS) && defined(__SSE2__)
    const Exchange_t tt{t};
    const Exchange_t ww{w};
    if constexpr (4 == N_LAYERS) {
      auto dp{_mm_madd_pi16(*tt.m64, *ww.m64)};       // (t[0] * w[0]) + ... + (t[3] * w[3])
      dp = _mm_add_pi32(dp, _mm_srli_si64(dp, 32));   // Add sums together
      const auto sum{_mm_cvtsi64_si32(dp)};           // Scale back to integer
      _mm_empty();                                    //
      return sum;                                     //
    } else {                                          //
      auto dp{_mm_madd_epi16(*tt.m128, *ww.m128)};    // (t[0] * w[0]) + ... + (t[7] * w[7])
      dp = _mm_add_epi32(dp, _mm_srli_si128(dp, 8));  // Add sums together
      dp = _mm_add_epi32(dp, _mm_srli_si128(dp, 4));  //
      const auto sum{_mm_cvtsi128_si32(dp)};          // Scale back to integer
      return sum;                                     //
    }
#else
    int32_t sum{0};
    const auto* __restrict tt{t};
    const auto* __restrict ww{w};
    for (auto n{N_LAYERS}; n--;) {
      assert((*tt >= -2048) && (*tt < 2048));
      sum += *ww++ * *tt++;
    }
    return sum;
#endif
  }

  const uint32_t _mask;                       // n-1
  uint32_t _ctx{0};                           // Context of last prediction
  int16_t* const __restrict _weights;         // Weights
  int32_t : 32;                               // Padding
  int32_t : 32;                               // Padding
  alignas(16) std::array<int16_t, 16> _pi{};  // Prediction inputs
  int16_t* __restrict _new{&_pi[0]};          // New Inputs (alternating between _pi[0] and _pi[8])
  int16_t* __restrict _prv{&_pi[8]};          // Previous Inputs (alternating between _pi[8] and _pi[0])
};

/**
 * @class HashTable_t
 * @brief Hash table handling
 *
 * A hash table maps a 32-bit index to an array of 4 bytes (Elements_t).
 * The first byte is a checksum that uses the top 8 bits of the index.
 * The index does not have to be a hash.
 */
class HashTable_t final {
public:
  explicit HashTable_t(const uint64_t max_size) noexcept
      : N{(max_size > MEM_LIMIT) ? MEM_LIMIT : max_size},  //
        _hashtable{static_cast<Elements_t*>(calloc(N, sizeof(uint8_t)))},
        _mask{static_cast<uint32_t>((N / UINT64_C(4)) - 1)} {  // 4 is search limit
    assert(ISPOWEROF2(N));
    assert(_hashtable);
    if (verbose_) {
      fprintf(stdout, "%s for HashTable_t\n", GetDimension(N).c_str());
    }
  }

  virtual ~HashTable_t() noexcept;

  HashTable_t(const HashTable_t&) = delete;
  HashTable_t(HashTable_t&&) = delete;
  auto operator=(const HashTable_t&) -> HashTable_t& = delete;
  auto operator=(HashTable_t&&) -> HashTable_t& = delete;

  // o --> 0,1,2,3,4,5,6,7
  [[nodiscard]] auto get1x(const uint32_t o, const uint32_t i) const noexcept -> uint8_t* {
    const auto chk{static_cast<uint8_t>(o | (i >> 27))};  // 3 + 5 bits
    const auto idx{i & _mask};

    // +-----+-----+-----+-----+
    // | chk | val | val | val | -1 (second, q)
    // +-----+-----+-----+-----+
    // | chk | val | val | val |  0 (first, p)
    // +-----+-----+-----+-----+
    // | chk | val | val | val | +1 (second, q)
    // +-----+-----+-----+-----+

    auto* __restrict p{&_hashtable[idx]};  // first
    if (chk == p->checksum) {              // idx + 0 ?
      return p->count.data();
    }
    auto* const __restrict q{&_hashtable[idx ^ 1]};  // second
    if (chk == q->checksum) {                        // idx +/- 1 ?
      return q->count.data();
    }

    // clang-format off
    if (p->count[0] > q->count[0]) { p = q; }
    // clang-format on

    *p = Elements_t{.checksum = chk, .count{{0, 0, 0}}};
    return p->count.data();
  }

  // o --> 0,3,4,7
  [[nodiscard]] auto get3a(const uint32_t o, const uint32_t i) const noexcept -> uint8_t* {
    const auto chk{static_cast<uint8_t>(o | (i >> 27))};  // 3 + 5 bits
    const auto idx{i & _mask};

    // +-----+-----+-----+-----+
    // | chk | val | val | val | -3 (second, q)
    // +-----+-----+-----+-----+
    // | chk | val | val | val | -2 (third, r)
    // +-----+-----+-----+-----+
    // | chk | val | val | val | -1 (fourth, s)
    // +-----+-----+-----+-----+
    // | chk | val | val | val |  0 (first, p)
    // +-----+-----+-----+-----+
    // | chk | val | val | val | +1 (fourth, s)
    // +-----+-----+-----+-----+
    // | chk | val | val | val | +2 (third, r)
    // +-----+-----+-----+-----+
    // | chk | val | val | val | +3 (second, q)
    // +-----+-----+-----+-----+

    auto* __restrict p{&_hashtable[idx]};  // first
    if (chk == p->checksum) {              // idx + 0 ?
      return p->count.data();
    }
    auto* const __restrict q{&_hashtable[idx ^ 3]};  // second
    if (chk == q->checksum) {                        // idx +/- 3 ?
      return q->count.data();
    }
    auto* const __restrict r{&_hashtable[idx ^ 2]};  // third
    if (chk == r->checksum) {                        // idx +/- 2 ?
      return r->count.data();
    }
    auto* const __restrict s{&_hashtable[idx ^ 1]};  // fourth
    if (chk == s->checksum) {                        // idx +/- 1 ?
      return s->count.data();
    }

    // clang-format off
    if (p->count[0] > q->count[0]) { p = q; }
    if (p->count[0] > r->count[0]) { p = r; }
    if (p->count[0] > s->count[0]) { p = s; }
    // clang-format on

    *p = Elements_t{.checksum = chk, .count{{0, 0, 0}}};
    return p->count.data();
  }

  // o --> 1,2,5,6
  [[nodiscard]] auto get3b(const uint32_t o, const uint32_t i) const noexcept -> uint8_t* {
    const auto chk{static_cast<uint8_t>(o | (i >> 27))};  // 3 + 5 bits
    const auto idx{i & _mask};

    // +-----+-----+-----+-----+
    // | chk | val | val | val | -3 (third, r)
    // +-----+-----+-----+-----+
    // | chk | val | val | val | -2 (second, q)
    // +-----+-----+-----+-----+
    // | chk | val | val | val | -1 (fourth, s)
    // +-----+-----+-----+-----+
    // | chk | val | val | val |  0 (first, p)
    // +-----+-----+-----+-----+
    // | chk | val | val | val | +1 (fourth, s)
    // +-----+-----+-----+-----+
    // | chk | val | val | val | +2 (second, q)
    // +-----+-----+-----+-----+
    // | chk | val | val | val | +3 (third, r)
    // +-----+-----+-----+-----+

    auto* __restrict p{&_hashtable[idx]};  // first
    if (chk == p->checksum) {              // idx + 0 ?
      return p->count.data();
    }
    auto* const __restrict q{&_hashtable[idx ^ 2]};  // second
    if (chk == q->checksum) {                        // idx +/- 2 ?
      return q->count.data();
    }
    auto* const __restrict r{&_hashtable[idx ^ 3]};  // third
    if (chk == r->checksum) {                        // idx +/- 3 ?
      return r->count.data();
    }
    auto* const __restrict s{&_hashtable[idx ^ 1]};  // fourth
    if (chk == s->checksum) {                        // idx +/- 1 ?
      return s->count.data();
    }

    // clang-format off
    if (p->count[0] > q->count[0]) { p = q; }
    if (p->count[0] > r->count[0]) { p = r; }
    if (p->count[0] > s->count[0]) { p = s; }
    // clang-format on

    *p = Elements_t{.checksum = chk, .count{{0, 0, 0}}};
    return p->count.data();
  }

private:
  static constexpr auto MEM_LIMIT{UINT64_C(0x400000000)};  // 16 GiB

  struct Elements_t final {
    uint8_t checksum;
    std::array<uint8_t, 3> count;
  };
  static_assert(0 == offsetof(Elements_t, checksum), "Alignment failure in HashTable_t::Elements_t");
  static_assert(1 == offsetof(Elements_t, count), "Alignment failure in HashTable_t::Elements_t");
  static_assert(4 == sizeof(Elements_t), "Alignment failure in HashTable_t::Elements_t");

  const uint64_t N;
  Elements_t* const __restrict _hashtable;
  const uint32_t _mask;
  int32_t : 32;  // Padding
};
HashTable_t::~HashTable_t() noexcept {
  std::free(_hashtable);
}

namespace {
  std::array<uint8_t* __restrict, 5> cp_{{nullptr, nullptr, nullptr, nullptr, nullptr}};

  // Some more arbitrary magic (prime) numbers
  constexpr auto MUL64_01{UINT64_C(0x993DDEFFB1462949)};
  constexpr auto MUL64_02{UINT64_C(0xE9C91DC159AB0D2D)};
  // constexpr auto MUL64_03{UINT64_C(0x83D6A14F1B0CED73)};
  // constexpr auto MUL64_04{UINT64_C(0xA14F1B0CED5A841F)};
  // constexpr auto MUL64_05{UINT64_C(0xC0E51314A614F4EF)};
  // constexpr auto MUL64_06{UINT64_C(0xDA9CC2600AE45A27)};
  // constexpr auto MUL64_07{UINT64_C(0x826797AA04A65737)};
  // constexpr auto MUL64_08{UINT64_C(0x2375BE54C41A08ED)};
  // constexpr auto MUL64_09{UINT64_C(0xD39104E950564B37)};
  // constexpr auto MUL64_10{UINT64_C(0x3091697D5E685623)};
  // constexpr auto MUL64_11{UINT64_C(0x20EB84EE04A3C7E1)};
  // constexpr auto MUL64_12{UINT64_C(0xF501F1D0944B2383)};
  // constexpr auto MUL64_13{UINT64_C(0xE3E4E8AA829AB9B5)};

  [[nodiscard]] ALWAYS_INLINE constexpr auto Hash(const uint64_t x0) noexcept -> uint64_t {
    const uint64_t ctx{(x0 + 1) * Utilities::PHI64};
    return (ctx << 32) | (ctx >> 32);
  }

  [[nodiscard]] ALWAYS_INLINE constexpr auto Hash(const uint64_t x0, const uint64_t x1) noexcept -> uint64_t {
    const uint64_t ctx{Hash(x0) + ((x1 + 1) * MUL64_01)};
    return (ctx << 32) | (ctx >> 32);
  }

  [[nodiscard]] ALWAYS_INLINE constexpr auto Hash(const uint64_t x0, const uint64_t x1, const uint64_t x2) noexcept -> uint64_t {
    const uint64_t ctx{Hash(x0, x1) + ((x2 + 1) * MUL64_02)};
    return (ctx << 32) | (ctx >> 32);
  }

  [[nodiscard]] ALWAYS_INLINE constexpr auto Combine64(const uint64_t seed, const uint64_t x) noexcept -> uint64_t {
    const uint64_t ctx{(seed + x) * Utilities::PHI64};
    return (ctx << 32) | (ctx >> 32);
  }

  [[nodiscard]] ALWAYS_INLINE constexpr auto Finalise64(const uint64_t hash, const uint32_t hashbits) noexcept -> uint32_t {
    assert(hashbits <= 64);
    return static_cast<uint32_t>(hash >> (64 - hashbits));
  }
};  // namespace

/**
 * @class StateMap_t
 * @brief State to prediction handling
 *
 * State to prediction handling
 * @tparam SIZE Size of state table. It will allocate 3*256*SIZE bytes of memory.
 */
template <const uint32_t SIZE>
class StateMap_t final {
  static_assert(ISPOWEROF2(SIZE), "Size of state map must a power of two");

public:
  StateMap_t() noexcept {
    _smt.fill(0x7FFF);  // No prediction
    if (verbose_) {
      fprintf(stdout, "%s for StateMap_t\n", GetDimension(_smt.size() * sizeof(_smt[0])).c_str());
    }
  }
  virtual ~StateMap_t() noexcept = default;

  StateMap_t(const StateMap_t&) = delete;
  StateMap_t(StateMap_t&&) = delete;
  auto operator=(const StateMap_t&) -> StateMap_t& = delete;
  auto operator=(StateMap_t&&) -> StateMap_t& = delete;

  [[nodiscard]] constexpr auto operator[](const uint32_t i) noexcept -> uint16_t& {
    return _smt[i];
  }

  [[nodiscard]] auto Update(const bool bit, const uint32_t context, const int32_t rate) noexcept -> int32_t {
    uint16_t& balz{_smt[_ctx]};
    balz = static_cast<uint16_t>(bit ? (balz + (static_cast<uint16_t>(~balz) >> rate)) : (balz - (balz >> rate)));
    _ctx = context & (SIZE - 1);
    return Stretch(_smt[_ctx] / 16);  // Conversion from 0..4095 into -2048..2047
  }

private:
  uint32_t _ctx{0};  // Context of last prediction
  int32_t : 32;      // Padding
  alignas(16) std::array<uint16_t, SIZE> _smt{};
};

/**
 * @class ContextMap_t
 * @brief Context to prediction handling
 *
 * Context to prediction handling
 * @tparam SIZE Size of context map
 * @tparam RATE0 Speed(s) of context map, high value is slow, low value is fast
 * @tparam RATE1 Speed(s) of context map, high value is slow, low value is fast
 * @tparam RATE2 Speed(s) of context map, high value is slow, low value is fast
 */
template <const uint32_t SIZE, const int32_t RATE0, const int32_t RATE1, const int32_t RATE2 = 0>
class ContextMap_t final {
  static_assert(ISPOWEROF2(SIZE), "Size of context map must a power of two");
  static_assert(RATE0 > 0, "Speed(s) of context map must be positive");
  static_assert(RATE1 > 0, "Speed(s) of context map must be positive");
  static_assert(RATE2 >= 0, "Speed(s) of context map must be positive (0 is switch off)");

public:
  explicit ContextMap_t() noexcept = default;
  virtual ~ContextMap_t() noexcept = default;

  ContextMap_t(const ContextMap_t&) = delete;
  ContextMap_t(ContextMap_t&&) = delete;
  auto operator=(const ContextMap_t&) -> ContextMap_t& = delete;
  auto operator=(ContextMap_t&&) -> ContextMap_t& = delete;

  void Set(const uint32_t ctx) noexcept {
    _ctx_new = ctx << 8;
  }

  auto Predict(const bool bit) noexcept -> std::tuple<int16_t, int16_t, int16_t> {
    const auto& state_table{bit ? state_table_y1_ : state_table_y0_};

    uint8_t* const __restrict st{&_state[_ctx_last_prediction][0]};
    st[0] = state_table[0][st[0]];
    st[1] = state_table[1][st[1]];
    st[2] = state_table[2][st[2]];

    const auto ctx{(7 == bcount_) ? static_cast<uint32_t>(0xFF & cx_) : c0_};
    _ctx_last_prediction = (_ctx_new | ctx) & _mask;

    if constexpr (0 == RATE2) {
      const auto p0{_sm0.Update(bit, _state[_ctx_last_prediction][0], RATE0)};
      const auto p1{_sm1.Update(bit, _state[_ctx_last_prediction][1], RATE1)};
      return {p0, p1, 0};
    }

    const auto p0{_sm0.Update(bit, _state[_ctx_last_prediction][0], RATE0)};
    const auto p1{_sm1.Update(bit, _state[_ctx_last_prediction][1], RATE1)};
    const auto p2{_sm2.Update(bit, _state[_ctx_last_prediction][2], RATE2)};
    return {p0, p1, p2};
  }

private:
  std::array<std::array<uint8_t, 3>, (SIZE * 256)> _state{};
  int32_t : 32;              // Padding
  int32_t : 32;              // Padding
  StateMap_t<0x100> _sm0{};  // State to prediction
  StateMap_t<0x100> _sm1{};  // State to prediction
  StateMap_t<0x100> _sm2{};  // State to prediction

  const uint32_t _mask{(SIZE * 256) - 1};
  uint32_t _ctx_new{0};
  uint32_t _ctx_last_prediction{0};
  int32_t : 32;  // Padding
};

/**
 * @class HashMap_t
 * @brief Hash map handling
 *
 * HashMap_t maps a 32 bit hash to an array of 4 bytes (checksum and two byte values)
 *
 * HashMap_t{N}; creates N elements table with 4 bytes each.
 *   N must be a power of 2.
 *   The first 16 bits of each element is reserved for a checksum to detect collisions.
 *   The remaining bytes are values, prioritised by the first value (named count).
 *   This byte is 0 to mark an unused element.
 */
class HashMap_t final {
public:
  explicit HashMap_t(const uint32_t elements) noexcept
      : _hashmap{static_cast<Elements_t*>(calloc(elements + M, sizeof(Elements_t)))},  //
        _mask{elements - 1} {
    assert(ISPOWEROF2(elements));
    assert(_hashmap);
  }

  virtual ~HashMap_t() noexcept;

  HashMap_t(const HashMap_t&) = delete;
  HashMap_t(HashMap_t&&) = delete;
  auto operator=(const HashMap_t&) -> HashMap_t& = delete;
  auto operator=(HashMap_t&&) -> HashMap_t& = delete;

  /**
   * @struct Node_t
   * @brief A node in the hash table containing the number of counts and value
   *
   * A node in the hash table containing the number of counts and value
   */
  struct Node_t final {
    uint8_t count;
    uint8_t value;
  };
  static_assert(0 == offsetof(Node_t, count), "Alignment failure in Node_t");
  static_assert(1 == offsetof(Node_t, value), "Alignment failure in Node_t");
  static_assert(2 == sizeof(Node_t), "Alignment failure in Node_t");

  /**
   * Returns a pointer to the i'th element,
   * such that bh[i].chk is a checksum of i,
   * bh[i].count used as priority and bh[i].value.
   * If a collision is detected,
   * up to M nearby locations in the same cache line are tested and
   * the first matching checksum or empty element is returned.
   * If no match or empty element is found,
   * then the lowest priority element is replaced.
   *
   * @param i Seek for the i'th element
   * @return Reference to count/value location (as byte)
   */
  [[nodiscard]] auto operator[](const uint32_t i) noexcept -> Node_t* {
    const auto checksum{static_cast<uint16_t>((i >> 16) ^ i)};
    const auto index{(i * M) & _mask};
    Elements_t* const __restrict front{&_hashmap[index]};
    Elements_t* __restrict slot;
    uint32_t offset{0};
    do {
      slot = &front[offset];
      if (0 == slot->node.count) {  // Priority/State byte is zero --> empty slot
        slot->checksum = checksum;  // Occupy
        break;
      }
      if (slot->checksum == checksum) {
        break;  // Element found!
      }
    } while (++offset < M);
    if (0 == offset) {
      return &slot->node;  // Already at the front --> nothing to do
    }
    Elements_t store;
    if (offset == M) {  // Element was not found
      store = Elements_t{.checksum = checksum, .node{.count = 0, .value = 0}};
      // Candidate to overwrite is the last one (M - 1) or the one before that (M - 2)
      offset = (front[M - 1].node.count > front[M - 2].node.count) ? (M - 2) : (M - 1);
    } else {  // Element was found or empty slot occupied
      store = *slot;
    }
#if 0  // Move to front
    memmove(&front[1], front, offset * sizeof(Elements_t));
#else
    auto* __restrict src{front + offset - 1};
    auto* __restrict dst{front + offset};
    for (auto n{offset}; n--;) {
      *dst-- = *src--;
    }
#endif
    *front = store;
    return &front->node;
  }

private:
  static constexpr auto M{UINT32_C(7)};  // search limit

  /**
   * @struct Elements_t
   * The elements in the hash table containing checksum and node
   */
  struct Elements_t final {
    uint16_t checksum;
    Node_t node;
  };
  static_assert(0 == offsetof(Elements_t, checksum), "Alignment failure in HashMap_t::Elements_t");
  static_assert(2 == offsetof(Elements_t, node), "Alignment failure in HashMap_t::Elements_t");
  static_assert(4 == sizeof(Elements_t), "Alignment failure in HashMap_t::Elements_t");

  Elements_t* const __restrict _hashmap;
  const uint32_t _mask;
  int32_t : 32;  // Padding
};
HashMap_t::~HashMap_t() noexcept {
  std::free(_hashmap);
}

/**
 * @class RunContextMap_t
 * @brief Run context map handling
 *
 * Run context map handling
 */
class RunContextMap_t final {
public:
  explicit RunContextMap_t(const int32_t max_size, const int32_t scale) noexcept
      : _hashmap{UINT32_C(1) << max_size},  //
        _cp{_hashmap[0]} {
    uint32_t x{14155776};
    for (uint32_t i{2}; i <= ilog.size(); ++i) {
      x += 774541002 / (i * 2 - 1);  // numerator is 2^29/ln 2
      ilog[i - 1] = clamp12(static_cast<int32_t>(x >> 24) * scale);
    }
  }
  virtual ~RunContextMap_t() noexcept;

  RunContextMap_t() = delete;
  RunContextMap_t(const RunContextMap_t&) = delete;
  RunContextMap_t(RunContextMap_t&&) = delete;
  auto operator=(const RunContextMap_t&) -> RunContextMap_t& = delete;
  auto operator=(RunContextMap_t&&) -> RunContextMap_t& = delete;

  void Set(const uint32_t context) noexcept {  // update count
    const auto expected_byte{static_cast<uint8_t>(cx_)};
    if ((0 == _cp->count) || (expected_byte != _cp->value)) {
      *_cp = HashMap_t::Node_t{.count = 1, .value = expected_byte};  // Reset count, set expected byte
    } else if (_cp->count < 255) {
      ++_cp->count;
    }
    _cp = _hashmap[context];
  }

  [[nodiscard]] auto Predict() noexcept -> int16_t {  // predict next bit
    const uint8_t expected_byte{_cp->value};
    if ((expected_byte | 0x100u) >> (1 + bcount_) == c0_) {
      const int32_t expected_bit{1 & (expected_byte >> bcount_)};
      const auto prediction{((expected_bit * 2) - 1) * ilog[_cp->count]};
      return static_cast<int16_t>(prediction);
    }
    return 0;  // No or wrong prediction
  }

private:
  std::array<int32_t, 0x100> ilog{};  // clamp12(round(log2(x)*16)*scale)
  HashMap_t _hashmap;
  HashMap_t::Node_t* __restrict _cp;
};
RunContextMap_t::~RunContextMap_t() noexcept = default;

/**
 * @class DynamicMarkovModel_t
 * @brief Handling the dynamic Markov model
 *
 * Handling the dynamic Markov model
 */
class DynamicMarkovModel_t final {
public:
  explicit DynamicMarkovModel_t(const uint64_t max_size) noexcept
      : _max_size_bytes{(max_size > MEM_LIMIT) ? MEM_LIMIT : max_size},
        _max_nodes{static_cast<uint32_t>((_max_size_bytes / sizeof(Node)) - 1)},
        _nodes{reinterpret_cast<Node*>(std::calloc(_max_size_bytes + sizeof(Node), sizeof(int8_t)))} {
    assert(0 == (_max_nodes >> 28));  // the top 4 bits must be unused by nx0 and nx1 for storing the 4+4 bits of the bit history state byte
    assert(_max_nodes >= 65280);
    if (verbose_) {
      fprintf(stdout, "%s for DynamicMarkovModel_t\n", GetDimension(_max_size_bytes + sizeof(Node)).c_str());
    }
    Flush();
  }
  ~DynamicMarkovModel_t() noexcept;
  DynamicMarkovModel_t() = delete;
  DynamicMarkovModel_t(const DynamicMarkovModel_t&) = delete;
  DynamicMarkovModel_t(DynamicMarkovModel_t&&) = delete;
  auto operator=(const DynamicMarkovModel_t&) -> DynamicMarkovModel_t& = delete;
  auto operator=(DynamicMarkovModel_t&&) -> DynamicMarkovModel_t& = delete;

  void Update() noexcept {
    _cm.Set(tt_);
  }

  void Predict(const bool bit) noexcept {
    Node& curr{_nodes[_curr]};

    const uint32_t n{bit ? curr.count1 : curr.count0};

    // Update count and state
    auto AdaptivelyIncrement{[](const uint32_t count) noexcept -> uint32_t {  //
      return ((count << 6) - count) >> 6;
    }};

    if (bit) {
      curr.count0 = static_cast<uint16_t>(AdaptivelyIncrement(curr.count0));
      curr.count1 = static_cast<uint16_t>(AdaptivelyIncrement(curr.count1) + 1024);

      curr.state = state_table_y1_[0][curr.state];
    } else {
      curr.count0 = static_cast<uint16_t>(AdaptivelyIncrement(curr.count0) + 1024);
      curr.count1 = static_cast<uint16_t>(AdaptivelyIncrement(curr.count1));

      curr.state = state_table_y0_[0][curr.state];
    }

    if (n > _threshold) {
      const uint32_t next{bit ? curr.nx1 : curr.nx0};
      uint32_t n0{_nodes[next].count0};
      uint32_t n1{_nodes[next].count1};

      if (const uint32_t nn{n0 + n1}; nn > (n + _threshold)) {
        _nodes[_top].nx0 = _nodes[next].nx0;
        _nodes[_top].nx1 = _nodes[next].nx1;
        _nodes[_top].state = _nodes[next].state;

        if ((n + n) == nn) {  // (n + n) == (n0 + n1) --> (n * n0) / (n0 + n1) --> 1/2
          n0 /= 2;
          n1 /= 2;

          _nodes[_top].count0 = static_cast<uint16_t>(n0);
          _nodes[_top].count1 = static_cast<uint16_t>(n1);
        } else {
          auto RescaleCount0{[&]() noexcept -> uint32_t {
            if (n0) {
              const auto rescale{(n0 * n) / nn};
              assert(n0 >= rescale);
              n0 -= rescale;
              return rescale;
            }
            return 0;
          }};

          auto RescaleCount1{[&]() noexcept -> uint32_t {
            if (n1) {
              const auto rescale{(n1 * n) / nn};
              assert(n1 >= rescale);
              n1 -= rescale;
              return rescale;
            }
            return 0;
          }};

          _nodes[_top].count0 = static_cast<uint16_t>(RescaleCount0());
          _nodes[_top].count1 = static_cast<uint16_t>(RescaleCount1());
        }

        assert(n0 <= USHRT_MAX);
        assert(n1 <= USHRT_MAX);
        assert((n0 + n1) > 0);

        _nodes[next].count0 = static_cast<uint16_t>(n0);
        _nodes[next].count1 = static_cast<uint16_t>(n1);

        // Node has been cloned, potentially unused
        _nodes[next].state = 0;

        if (bit) {
          curr.nx1 = MASK_28_BITS & _top;
        } else {
          curr.nx0 = MASK_28_BITS & _top;
        }

        ++_top;
        if (_top > _max_nodes) {
          Flush();
        }

        if (_threshold < (10 * THRESHOLD)) {  // Max threshold of 10 is based on enwik9
          _threshold = ++_threshold_fine >> THRESHOLD_SPEED;
        }
      }
    }

    _curr = bit ? curr.nx1 : curr.nx0;

    auto& pr{_blend.Get()};
    pr[0] = static_cast<int16_t>(Predict());                                 // DMC prediction -2048..2047
    pr[1] = static_cast<int16_t>(_sm2.Update(bit, _nodes[_curr].state, 5));  // Rate of 5 is based on enwik9

    // Little improvements of DMC predictions
    pr[2] = static_cast<int16_t>(_sm3.Update(bit, (tt_ << 8) | c0_, 1));                    // Rate of 1 is based on enwik9
    pr[3] = static_cast<int16_t>(_sm4.Update(bit, (Finalise64(word_, 32) << 8) | c0_, 1));  // Rate of 1 is based on enwik9
    pr[4] = static_cast<int16_t>(_sm5.Update(bit, (x5_ << 8) | c0_, 2));                    // Rate of 2 is based on enwik9

    const auto [p5, p6, p7]{_cm.Predict(bit)};
    pr[5] = p5;
    pr[6] = p6;
    pr[7] = p7;

    const auto last_pr{Squash(Mixer_t::tx_[7])};  // Conversion from -2048..2047 (clamped) into 0..4095
    const auto ctx{(w5_ << 3) | bcount_};
    const int32_t err{((bit << 12) - static_cast<int32_t>(last_pr)) * 10};  // Scale of 10 is based on enwik9
    const auto px{_blend.Predict(err, ctx)};
    Mixer_t::tx_[7] = px;
  }

private:
  [[nodiscard]] auto Predict() const noexcept -> int32_t {
    const uint32_t n0{_nodes[_curr].count0};
    const uint32_t n1{_nodes[_curr].count1};

    // clang-format off
    if (n0 == n1) { return  0x000; } // no prediction
    if ( 0 == n0) { return  0x7FF; } // one
    if ( 0 == n1) { return ~0x7FF; } // zero
    // clang-format on

    const uint32_t pr{(0xFFFu * n1) / (n0 + n1)};
    return Stretch(pr);  // Conversion from 0..4095 into -2048..2047
  }

#pragma pack(push, 1)
  struct Node {
    uint32_t nx0 : 28;
    uint32_t nx1 : 28;
    uint32_t state : 8;
    uint16_t count0;
    uint16_t count1;
  };
#pragma pack(pop)
#if !defined(CLANG_TIDY)
  static_assert(8 == offsetof(Node, count0), "Alignment failure in DMC node");
  static_assert(10 == offsetof(Node, count1), "Alignment failure in DMC node");
  static_assert(12 == sizeof(Node), "Alignment failure in DMC node");
#endif // CLANG_TIDY

  static constexpr auto MEM_LIMIT{(UINT64_C(1) << 28) * sizeof(Node)};  // 3 GiB
  static constexpr auto MASK_28_BITS{(UINT32_C(1) << 28) - 1};

  static constexpr uint32_t INIT_COUNT{486};      // Initial value of counter
  static constexpr uint32_t THRESHOLD{1576};      // Threshold of when to clone
  static constexpr uint32_t THRESHOLD_SPEED{11};  //

  void Flush() noexcept {
    _threshold = THRESHOLD;
    _threshold_fine = THRESHOLD << THRESHOLD_SPEED;
    _top = 0;
    _curr = 0;
    for (uint32_t n{0}; n < _max_nodes; ++n) {
      _nodes[n].state = 0;
    }
#if 1
    for (uint32_t j{0}; j < 256; ++j) {                                // 256 trees
      for (uint32_t i{0}; i < 255; ++i) {                              // 255 nodes in each tree
        if (i < 127) {                                                 // Internal tree nodes
          _nodes[_top].nx0 = MASK_28_BITS & (_top + i + 1);            // Left node
          _nodes[_top].nx1 = MASK_28_BITS & (_top + i + 2);            // Right node
        } else {                                                       // 128 leaf nodes - they each references a root node of tree(i)
          const uint32_t linked_tree_root{(i - 127) * 2 * 255};        //
          _nodes[_top].nx0 = MASK_28_BITS & linked_tree_root;          // Left node -> root of tree 0,2,4,...
          _nodes[_top].nx1 = MASK_28_BITS & (linked_tree_root + 255);  // Right node -> root of tree 1,3,5,...
        }
        _nodes[_top].count0 = INIT_COUNT;
        _nodes[_top].count1 = INIT_COUNT;
        _top++;
      }
    }
#else
    // The Braid construction
    static constexpr auto NBITS{8u};  // Number of hits per byte
    static constexpr auto STRANDS{1u << NBITS};
    for (uint32_t i{0}; i < NBITS; ++i) {
      for (uint32_t j{0}; j < STRANDS; ++j) {
        const auto state{i + NBITS * j};
        const auto k{(i + 1) % NBITS};
        _nodes[state].nx0 = MASK_28_BITS & ((k + (2 * j) % STRANDS) * NBITS);
        _nodes[state].nx1 = MASK_28_BITS & ((k + (2 * j + 1) % STRANDS) * NBITS);
        _nodes[state].count0 = INIT_COUNT;
        _nodes[state].count1 = INIT_COUNT;
      }
    }
    _top = NBITS * STRANDS - 1;
#endif
  }

  const uint64_t _max_size_bytes;
  const uint32_t _max_nodes;
  uint32_t _top{0};
  Node* const __restrict _nodes;
  uint32_t _curr{0};
  uint32_t _threshold{THRESHOLD};
  uint32_t _threshold_fine{THRESHOLD << THRESHOLD_SPEED};
  int32_t : 32;                               // Padding
  int32_t : 32;                               // Padding
  int32_t : 32;                               // Padding
  StateMap_t<0x100> _sm2{};                   // state
  StateMap_t<0x4000> _sm3{};                  // tt_     | not part of model, just an improvement
  StateMap_t<0x10000> _sm4{};                 // word_   | not part of model, just an improvement
  StateMap_t<0x40000> _sm5{};                 // x5_     | not part of model, just an improvement
  ContextMap_t<0x4000, 0xE, 0xD, 0x7> _cm{};  // tt_|c0_ | Rates of 14/13/ 7 are based on enwik9 | not part of model, just an improvement
  Blend_t<8> _blend{UINT32_C(1) << 19, 512};  // w5_
};
DynamicMarkovModel_t::~DynamicMarkovModel_t() noexcept {
  std::free(_nodes);
}

/**
 * @class LempelZivPredict_t
 * @brief Lempel-Ziv Prediction
 *
 * Handling Lempel-Ziv or match model predictions
 */
class LempelZivPredict_t final {  // MatchModel
public:
  explicit LempelZivPredict_t(const Buffer_t& __restrict buf, const uint64_t max_size) noexcept
      : _buf{buf},  //
        _hashbits{CountBits(((max_size > MEM_LIMIT) ? MEM_LIMIT : max_size) - UINT64_C(1))},
        _ht{static_cast<uint32_t*>(std::calloc((UINT64_C(1) << _hashbits) + UINT64_C(1), sizeof(uint32_t)))} {
    assert(ISPOWEROF2(max_size));
    if (verbose_) {
      fprintf(stdout, "%s for LempelZivPredict_t\n", GetDimension(((UINT64_C(1) << _hashbits) + UINT64_C(1)) * sizeof(uint32_t)).c_str());
    }
  }

  virtual ~LempelZivPredict_t() noexcept;

  LempelZivPredict_t() = delete;
  LempelZivPredict_t(const LempelZivPredict_t&) = delete;
  LempelZivPredict_t(LempelZivPredict_t&&) = delete;
  auto operator=(const LempelZivPredict_t&) -> LempelZivPredict_t& = delete;
  auto operator=(LempelZivPredict_t&&) -> LempelZivPredict_t& = delete;

  void Update() noexcept {
    uint64_t h{1};
    for (auto n{MINLEN + 2}; n;) {
      h = Combine64(h, _buf(n--));
    }
    const auto idx{Finalise64(h, _hashbits)};

    if (_match_length >= MINLEN) {
      _match_length += _match_length < MAXLEN;
      ++_match;
    } else {
      _match_length = 0;
      _match = _ht[idx];
      if (_match) {
        while ((_match_length < MAXLEN) && (_buf(_match_length + 1) == _buf[_match - _match_length - 1])) {
          ++_match_length;
        }
      }
    }
    _ht[idx] = _buf.Pos();

    _expected_byte = _buf[_match];

    _rc0.Set((_match_length << 8) | c1_);  // 6+8 bits
    _rc1.Set(w5_);
    _rc2.Set(x5_);
    _rc3.Set(tt_);
    _rc4.Set(Finalise64(word_, 32));
  }

  [[nodiscard]] auto Predict(const bool bit) noexcept -> uint32_t {
    auto& pr{_blend.Get()};

    uint32_t ctx0{0};
    uint32_t order;

    if ((_match_length >= MINLEN) && (((_expected_byte | 0x100) >> (1 + bcount_)) == c0_)) {
      const auto expected_bit{UINT32_C(1) & (_expected_byte >> bcount_)};

      const auto sign{static_cast<int32_t>(2 * expected_bit) - 1};
      const auto length_to_prediction{sign * static_cast<int32_t>(_match_length) * 32};
      pr[0] = static_cast<int16_t>(clamp12(length_to_prediction));

      const auto length{_match_length - MINLEN};
      if (length > 0) {
        if (length <= 16) {
          ctx0 = (2 * (length - 1)) + expected_bit;  // 0..31
        } else {
          ctx0 = 22 + (2 * ((length - 1) / 3)) + expected_bit;  // 32..63
        }
      }

      const auto ctx1{(length << 9) | (expected_bit << 8) | c1_};  // 6+1+8=15 bits
      pr[1] = static_cast<int16_t>(_ltp0.Update(bit, ctx1, 8));    // Rate of 8 is based on enwik9

      // Length to order, based on enwik9 (value must start with 9 and end with 4)
      const auto l2o{(7 == bcount_) ? UINT64_C(0x9999988888776654) : UINT64_C(0x9999998888776654)};
      order = static_cast<uint32_t>(0xF & (l2o >> (4 * (length / 4))));
    } else {
      _match_length = 0;  // Wrong prediction, reset!
      pr[0] = 0;
      pr[1] = static_cast<int16_t>(_ltp0.Update(bit, c0_, 2) / 2);  // Rate of 2 is based on enwik9

      order = 0;
      if (*cp_[1]) {
        order = 1;
        if (*cp_[2]) {
          order = 2;
          if (*cp_[3]) {
            order = 3;
          }
        }
      }
    }

    const auto py{static_cast<int16_t>(_ltp1.Update(bit, (ctx0 << 8) | c0_, 4))};  // 6+8=14 bits | Rate of 4 is based on enwik9
    pr[2] = ctx0 ? py : 0;
    pr[3] = _rc0.Predict();
    pr[4] = _rc1.Predict();
    pr[5] = _rc2.Predict();
    pr[6] = _rc3.Predict();
    pr[7] = _rc4.Predict();

    const auto last_pr{Squash(Mixer_t::tx_[0])};  // Conversion from -2048..2047 (clamped) into 0..4095
    const auto ctx{(w5_ << 3) | bcount_};
    const int32_t err{((bit << 12) - static_cast<int32_t>(last_pr)) * 11};  // Scale of 11 is based on enwik9
    const auto px{_blend.Predict(err, ctx)};
    Mixer_t::tx_[0] = px;

    return order;
  }

private:
  static constexpr uint32_t MINLEN{7};                     // Minimum required match length
  static constexpr uint32_t MAXLEN{MINLEN + 63};           // Longest allowed match (max 6 bits, after subtraction of minimum length)
  static constexpr auto MEM_LIMIT{UINT64_C(0x100000000)};  // 4 GiB

  [[nodiscard]] auto CountBits(uint64_t x) const noexcept -> uint32_t {
    uint32_t n{0};
    while (x) {
      ++n;
      x >>= 1;
    }
    return n;
  }

  const Buffer_t& __restrict _buf;
  const uint32_t _hashbits;
  int32_t : 32;  // Padding
  uint32_t* const __restrict _ht;
  uint32_t _match{0};
  uint32_t _match_length{0};
  uint32_t _expected_byte{0};
  int32_t : 32;                           // Padding
  StateMap_t<0x8000> _ltp0{};             // Length to prediction
  StateMap_t<0x4000> _ltp1{};             // (curved) Length to prediction
  RunContextMap_t _rc0{14, 23};           // match_length|c1_ | scale of 23 is based on enwik9
  RunContextMap_t _rc1{16 + level_, 49};  //              w5_ | scale of 49 is based on enwik9 | not part of model, just an improvement
  RunContextMap_t _rc2{16 + level_, 51};  //              x5_ | scale of 51 is based on enwik9 | not part of model, just an improvement
  RunContextMap_t _rc3{16 + level_, 32};  //              tt_ | scale of 32 is based on enwik9 | not part of model, just an improvement
  RunContextMap_t _rc4{16 + level_, 26};  //            word_ | scale of 26 is based on enwik9 | not part of model, just an improvement
  int32_t : 32;                           // Padding
  int32_t : 32;                           // Padding
  Blend_t<8> _blend{1u << 19, 4096};      // w5_
};
LempelZivPredict_t::~LempelZivPredict_t() noexcept {
  std::free(_ht);
}

/**
 * @class SparseMatchModel_t
 * @brief Sparse match model implementation
 *
 * Sparse match model implementation
 */
class SparseMatchModel_t final {
public:
  explicit SparseMatchModel_t(const Buffer_t& __restrict buf) noexcept
      : _buf{buf},  //
        _ht{static_cast<uint32_t*>(std::calloc((UINT64_C(1) << NBITS) + UINT64_C(1), sizeof(uint32_t)))} {
    if (verbose_) {
      fprintf(stdout, "%s for SparseMatchModel_t\n", GetDimension(((UINT64_C(1) << NBITS) + UINT64_C(1)) * sizeof(uint32_t)).c_str());
    }
  }
  virtual ~SparseMatchModel_t() noexcept;

  SparseMatchModel_t() = delete;
  SparseMatchModel_t(const SparseMatchModel_t&) = delete;
  SparseMatchModel_t(SparseMatchModel_t&&) = delete;
  auto operator=(const SparseMatchModel_t&) -> SparseMatchModel_t& = delete;
  auto operator=(SparseMatchModel_t&&) -> SparseMatchModel_t& = delete;

  void Update() noexcept {
    const auto idx{((UINT64_C(1) << NBITS) - 1) & cx_};

    if (_match_length >= MINLEN) {
      _match_length += _match_length < MAXLEN;
      ++_match;
    } else {
      _match_length = 0;
      _match = _ht[idx];
      if (_match) {
        while ((_match_length < MAXLEN) && (_buf(_match_length + 1) == _buf[_match - _match_length - 1])) {
          ++_match_length;
        }
      }
    }
    _ht[idx] = _buf.Pos();

    _expected_byte = _buf[_match];

    _cm0.Set(0);
    _cm1.Set(x5_);
  }

  void Predict(const bool bit) noexcept {
    auto& pr{_blend.Get()};

    if ((_match_length >= MINLEN) && (((_expected_byte | 0x100) >> (1 + bcount_)) == c0_)) {
      const auto expected_bit{UINT32_C(1) & (_expected_byte >> bcount_)};

      const auto sign{static_cast<int32_t>(2 * expected_bit) - 1};
      const auto length_to_prediction{sign * static_cast<int32_t>(_match_length) * 32};
      pr[0] = static_cast<int16_t>(clamp12(length_to_prediction));

      const auto ctx0{(_match_length << 9) | (expected_bit << 8) | c1_};  // 6+1+8=15 bits
      pr[1] = static_cast<int16_t>(_ltp.Update(bit, ctx0, 5));            // Rate of 5 is based on enwik9

      const auto ctx1{(_expected_byte << 11) | (bcount_ << 8) | _buf(1)};  // 8+3+8=19 bits
      pr[2] = static_cast<int16_t>(_sm1.Update(bit, ctx1, 8));             // Rate of 8 is based on enwik9
    } else {
      _match_length = 0;  // Wrong prediction, reset!
      pr[0] = 0;
      pr[1] = static_cast<int16_t>(_ltp.Update(bit, c1_, 5) / 4);      // Rate of 5, division of 4 are based on enwik9
      pr[2] = static_cast<int16_t>(_sm1.Update(bit, _buf(1), 4) / 8);  // Rate of 4, division of 8 are based on enwik9
    }

    const auto [p3, p4, p5]{_cm0.Predict(bit)};
    const auto [p6, p7, p8]{_cm1.Predict(bit)};

    pr[3] = p3;
    pr[4] = p4;
    pr[5] = p5;
    pr[6] = p6;
    pr[7] = p7;
#if 0  // Disabled, to have length of 8 for blend
    pr[8] = p8;
#endif

    const auto last_pr{Squash(Mixer_t::tx_[8])};  // Conversion from -2048..2047 (clamped) into 0..4095
    const auto ctx{(w5_ << 3) | bcount_};
    const int32_t err{((bit << 12) - static_cast<int32_t>(last_pr)) * 9};  // Scale of 9 is based on enwik9
    const auto px{_blend.Predict(err, ctx)};
    Mixer_t::tx_[8] = px;
  }

private:
  static constexpr auto NBITS{UINT32_C(15)};            // Size of look-up table (< 32) default 15 based on enwik9
  static constexpr auto MINLEN{UINT32_C(2)};            // Minimum required match length
  static constexpr auto MAXLEN{UINT32_C(MINLEN + 63)};  // Longest allowed match (max 6 bits, after subtraction of minimum length)

  const Buffer_t& __restrict _buf;
  uint32_t* const __restrict _ht;
  uint32_t _match{0};
  uint32_t _match_length{0};
  uint32_t _expected_byte{0};
  int32_t : 32;                                // Padding
  int32_t : 32;                                // Padding
  int32_t : 32;                                // Padding
  ContextMap_t<0x001, 0xC, 0xA, 0xD> _cm0{};   //     c0_ | Rates of 12/10/13 are based on enwik9 | not part of model, just an improvement
  ContextMap_t<0x100, 0xC, 0x6> _cm1{};        // x5_|c0_ | Rates of 12/ 6    are based on enwik9 | not part of model, just an improvement
  StateMap_t<0x8000> _ltp{};                   // length|expected_bit|c1
  StateMap_t<0x80000> _sm1{};                  // expected_byte|bcount|buf(1)
  Blend_t<8> _blend{UINT32_C(1) << 19, 4096};  // w5_
};
SparseMatchModel_t::~SparseMatchModel_t() noexcept {
  std::free(_ht);
}

/**
 * @class Txt_t
 * @brief Text prediction model
 *
 * This is not really a prediction model. It is more of a state machine that
 * can follow the sequence of text or value preparation.
 * If the text preparation fails or is ineffective, this model does nothing.
 * If the text preparation is successful, it can follow the byte order of the
 * text preparation. It can predict single bits with 100% accuracy.
 * When a debug version is created, this accuracy is tested by means of an assert.
 * Therefore the function 'Predict' has a parameter 'bit',
 * normally (release build) it is not used.
 * This model is not "super" effective, but does not claim memory or heavy CPU usage.
 * If the model is active, it predicts a few bits (2,5 or 10), in line with text preparation,
 * and it predicts a few bits (6,8,10,12,14,16,18,20 and 22), in line with value preparation.
 */
class Txt_t final {
public:
  explicit Txt_t() noexcept = default;
  ~Txt_t() noexcept = default;

  Txt_t(const Txt_t&) = delete;
  Txt_t(Txt_t&&) = delete;
  auto operator=(const Txt_t&) -> Txt_t& = delete;
  auto operator=(Txt_t&&) -> Txt_t& = delete;

  /**
   * Skip a few bytes till the text-preparation sequence starts.
   */
  void Update() noexcept {
    if (_dic_end_offset > 0) {
      if (_dic_start_offset > 0) {
        --_dic_start_offset;
      } else {
        --_dic_end_offset;
        if (0 == _dic_end_offset) {
          _prdct = 0;
          _value = 0;
        }
      }
    }

    if (_skip_bytes > 0) {
      --_skip_bytes;
    }
  }

  /**
   * Forecast a few bits with 100% accuracy
   * @param bit List bit seen
   * @return True when there is a forecast otherwise false
   */
  [[nodiscard]] auto Predict(const bool bit) noexcept -> std::pair<bool, uint16_t> {
    //                         (No prediction) || (          1 predicted) || (           0 predicted)
    const bool valid_prediction{(0x7FF == _pr) || (bit && (0xFFF == _pr)) || (!bit && (0x000 == _pr))};
    assert(valid_prediction);
    if (!valid_prediction) {
      _prdct = 0;  // Something went wrong, reset the prediction to minimise the damage
      _value = 0;
    }

    if (_start && (_dic_end_offset > 0) && (0 == _dic_start_offset)) {
      return PredictDictionary();
    }

    if (!_start || (_skip_bytes > 0)) {
      return {false, _pr = 0x7FF};  // No prediction
    }

    return PredictEncodedText();
  }

  void SetDataPos(const int64_t data_pos) noexcept {
    _skip_bytes = static_cast<uint32_t>(data_pos);
  }
  void SetStart(const bool state) noexcept {
    _start = state;
  }
  void SetDicStartOffset(const int64_t dic_start_offset) noexcept {
    _dic_start_offset = static_cast<uint32_t>(dic_start_offset);
  }
  void SetDicEndOffset(const int64_t dic_end_offset) noexcept {
    _dic_end_offset = static_cast<uint32_t>(dic_end_offset);
  }
  void SetDicWords(const int64_t number_of_words) noexcept {
    _number_of_words = static_cast<uint32_t>(number_of_words);

    struct mask_t final {
      const uint32_t words;
      const uint32_t mask;
    };

    static constexpr std::array<const mask_t, 10> mask_low{{{0x00042, 0b11111111111111100000000000000000},    // +10 bits prediction
                                                            {0x00044, 0b11111111111111000000000000000000},    //  +9 bits prediction
                                                            {0x00048, 0b11111111111110000000000000000000},    //  +8 bits prediction
                                                            {0x00050, 0b11111111111100000000000000000000},    //  +7 bits prediction
                                                            {0x00060, 0b11111111111000000000000000000000},    //  +6 bits prediction
                                                            {0x00080, 0b11111111000000000000000000000000},    //  +5 bits prediction
                                                            {0x000C0, 0b11111110000000000000000000000000},    //  +4 bits prediction
                                                            {0x00140, 0b11111100000000000000000000000000},    //  +3 bits prediction
                                                            {0x00240, 0b11111000000000000000000000000000},    //  +2 bits prediction
                                                            {0x00440, 0b11110000000000000000000000000000}}};  //  +1  bit prediction
    _extend_mask_low = 0;
    for (uint32_t n{0}; n < mask_low.size(); ++n) {
      if (mask_low[n].words > number_of_words) {
        _extend_mask_low = mask_low[n].mask;
        break;
      }
    }

    static constexpr std::array<const mask_t, 13> mask_mid{{{0x00842, 0b11111111111111111111110000000000},    // +13 bits prediction
                                                            {0x00844, 0b11111111111111111111100000000000},    // +12 bits prediction
                                                            {0x00848, 0b11111111111111111111000000000000},    // +11 bits prediction
                                                            {0x00850, 0b11111111111111111110000000000000},    // +10 bits prediction
                                                            {0x00860, 0b11111111111111111100000000000000},    //  +9 bits prediction
                                                            {0x00880, 0b11111111111111100000000000000000},    //  +8 bits prediction
                                                            {0x008C0, 0b11111111111111000000000000000000},    //  +7 bits prediction
                                                            {0x00940, 0b11111111111110000000000000000000},    //  +6 bits prediction
                                                            {0x00A40, 0b11111111111100000000000000000000},    //  +5 bits prediction
                                                            {0x00C40, 0b11111111111000000000000000000000},    //  +4 bits prediction
                                                            {0x01040, 0b11111110000000000000000000000000},    //  +3 bits prediction
                                                            {0x01840, 0b11111100000000000000000000000000},    //  +2 bits prediction
                                                            {0x02840, 0b11111000000000000000000000000000}}};  //  +1  bit prediction
    _extend_mask_mid = 0;
    for (uint32_t n{0}; n < mask_mid.size(); ++n) {
      if (mask_mid[n].words > number_of_words) {
        _extend_mask_mid = mask_mid[n].mask;
        break;
      }
    }

    static constexpr std::array<const mask_t, 16> mask_high{{{0x08842, 0b11111111111111111111111111111100},    // +16 bits prediction
                                                             {0x08844, 0b11111111111111111111111111111000},    // +15 bits prediction
                                                             {0x08848, 0b11111111111111111111111111110000},    // +14 bits prediction
                                                             {0x08850, 0b11111111111111111111111111100000},    // +13 bits prediction
                                                             {0x08860, 0b11111111111111111111111111000000},    // +12 bits prediction
                                                             {0x08880, 0b11111111111111111111111000000000},    // +11 bits prediction
                                                             {0x088C0, 0b11111111111111111111110000000000},    // +10 bits prediction
                                                             {0x08940, 0b11111111111111111111100000000000},    //  +9 bits prediction
                                                             {0x08A40, 0b11111111111111111111000000000000},    //  +8 bits prediction
                                                             {0x08C40, 0b11111111111111111110000000000000},    //  +7 bits prediction
                                                             {0x09040, 0b11111111111111100000000000000000},    //  +6 bits prediction
                                                             {0x09840, 0b11111111111111000000000000000000},    //  +5 bits prediction
                                                             {0x0A840, 0b11111111111110000000000000000000},    //  +4 bits prediction
                                                             {0x0C840, 0b11111111111100000000000000000000},    //  +3 bits prediction
                                                             {0x10840, 0b11111110000000000000000000000000},    //  +2 bits prediction
                                                             {0x18840, 0b11111100000000000000000000000000}}};  //  +1  bit prediction
    _extend_mask_high = 0;
    for (uint32_t n{0}; n < mask_high.size(); ++n) {
      if (mask_high[n].words > number_of_words) {
        _extend_mask_high = mask_high[n].mask;
        break;
      }
    }
  }

private:
  uint128_t _prdct{0};
  uint128_t _value{0};
  uint32_t _skip_bytes{0};
  uint32_t _dic_start_offset{0};
  uint32_t _dic_end_offset{0};
  uint32_t _extend_mask_low{0};
  uint32_t _extend_mask_mid{0};
  uint32_t _extend_mask_high{0};
  uint32_t _number_of_words{};
  uint16_t _pr{0x7FF};  // Prediction 0..4095
  bool _start{false};
  int32_t : 8;   // Padding
  int32_t : 32;  // Padding
  int32_t : 32;  // Padding
  int32_t : 32;  // Padding
  int32_t : 32;  // Padding

  constexpr void Shift() noexcept {
    _prdct += _prdct;
    _value += _value;
  }

  [[nodiscard]] auto PredictDictionary() noexcept -> std::pair<bool, uint16_t> {
    if (_value) {
      Shift();

      const bool has_value{(_value >> 127) ? true : false};
      if (has_value) {
        const bool prediction{(_prdct >> 127) ? true : false};
        return {true, _pr = prediction ? 0xFFF : 0x000};  // Prediction
      }
    } else {
      if ((3 == bcount_) && (TP5_ESCAPE_CHAR != (0xFF & cx_))) {
        static_assert(0x40 == TP5_NEGATIVE_CHAR, "Modify this when changed");
        if ((TP5_NEGATIVE_CHAR >> 4) == (0xF & c0_)) {
          //         40      80 --> 5 bits prediction
          //       0b010000001xxxxxxx
          _prdct = 0b01000000100000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000_xxl;
          _value = 0b11111111100000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000_xxl;
          //         ^^^^ppppp

          Shift();  // Get in sync
          Shift();
          Shift();
          Shift();
        } else {
          // Detect dictionary indexes
          if (0xC == (0xC & c0_)) {
            uint32_t prdct{0};
            uint32_t value{0};

            if (0xC == (0xE & c0_)) {
              // < MID
              //        C0      80 --> 2 bits prediction
              //      0b110xxxxx10xxxxxx
              prdct = 0b11000000100000000000000000000000;
              value = 0b11100000110000000000000000000000;
              //        ^^^     pp
              //
            } else if (0xE == (0xF & c0_)) {
              // < HIGH
              //        E0      C0      80 --> 5 bits prediction
              //      0b1110xxxx110xxxxx10xxxxxx
              prdct = 0b11100000110000001000000000000000;
              value = 0b11110000111000001100000000000000;
              //        ^^^^    ppp     pp
              //
            } else if (0xF == (0xF & c0_)) {
              // >= HIGH
              //        F0      E0      C0      80 --> 10 bits prediction
              //      0b11110xxx1110xxxx110xxxxx10xxxxxx
              prdct = 0b11110000111000001100000010000000;
              value = 0b11111000111100001110000011000000;
              //        ^^^^p   pppp    ppp     pp
            }

            assert(prdct > 0);
            assert(value > 0);

            _prdct = uint128_t(prdct) << 96;
            _value = uint128_t(value) << 96;

            Shift();  // Get in sync
            Shift();
            Shift();
            Shift();
          }
        }
      }
    }
    return {false, _pr = 0x7FF};  // No prediction
  }

  [[nodiscard]] auto PredictEncodedText() noexcept -> std::pair<bool, uint16_t> {
    if (_value) {
      Shift();

      const bool has_value{(_value >> 127) ? true : false};
      if (has_value) {
        const bool prediction{(_prdct >> 127) ? true : false};
        return {true, _pr = prediction ? 0xFFF : 0x000};  // Prediction
      }
    } else {
      if (TP5_ESCAPE_CHAR != (0xFF & cx_)) {
        // Detect dictionary indexes
        if ((3 == bcount_) && (0xC == (0xC & c0_))) {
          uint32_t prdct{0};
          uint32_t value{0};

          if (0xC == (0xE & c0_)) {
            assert(_number_of_words >= 64);
            // < MID
            //        C0      80 --> 2 bits prediction
            //      0b110xxxxx10xxxxxx
            prdct = 0b11000000100000000000000000000000;
            value = 0b11100000110000000000000000000000;
            //        ^^^     pp

            value |= _extend_mask_low;
          } else if (0xE == (0xF & c0_)) {
            assert(_number_of_words >= (64 + 2048));
            // < HIGH
            //        E0      C0      80 --> 5 bits prediction
            //      0b1110xxxx110xxxxx10xxxxxx
            prdct = 0b11100000110000001000000000000000;
            value = 0b11110000111000001100000000000000;
            //        ^^^^    ppp     pp

            value |= _extend_mask_mid;
          } else if (0xF == (0xF & c0_)) {
            assert(_number_of_words >= (64 + 2048 + 32768));
            // >= HIGH
            //        F0      E0      C0      80 --> 10 bits prediction
            //      0b11110xxx1110xxxx110xxxxx10xxxxxx
            prdct = 0b11110000111000001100000010000000;
            value = 0b11111000111100001110000011000000;
            //        ^^^^p   pppp    ppp     pp

            value |= _extend_mask_high;
          }

          assert(prdct > 0);
          assert(value > 0);

          _prdct = uint128_t(prdct) << 96;
          _value = uint128_t(value) << 96;

          Shift();  // Get in sync
          Shift();
          Shift();
          Shift();
        }

        // Detect value transformation <escape><0xFx><0x8x>...<0x0x>
        if ((5 == bcount_) && (0xF0 == (0xF0 & cx_)) && (0x06 == c0_)) {
          const auto costs{0x0F & cx_};
          switch (costs) {  // clang-format off
            case 0x4: // 80      80      80      00 --> 6 bits prediction
              //       0b10xxxxxx10xxxxxx10xxxxxx00xxxxxx
              _prdct = 0b10000000100000001000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000_xxl;
              _value = 0b11000000110000001100000011000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000_xxl;
              break;  // ^^      pp      pp      pp

            case 0x5: // 80      80      80      80      00 --> 8 bits prediction
              //       0b10xxxxxx10xxxxxx10xxxxxx10xxxxxx00xxxxxx
              _prdct = 0b10000000100000001000000010000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000_xxl;
              _value = 0b11000000110000001100000011000000110000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000_xxl;
              break;  // ^^      pp      pp      pp      pp

            case 0x6: // 80      80      80      80      80      00 --> 10 bits prediction
              //       0b10xxxxxx10xxxxxx10xxxxxx10xxxxxx10xxxxxx00xxxxxx
              _prdct = 0b10000000100000001000000010000000100000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000_xxl;
              _value = 0b11000000110000001100000011000000110000001100000000000000000000000000000000000000000000000000000000000000000000000000000000000000_xxl;
              break;  // ^^      pp      pp      pp      pp      pp

            case 0x7: // 80      80      80      80      80      80      00 --> 12 bits prediction
              //       0b10xxxxxx10xxxxxx10xxxxxx10xxxxxx10xxxxxx10xxxxxx00xxxxxx
              _prdct = 0b10000000100000001000000010000000100000001000000000000000000000000000000000000000000000000000000000000000000000000000000000000000_xxl;
              _value = 0b11000000110000001100000011000000110000001100000011000000000000000000000000000000000000000000000000000000000000000000000000000000_xxl;
              break;  // ^^      pp      pp      pp      pp      pp      pp

            case 0x8: // 80      80      80      80      80      80      80      00 --> 14 bits prediction
              //       0b10xxxxxx10xxxxxx10xxxxxx10xxxxxx10xxxxxx10xxxxxx10xxxxxx00xxxxxx
              _prdct = 0b10000000100000001000000010000000100000001000000010000000000000000000000000000000000000000000000000000000000000000000000000000000_xxl;
              _value = 0b11000000110000001100000011000000110000001100000011000000110000000000000000000000000000000000000000000000000000000000000000000000_xxl;
              break;  // ^^      pp      pp      pp      pp      pp      pp      pp

            case 0x9: // 80      80      80      80      80      80      80      80      00 --> 16 bits prediction
              //       0b10xxxxxx10xxxxxx10xxxxxx10xxxxxx10xxxxxx10xxxxxx10xxxxxx10xxxxxx00xxxxxx
              _prdct = 0b10000000100000001000000010000000100000001000000010000000100000000000000000000000000000000000000000000000000000000000000000000000_xxl;
              _value = 0b11000000110000001100000011000000110000001100000011000000110000001100000000000000000000000000000000000000000000000000000000000000_xxl;
              break;  // ^^      pp      pp      pp      pp      pp      pp      pp      pp

            case 0xA: // 80      80      80      80      80      80      80      80      80      00 --> 18 bits prediction
              //       0b10xxxxxx10xxxxxx10xxxxxx10xxxxxx10xxxxxx10xxxxxx10xxxxxx10xxxxxx10xxxxxx00xxxxxx
              _prdct = 0b10000000100000001000000010000000100000001000000010000000100000001000000000000000000000000000000000000000000000000000000000000000_xxl;
              _value = 0b11000000110000001100000011000000110000001100000011000000110000001100000011000000000000000000000000000000000000000000000000000000_xxl;
              break;  // ^^      pp      pp      pp      pp      pp      pp      pp      pp      pp

            case 0xB: // 80      80      80      80      80      80      80      80      80      80      00 --> 20 bits prediction
              //       0b10xxxxxx10xxxxxx10xxxxxx10xxxxxx10xxxxxx10xxxxxx10xxxxxx10xxxxxx10xxxxxx10xxxxxx00xxxxxx
              _prdct = 0b10000000100000001000000010000000100000001000000010000000100000001000000010000000000000000000000000000000000000000000000000000000_xxl;
              _value = 0b11000000110000001100000011000000110000001100000011000000110000001100000011000000110000000000000000000000000000000000000000000000_xxl;
              break;  // ^^      pp      pp      pp      pp      pp      pp      pp      pp      pp      pp

            case 0xC: // 80      80      80      80      80      80      80      80      80      80      80      00 --> 22 bits prediction
              //       0b10xxxxxx10xxxxxx10xxxxxx10xxxxxx10xxxxxx10xxxxxx10xxxxxx10xxxxxx10xxxxxx10xxxxxx10xxxxxx00xxxxxx
              _prdct = 0b10000000100000001000000010000000100000001000000010000000100000001000000010000000100000000000000000000000000000000000000000000000_xxl;
              _value = 0b11000000110000001100000011000000110000001100000011000000110000001100000011000000110000001100000000000000000000000000000000000000_xxl;
              break;  // ^^      pp      pp      pp      pp      pp      pp      pp      pp      pp      pp      pp

            case 0x00:
            case 0x01:
            case 0x02:
            case 0x03:
            case 0x0D:
            case 0x0E:
            case 0x0F:
            default: // Ignore the not used costs
              break;
          }  // clang-format on

          Shift();  // Get in sync
          Shift();
        }
      }
    }

    return {false, _pr = 0x7FF};  // No prediction
  }
};

namespace {
  // 2 bits markers at 0x0F, 0x66 and 0x67
  [[nodiscard]] constexpr auto Prefix(uint8_t c) noexcept -> uint32_t {
    // clang-format off
    if (0x0F == c) { return 1; }
    if (0x66 == c) { return 2; }
    if (0x67 == c) { return 3; }
    // clang-format on
    return 0;
  }

  // Get context relevant to parsing 32-bit x86 code
  [[nodiscard]] auto ExeContext(const Buffer_t& buf) noexcept -> uint32_t {
    auto ctx{0xC7u & buf(2)};            // Mod and r/m fields
    ctx = (ctx * 256) | buf(3);          // Opcode
    ctx = (ctx * 256) | Prefix(buf(4));  // Prefix
    ctx = (ctx * 4) | Prefix(buf(5));    // Prefix
    return ctx;
  }
};  // namespace

template <typename T>
[[nodiscard]] constexpr auto Balance(const T& weight, const T& px, const T& py) noexcept -> T {
  return static_cast<T>((((16 * px) - ((16 - weight) * (px - py))) + 8) / 16);
}

/**
 * @class SSE_t
 * @brief Secondary Symbol Estimation
 *
 * Secondary Symbol Estimation
 */
class SSE_t final {
public:
  SSE_t() noexcept {
    for (uint32_t n{0x000}; n <= 0xFFF; ++n) {
      const uint32_t n0{0xFFF - n};
      const uint32_t n1{n};
      _n0[n] = n0;  // 0xFFF ... 0x000
      _n1[n] = n1;  // 0x000 ... 0xFFF
      assert(n == ((0xFFF * n1) / (n0 + n1)));
    }
  }
  ~SSE_t() noexcept = default;
  SSE_t(const SSE_t&) = delete;
  SSE_t(SSE_t&&) = delete;
  auto operator=(const SSE_t&) -> SSE_t& = delete;
  auto operator=(SSE_t&&) -> SSE_t& = delete;

  [[nodiscard]] auto Predict16(const int32_t pr12, const bool bit) noexcept -> uint32_t {
    // Update
    if (bit) {
      ++_n1[_sse];
    } else {
      ++_n0[_sse];
    }
    if ((_n0[_sse] | _n1[_sse]) >> 21) {  // Shift of 21 is based on enwik9
      _n0[_sse] /= 2;
      _n1[_sse] /= 2;
    }
    _sse = Squash(pr12);  // Conversion from -2048..2047 (clamped) into 0..4095

    // Predict
    const uint64_t n0{_n0[_sse]};
    const uint64_t n1{_n1[_sse]};

    // clang-format off
    if (n0 == n1) { return 0x7FFF; } // no prediction
    if ( 0 == n0) { return 0xFFFF; } // one
    if ( 0 == n1) { return 0x0001; } // zero
    // clang-format on
    const auto pr{(UINT64_C(0xFFFF) * n1) / (n0 + n1)};
    return static_cast<uint32_t>(pr + (pr < 0x8000));
  }

private:
  std::array<uint32_t, 4096> _n0{};
  std::array<uint32_t, 4096> _n1{};
  uint32_t _sse{0};
};

/**
 * @class Predict_t
 * @brief Main model - predicts next bit probability from previous data
 *
 * Main model - predicts next bit probability from previous data
 */
class Predict_t final {
public:
  explicit Predict_t(Buffer_t& __restrict buf) noexcept : _buf{buf} {
    cp_[0] = cp_[1] = cp_[2] = cp_[3] = cp_[4] = _t0.data();
  }

  virtual ~Predict_t() noexcept;

  Predict_t() = delete;
  Predict_t(const Predict_t&) = delete;
  Predict_t(Predict_t&&) = delete;
  auto operator=(const Predict_t&) -> Predict_t& = delete;
  auto operator=(Predict_t&&) -> Predict_t& = delete;

  [[nodiscard]] auto Next(const bool bit) noexcept -> uint32_t {
    if (_fails & 0x80) {
      --_failcount;  // 0..8
    }
    _fails += _fails;
    _failz += _failz;
    const auto pr16{bit ? (_pr16 ^ 0xFFFF) : _pr16};  // Previous prediction 0..65535
    if (pr16 >= (375 * 32)) {
      ++_failz;
      if (pr16 >= (975 * 32)) {
        ++_fails;
        ++_failcount;  // 0..8
      }
    }

    // Filter the context model with APMs
    const auto p0{Predict(bit)};
    const auto p0s{Stretch(p0)};
    const auto p1{Balance(7u, _a1.Predict(bit, p0s, c0_), p0)};  // Weight of 7 is based on enwik9

#if 0
    uint32_t tra[12] = {0, 4, 3, 3, 0, 6, 6, 12, 0, 6, 12, 15};  // based on enwik9
    uint32_t cz{(1 & _fails) ? UINT32_C(9) : UINT32_C(1)};
    cz += tra[0 + ((_fails >> 5) & 3)];
    cz += tra[4 + ((_fails >> 3) & 3)];
    cz += tra[8 + ((_fails >> 1) & 3)];
    cz = (std::min)(UINT32_C(9), (_failcount + cz) / 2);
#else
    uint32_t cz{(1 & _fails) ? UINT32_C(9) : UINT32_C(1)};
    cz += 0xFu & (0x3340u >> (4 * (3 & (_fails >> 5))));
    cz += 0xFu & (0xC660u >> (4 * (3 & (_fails >> 3))));
    cz += 0xFu & (0xFC60u >> (4 * (3 & (_fails >> 1))));
    cz = (std::min)(UINT32_C(9), (_failcount + cz) / 2);
#endif

    // clang-format off
    const auto p2{_a2.Predict(bit,         p0s, Finalise64(Hash(  8*c0_, 0x7FF & _failz                         ), 27))};           // hash bits of 27 is based on enwik9
    const auto p3{_a3.Predict(bit,         p0s, Finalise64(Hash( 32*c0_, 0x80FFFF & x5_                         ), 25))};           // hash bits of 25 is based on enwik9
    const auto p4{_a4.Predict(bit, Stretch(p1), Finalise64(Hash(_buf(1), 0xFF & (x5_ >> 8), 0x80FF & (x5_ >> 16)), 57) ^ (2*c0_))}; // hash bits of 57 is based on enwik9
    const auto p4s{Stretch(p4)};
    const auto p5{_a5.Predict(bit, Stretch(p2), Finalise64(Hash(    c0_, w5_                                    ), 24))};           // hash bits of 24 is based on enwik9
    const auto p6{_a6.Predict(bit,         p4s, Finalise64(Hash(    cz, 0x0080FF & x5_                          ), 57) ^ (4*c0_))}; // hash bits of 57 is based on enwik9
    // clang-format on

    auto& pr{_blend.Get()};
    if (0x7FF != _pt) {
      const auto no_model_pr{static_cast<int16_t>(_pt ? 0x7FF : ~0x7FF)};
      pr[0] = no_model_pr;
      pr[1] = no_model_pr;
      pr[2] = no_model_pr;
      pr[3] = no_model_pr;
    } else {
      pr[0] = static_cast<int16_t>(Stretch(p3));  // Conversions from 0..4095 into -2048..2047
      pr[1] = static_cast<int16_t>(p4s);
      pr[2] = static_cast<int16_t>(Stretch(p5));
      pr[3] = static_cast<int16_t>(Stretch(p6));
    }

    const auto ctx{(w5_ << 1) | ((0xFF & _fails) ? 1 : 0)};
    const int32_t err{((bit << 16) - static_cast<int32_t>(_pr16)) / 8};  // Division of 8 is based on enwik9
    const auto pr12{_blend.Predict(err, ctx)};

    _pr16 = _sse.Predict16(pr12, bit);
    if (0x7FF != _pt) {
      _pr16 = _pt ? 0xFFFF : 0x0000;
    }
    return _pr16;
  }

  void SetBinary(const bool is_binary) noexcept {
    _is_binary = is_binary;
  }
  void SetDataPos(const int64_t data_pos) noexcept {
    _txt.SetDataPos(data_pos);
  }
  void SetStart(const bool state) noexcept {
    _txt.SetStart(state);
  }
  void SetDicStartOffset(const int64_t dic_start_offset) noexcept {
    _txt.SetDicStartOffset(dic_start_offset);
  }
  void SetDicEndOffset(const int64_t dic_end_offset) noexcept {
    _txt.SetDicEndOffset(dic_end_offset);
  }
  void SetDicWords(const int64_t number_of_words) noexcept {
    _txt.SetDicWords(number_of_words);
  }

private:
  Buffer_t& __restrict _buf;
  uint32_t _add2order{0};
  uint32_t _fails{0};
  uint32_t _failz{0};
  uint32_t _failcount{0};
  Mixer_t _mixer{};
  DynamicMarkovModel_t _dmc{MEM()};
  LempelZivPredict_t _lzp{_buf, MEM(20)};
  SparseMatchModel_t _smm{_buf};
  Txt_t _txt{};
  APM_t _ax1{0x10000, 9216, 7};  // Fixed 16 bit context | Offset 7 is based on enwik9
  APM_t _ax2{0x4000, 3722, 31};  //                      | Offset 31 is based on enwik9
  APM_t _a1{0x100, 9238};        // Fixed 8 bit context  | Offset 8 (fixed)
  APM_t _a2{MEM(9), 9238};       // 5                    | Offset 8 (fixed)
  APM_t _a3{MEM(12), 9238};      // 3                    | Offset 8 (fixed)
  APM_t _a4{MEM(14), 9238};      // 1                    | Offset 8 (fixed)
  APM_t _a5{MEM(12), 9238};      // 2                    | Offset 8 (fixed)
  APM_t _a6{MEM(9), 9238};       // 4                    | Offset 8 (fixed)
  uint32_t _mxr_pr{0x7FF};
  uint32_t _pt{0x7FF};
  uint32_t _pr16{0x7FFF};  // Prediction 0..65535
  int32_t : 32;            // Padding
  HashTable_t _t4a{MEM(23)};
  HashTable_t _t4b{MEM(23)};
  bool _is_binary{false};
  int32_t : 24;                                // Padding
  int32_t : 32;                                // Padding
  int32_t : 32;                                // Padding
  int32_t : 32;                                // Padding
  Blend_t<4> _blend{UINT32_C(1) << 19, 4096};  // w5_
  std::array<uint8_t, 0x10000> _t0{};
  uint8_t* __restrict _t0c1{_t0.data()};
  uint32_t _ctx1{0};
  uint32_t _ctx2{0};
  uint32_t _ctx3{0};
  uint32_t _ctx4{0};
  uint32_t _ctx5{0};
  uint32_t _pw{0};
  int32_t* _ctx6{&smt_[0][0]};
  uint32_t _bc4cp0{0};  // Range 0,1,2 or 3
  SSE_t _sse{};
  int32_t : 32;  // Padding
  int32_t : 32;  // Padding
  int32_t : 32;  // Padding
  int32_t : 32;  // Padding

  [[nodiscard]] auto Predict_not32(const bool bit) noexcept -> uint32_t {
    auto y2o{(bit << 20) - bit};

    const auto len{_lzp.Predict(bit)};        // len --> 0..9
    _mixer.Context(_add2order + (64 * len));  // len --> 0..576 --> 10800+576+(9*8)
    _ctx6[0] += (y2o - _ctx6[0]) >> 6;        // (6) 6 is based on enwik9 (little influence)
    _ctx6 = &smt_[_bc4cp0][_t0c1[c0_]];       // smt[0,1,2 or 3][...]

    smt_[0x5][_ctx5] += (y2o - smt_[0x5][_ctx5]) * limits_15a(_ctx5) >> 9;  // P5
    y2o += 384;                                                             //
    smt_[0x4][_ctx1] += (y2o - smt_[0x4][_ctx1]) >> 9;                      // P1
    smt_[0x6][_ctx2] += (y2o - smt_[0x6][_ctx2]) >> 9;                      // P2
    smt_[0x8][_ctx3] += (y2o - smt_[0x8][_ctx3]) >> 10;                     // P3
    smt_[0xA][_ctx4] += (y2o - smt_[0xA][_ctx4]) >> 10;                     // P4

    _ctx1 = *cp_[0x0];
    _ctx2 = *cp_[0x1];
    _ctx3 = *cp_[0x2];
    _ctx4 = *cp_[0x3];
    _ctx5 = *cp_[0x4];

    Mixer_t::tx_[1] = Stretch256(smt_[0x4][_ctx1]);  // Conversion from 0..1048575 into -2048..2047
    Mixer_t::tx_[2] = Stretch256(smt_[0x6][_ctx2]);  // Conversion from 0..1048575 into -2048..2047
    Mixer_t::tx_[3] = Stretch256(smt_[0x8][_ctx3]);  // Conversion from 0..1048575 into -2048..2047
    Mixer_t::tx_[4] = Stretch256(smt_[0xA][_ctx4]);  // Conversion from 0..1048575 into -2048..2047
    Mixer_t::tx_[5] = Stretch256(smt_[0x5][_ctx5]);  // Conversion from 0..1048575 into -2048..2047
    Mixer_t::tx_[6] = Stretch256(_ctx6[0]);          // Conversion from 0..1048575 into -2048..2047

    const auto pr{_mixer.Predict()};
    _mxr_pr = _ax1.Predict(bit, pr, c2_ | c0_);
    const auto px{Balance(3u, Squash(pr), _mxr_pr)};  // Conversion from -2048..2047 (clamped) into 0..4095, Weight of 3 is based on enwik9

    const auto py{_ax2.Predict(bit, Stretch(px), (fails_ * 8) + bcount_)};  // Conversion from 0..4095 into -2048..2047
    const auto pz{Balance(4u, _mxr_pr, py)};                                // Weight of 4 is based on enwik9
    assert(pz < 0x1000);
    return pz;
  }

  [[nodiscard]] auto Predict_not32s(const bool bit) noexcept -> uint32_t {
    auto y2o{(bit << 20) - bit};

    const auto len{_lzp.Predict(bit)};        // len --> 0..9
    _mixer.Context(_add2order + (64 * len));  // len --> 0..576 --> 10800+576+(9*8)
    _ctx6[0] += (y2o - _ctx6[0]) >> 6;        // (6) 6 is based on enwik9 (little influence)
    _ctx6 = &smt_[_bc4cp0][_t0c1[1]];         // smt[0,1,2 or 3][...] with c0_=1

    smt_[0x4][_ctx1] += (y2o - smt_[0x4][_ctx1]) >> 9;                      // P1
    smt_[0x5][_ctx5] += (y2o - smt_[0x5][_ctx5]) * limits_15a(_ctx5) >> 9;  // P5

    if (0x2000 == (0xFF00 & cx_)) {
      y2o += 768;
      smt_[0x7][_ctx2] += (y2o - smt_[0x7][_ctx2]) >> 10;  // P2
      smt_[0x9][_ctx3] += (y2o - smt_[0x9][_ctx3]) >> 11;  // P3
      smt_[0xB][_ctx4] += (y2o - smt_[0xB][_ctx4]) >> 11;  // P4
    } else {
      y2o += 384;
      smt_[0x6][_ctx2] += (y2o - smt_[0x6][_ctx2]) >> 9;   // P2
      smt_[0x8][_ctx3] += (y2o - smt_[0x8][_ctx3]) >> 10;  // P3
      smt_[0xA][_ctx4] += (y2o - smt_[0xA][_ctx4]) >> 9;   // P4
    }

    _ctx1 = *cp_[0x0];
    _ctx2 = *cp_[0x1];
    _ctx3 = *cp_[0x2];
    _ctx4 = *cp_[0x3];
    _ctx5 = *cp_[0x4];

    Mixer_t::tx_[1] = Stretch256(smt_[0x4][_ctx1]);  // Conversion from 0..1048575 into -2048..2047
    Mixer_t::tx_[2] = Stretch256(smt_[0x6][_ctx2]);  // Conversion from 0..1048575 into -2048..2047
    Mixer_t::tx_[3] = Stretch256(smt_[0x8][_ctx3]);  // Conversion from 0..1048575 into -2048..2047
    Mixer_t::tx_[4] = Stretch256(smt_[0xA][_ctx4]);  // Conversion from 0..1048575 into -2048..2047
    Mixer_t::tx_[5] = Stretch256(smt_[0x5][_ctx5]);  // Conversion from 0..1048575 into -2048..2047
    Mixer_t::tx_[6] = Stretch256(_ctx6[0]);          // Conversion from 0..1048575 into -2048..2047

    const auto pr{_mixer.Predict()};
    const auto px{_ax1.Predict(bit, pr, c2_ | c0_)};
    _mxr_pr = Balance(2u, Squash(pr), px);  // Conversion from -2048..2047 (clamped) into 0..4095, Weight of 2 is based on enwik9

    const auto py{_ax2.Predict(bit, Stretch(px), (fails_ * 8) + 7)};  // Conversion from 0..4095 into -2048..2047
    const auto pz{Balance(8u, _mxr_pr, py)};                          // Weight of 8 is based on enwik9
    assert(pz < 0x1000);
    return pz;
  }

  [[nodiscard]] auto Predict_was32(const bool bit) noexcept -> uint32_t {
    auto y2o{(bit << 20) - bit};

    const auto len{_lzp.Predict(bit)};        // len --> 0..9
    _mixer.Context(_add2order + (64 * len));  // len --> 0..576 --> 10800+576+(9*8)
    _ctx6[0] += (y2o - _ctx6[0]) >> 7;        // (8) 7 is based on enwik9 (little influence)
    _ctx6 = &smt_[1][_t0c1[c0_]];

    smt_[0x5][_ctx5] += (y2o - smt_[0x5][_ctx5]) * limits_15b(_ctx5) >> 10;  // P5
    y2o += 768;                                                              //
    smt_[0x4][_ctx1] += (y2o - smt_[0x4][_ctx1]) >> 14;                      // P1
    smt_[0x7][_ctx2] += (y2o - smt_[0x7][_ctx2]) >> 10;                      // P2
    smt_[0x9][_ctx3] += (y2o - smt_[0x9][_ctx3]) >> 11;                      // P3
    smt_[0xB][_ctx4] += (y2o - smt_[0xB][_ctx4]) >> 10;                      // P4

    _ctx1 = *cp_[0x0];
    _ctx2 = *cp_[0x1];
    _ctx3 = *cp_[0x2];
    _ctx4 = *cp_[0x3];
    _ctx5 = *cp_[0x4];

    Mixer_t::tx_[1] = Stretch256(smt_[0x4][_ctx1]);  // Conversion from 0..1048575 into -2048..2047
    Mixer_t::tx_[2] = Stretch256(smt_[0x7][_ctx2]);  // Conversion from 0..1048575 into -2048..2047
    Mixer_t::tx_[3] = Stretch256(smt_[0x9][_ctx3]);  // Conversion from 0..1048575 into -2048..2047
    Mixer_t::tx_[4] = Stretch256(smt_[0xB][_ctx4]);  // Conversion from 0..1048575 into -2048..2047
    Mixer_t::tx_[5] = Stretch256(smt_[0x5][_ctx5]);  // Conversion from 0..1048575 into -2048..2047
    Mixer_t::tx_[6] = Stretch256(_ctx6[0]);          // Conversion from 0..1048575 into -2048..2047

    const auto pr{_mixer.Predict()};
    _mxr_pr = _ax1.Predict(bit, pr, c2_ | c0_);
    const auto px{Balance(12u, Squash(pr), _mxr_pr)};                            // Conversion from -2048..2047 (clamped) into 0..4095, Weight of 12 is based on enwik9
    const auto py{_ax2.Predict(bit, Stretch(_mxr_pr), (fails_ * 8) + bcount_)};  // Conversion from 0..4095 into -2048..2047
    const auto pz{Balance(6u, px, py)};                                          // Weight of 6 is based on enwik9
    assert(pz < 0x1000);
    return pz;
  }

  [[nodiscard]] auto Predict_was32s(const bool bit) noexcept -> uint32_t {
    auto y2o{(bit << 20) - bit};

    const auto len{_lzp.Predict(bit)};        // len --> 0..9
    _mixer.Context(_add2order + (64 * len));  // len --> 0..576 --> 10800+576+(9*8)
    _ctx6[0] += (y2o - _ctx6[0]) >> 13;       // (12) 13 is based on enwik9 (little influence)
    _ctx6 = &smt_[1][_t0c1[1]];               // c0_=1

    smt_[0x5][_ctx5] += (y2o - smt_[0x5][_ctx5]) * limits_15b(_ctx5) >> 14;  // P5
    y2o += 6144;                                                             //
    smt_[4][_ctx1] += (y2o - smt_[4][_ctx1]) >> 14;                          // P1

    if (0x2000 == (0xFF00 & cx_)) {
      smt_[0x7][_ctx2] += (y2o - smt_[0x7][_ctx2]) >> 13;  // P2
      smt_[0x9][_ctx3] += (y2o - smt_[0x9][_ctx3]) >> 14;  // P3
      smt_[0xB][_ctx4] += (y2o - smt_[0xB][_ctx4]) >> 13;  // P4
    } else {
      smt_[0x6][_ctx2] += (y2o - smt_[0x6][_ctx2]) >> 13;  // P2
      smt_[0x8][_ctx3] += (y2o - smt_[0x8][_ctx3]) >> 14;  // P3
      smt_[0xA][_ctx4] += (y2o - smt_[0xA][_ctx4]) >> 13;  // P4
    }

    _ctx1 = *cp_[0x0];
    _ctx2 = *cp_[0x1];
    _ctx3 = *cp_[0x2];
    _ctx4 = *cp_[0x3];
    _ctx5 = *cp_[0x4];

    Mixer_t::tx_[1] = Stretch256(smt_[0x4][_ctx1]);  // Conversion from 0..1048575 into -2048..2047
    Mixer_t::tx_[2] = Stretch256(smt_[0x6][_ctx2]);  // Conversion from 0..1048575 into -2048..2047
    Mixer_t::tx_[3] = Stretch256(smt_[0x8][_ctx3]);  // Conversion from 0..1048575 into -2048..2047
    Mixer_t::tx_[4] = Stretch256(smt_[0xA][_ctx4]);  // Conversion from 0..1048575 into -2048..2047
    Mixer_t::tx_[5] = Stretch256(smt_[0x5][_ctx5]);  // Conversion from 0..1048575 into -2048..2047
    Mixer_t::tx_[6] = Stretch256(_ctx6[0]);          // Conversion from 0..1048575 into -2048..2047

    const auto pr{_mixer.Predict()};
    const auto px{_ax1.Predict(bit, pr, c2_ | c0_)};
    _mxr_pr = Balance(6u, Squash(pr), px);  // Conversion from -2048..2047 (clamped) into 0..4095, Weight of 6 is based on enwik9

    const auto py{_ax2.Predict(bit, Stretch(px), (fails_ * 8) + 7)};  // Conversion from 0..4095 into -2048..2047
    const auto pz{Balance(12u, _mxr_pr, py)};                         // Weight of 12 is based on enwik9
    assert(pz < 0x1000);
    return pz;
  }

  void UpdateStates(const bool bit, int32_t context) noexcept {
    const auto& p{bit ? state_table_y1_ : state_table_y0_};
    int32_t r{1 & context};
    const auto& q{r ? state_table_y1_ : state_table_y0_};

    auto* const __restrict toc{_t0c1};
    toc[context] = p[2][toc[context]];
    context >>= 1;
    toc[context] = q[2][toc[context]];
    r = r ^ ~0;  // 0 --> -1    1 --> -2

    auto* const __restrict cp0{cp_[0]};
    cp0[0] = p[1][cp0[0]];
    cp0[r] = q[1][cp0[r]];

    auto* const __restrict cp1{cp_[1]};
    cp1[0] = p[0][cp1[0]];  // in lpaq9m 4 for was32
    cp1[r] = q[0][cp1[r]];

    auto* const __restrict cp2{cp_[2]};
    cp2[0] = p[3][cp2[0]];
    cp2[r] = q[3][cp2[r]];

    auto* const __restrict cp3{cp_[3]};
    cp3[0] = p[4][cp3[0]];
    cp3[r] = q[4][cp3[r]];

    auto* const __restrict cp4{cp_[4]};
    cp4[0] = p[5][cp4[0]];  // In lpaq9m cycles between 5,3,1,5,..
    cp4[r] = q[5][cp4[r]];  // Staying in 5 performs better
  }

  [[nodiscard]] auto calcfails(uint32_t err) noexcept -> uint32_t {
    assert(err < 0x1000);
#if 0
    //                                                {{26, 42, 25, 43, 26, 62, 2, 40, 22, 64, 0, 45, 1, 9, 23, 40}};  // based on enwik8
    static constexpr std::array<const uint8_t, 16> lvl{{24, 44, 25, 45, 25, 64, 2, 26, 22, 51, 0, 44, 0, 3, 25, 42}};  // based on enwik9
    err /= 64;
    const uint32_t v{(err >= lvl[(2 * bcount_) + 1]) ? 3u : (err >= lvl[2 * bcount_]) ? 1u : 0u};
#elif 0
    uint32_t v{0};
    switch (bcount_) {  // clang-format off
    default:
    case 0: if (err >= (24 * 64)) { v = 1; } if (err >= (44 * 64)) { v = 3; } break;
    case 1: if (err >= (25 * 64)) { v = 1; } if (err >= (45 * 64)) { v = 3; } break;
    case 2: if (err >= (25 * 64)) { v = 1; }                                  break;
    case 3: if (err >= ( 2 * 64)) { v = 1; } if (err >= (26 * 64)) { v = 3; } break;
    case 4: if (err >= (22 * 64)) { v = 1; } if (err >= (51 * 64)) { v = 3; } break;
    case 5:                         v = 1;   if (err >= (44 * 64)) { v = 3; } break;
    case 6:                         v = 1;   if (err >= ( 3 * 64)) { v = 3; } break;
    case 7: if (err >= (25 * 64)) { v = 1; } if (err >= (42 * 64)) { v = 3; } break;
    }  // clang-format on
#elif 1
    static constexpr std::array<const uint128_t, 8> cf{{
        0xFFFFFFFFFF5555555555000000000000_xxl,
        0xFFFFFFFFFD5555555554000000000000_xxl,
        0x55555555555555555554000000000000_xxl,
        0xFFFFFFFFFFFFFFFFFFF5555555555550_xxl,
        0xFFFFFFD5555555555555500000000000_xxl,
        0xFFFFFFFFFF5555555555555555555555_xxl,
        0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFD5_xxl,
        0xFFFFFFFFFFF555555554000000000000_xxl,
    }};
    const uint32_t v{3u & uint32_t(cf[bcount_] >> (2 * (err / 64)))};
#endif
    return v;
  }

  [[nodiscard]] auto Predict(const bool bit) noexcept -> uint32_t {
#if 1
    // const auto MU{static_cast<int8_t>(INT64_C(0x0F090B0E191B3430) >> (8 * bcount_))};  // based on enwik8
    const auto MU{static_cast<int8_t>(INT64_C(0x06100F101A15282D) >> (8 * bcount_))};  // based on enwik9
#else
    //                                                    30  34  1B  19   E   B  9   F
    // static constexpr std::array<const int8_t, 8> flaw{{48, 52, 27, 25, 14, 11, 9, 15}};  // based on enwik8

    //                                                 2D  28  15  1A  10  0F  10  6
    static constexpr std::array<const int8_t, 8> flaw{{45, 40, 21, 26, 16, 15, 16, 6}};  // based on enwik9
    const int8_t MU{flaw[bcount_]};
#endif

    fails_ += fails_;
    bcount_ = 7 & (bcount_ - 1);
    // bpos_ = (bpos_ + 1) & 7;

    {
      const auto err{(bit << 12) - static_cast<int32_t>(_mxr_pr) - bit};
#if 1
      const auto fail{(std::abs)(err)};
#endif
      if (fail >= MU) {
        fails_ |= calcfails(uint32_t(fail));
        _mixer.Update(err);
      }
    }

    const auto cx{static_cast<int32_t>(c0_)};
    c0_ += c0_ + static_cast<uint32_t>(bit);
    _add2order += Mixer_t::N_LAYERS;

    switch (bcount_) {
      case 6:    // c0_ contains 1 bit
      case 4:    // c0_ contains 3 bits
      case 2:    // c0_ contains 5 bits
      case 0: {  // c0_ contains 7 bits
        const auto z{bit ? 2 : 1};
        cp_[0] += z;
        cp_[1] += z;
        cp_[2] += z;
        cp_[3] += z;
        cp_[4] += z;
      } break;

      case 5: {  // c0_ contains 2 bits
        UpdateStates(bit, cx);
        auto zq{2 + (c0_ & 0x03) * 2};
        cp_[0] = _t4b.get1x(0x00, zq + hh_[0]);  // 000 (0)
        cp_[1] = _t4a.get1x(0x80, zq + hh_[1]);  // 100 (4)
        cp_[4] = _t4b.get1x(0x00, zq + hh_[4]);  // 000 (0)
        zq *= 2;
        cp_[2] = _t4a.get3a(0x00, zq + hh_[2]);  // 000 (0)
        cp_[3] = _t4b.get3a(0x80, zq + hh_[3]);  // 100 (4)
      } break;

      case 1: {  // c0_ contains 6 bits
        UpdateStates(bit, cx);
        auto zq{2 + (c0_ & 0x3F) * 2};
        cp_[0] = _t4b.get1x(0xC0, zq + hh_[0]);  // 110 (6)
        cp_[1] = _t4a.get1x(0x40, zq + hh_[1]);  // 010 (2)
        cp_[4] = _t4b.get1x(0xC0, zq + hh_[4]);  // 110 (6)
        zq *= 2;
        cp_[2] = _t4a.get3b(0xC0, zq + hh_[2]);  // 110 (6)
        cp_[3] = _t4b.get3b(0x40, zq + hh_[3]);  // 010 (2)
      } break;

      case 3: {  // c0_ contains 4 bits
        UpdateStates(bit, cx);
        const auto zq{2 + (c0_ & 0x0F) * 2};
        const auto blur{Utilities::PHI32 * zq};
        const auto c4{cx_ & 0xFFFFFFFF};
        const auto c8{cx_ >> 32};
        hh_[0] = Finalise64(Hash(zq - hh_[0]), 32);
        hh_[1] ^= blur;
        hh_[2] = Finalise64(Hash(zq, c4, c8 & 0x000080FF), 32);
        hh_[3] = Finalise64(Hash(zq, c4, c8 & 0x00FFFFFF), 32);
        hh_[4] ^= blur;
        cp_[0] = _t4b.get1x(0xA0, hh_[0]);  // 101 (5)
        cp_[1] = _t4a.get1x(0x20, hh_[1]);  // 001 (1)
        cp_[2] = _t4a.get3b(0xA0, hh_[2]);  // 101 (5)
        cp_[3] = _t4b.get3b(0x20, hh_[3]);  // 001 (1)
        cp_[4] = _t4b.get1x(0xA0, hh_[4]);  // 101 (5)
      } break;

      case 7:
      default: {  // c0_ contains 8 bits (from previous cycle) --> Reset to 1 for new cycle
        UpdateStates(bit, cx);
        const auto ch{static_cast<uint8_t>(c0_)};
        c0_ = ch;
        const auto idx{Mixer_t::N_LAYERS * 10u * 4u * WRT_mxr[ch]};  // 9*10*4*(0..30) --> 10800
        _add2order = idx;

        // Based on enwik9                                       0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F              0 1 2 3 4 5 6 7 8 9 A B C D E F
        static constexpr std::array<const uint8_t, 256> WRT_mtt{{0, 4, 2, 1, 3, 3, 3, 7, 3, 2, 4, 0, 0, 0, 0, 0,    // 00-0F . . . . . . . . . . . . . . . .
                                                                 3, 0, 0, 0, 5, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0,    // 10-1F . . . . . . . . . . . . . . . .
                                                                 2, 4, 3, 3, 4, 0, 6, 0, 5, 7, 0, 4, 3, 7, 0, 3,    // 20-2F   ! " # $ % & ' ( ) * + , - . /
                                                                 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 3, 3, 3, 3, 4, 4,    // 30-3F 0 1 2 3 4 5 6 7 8 9 : ; < = > ?
                                                                 0, 5, 5, 5, 2, 5, 5, 5, 5, 5, 5, 5, 3, 5, 5, 5,    // 40-4F @ A B C D E F G H I J K L M N O
                                                                 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 4, 6, 3, 6, 7,    // 50-5F P Q R S T U V W X Y Z [ \ ] ^ _
                                                                 4, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,    // 60-6F ` a b c d e f g h i j k l m n o
                                                                 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 2, 4, 2, 7, 4,    // 70-7F p q r s t u v w x y z { | } ~ .
                                                                 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,    // 80-8F . . . . . . . . . . . . . . . .
                                                                 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,    // 90-9F . . . . . . . . . . . . . . . .
                                                                 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,    // A0-AF . . . . . . . . . . . . . . . .
                                                                 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,    // B0-BF . . . . . . . . . . . . . . . .
                                                                 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,    // C0-CF . . . . . . . . . . . . . . . .
                                                                 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,    // D0-DF . . . . . . . . . . . . . . . .
                                                                 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,    // E0-EF . . . . . . . . . . . . . . . .
                                                                 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7}};  // F0-FF . . . . . . . . . . . . . . . .
        if (!(0xFF & _pw)) {
          c1_ = (static_cast<uint32_t>(WRT_mtt[ch]) << 2) + 33u;  // 0..61
        } else {
          c1_ = (static_cast<uint32_t>(WRT_mtt[ch]) << 5) | (0x1F & _pw);  // 0..224 | (0..31)
        }
        c2_ = c1_ * 256;

        _buf.Add(ch);
        cx_ = (cx_ << 8) | ch;
        _t0c1 = &_t0[ch * 256];

        if (!(ch & 0x80)) {
#if 1
          static constexpr auto TxtFilter{0x28000001D00000000000C14000000400_xxl};
          static constexpr auto ExeFilter{0x00000000000000000000000000008002_xxl};

          if (const auto filter{_is_binary ? ExeFilter : TxtFilter}; 1 & (filter >> ch)) {
#else
          // \n & ( . / \ ^ _ ' { }                                  0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F              0 1 2 3 4 5 6 7 8 9 A B C D E F
          static constexpr std::array<const uint8_t, 128> TxtFilter{{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0,    // 00-0F . . . . . . . . . . . . . . . .
                                                                     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,    // 10-1F . . . . . . . . . . . . . . . .
                                                                     0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 1, 1,    // 20-2F   ! " # $ % & ' ( ) * + , - . /
                                                                     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,    // 30-3F 0 1 2 3 4 5 6 7 8 9 : ; < = > ?
                                                                     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,    // 40-4F @ A B C D E F G H I J K L M N O
                                                                     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 1,    // 50-5F P Q R S T U V W X Y Z [ \ ] ^ _
                                                                     1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,    // 60-6F ` a b c d e f g h i j k l m n o
                                                                     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 0}};  // 70-7F p q r s t u v w x y z { | } ~ .

          // 01 0F                                                   0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F              0 1 2 3 4 5 6 7 8 9 A B C D E F
          static constexpr std::array<const uint8_t, 128> ExeFilter{{0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,    // 00-0F . . . . . . . . . . . . . . . .
                                                                     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,    // 10-1F . . . . . . . . . . . . . . . .
                                                                     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,    // 20-2F   ! " # $ % & ' ( ) * + , - . /
                                                                     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,    // 30-3F 0 1 2 3 4 5 6 7 8 9 : ; < = > ?
                                                                     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,    // 40-4F @ A B C D E F G H I J K L M N O
                                                                     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,    // 50-5F P Q R S T U V W X Y Z [ \ ] ^ _
                                                                     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,    // 60-6F ` a b c d e f g h i j k l m n o
                                                                     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}};  // 70-7F p q r s t u v w x y z { | } ~ .

          if (const auto& filter{_is_binary ? ExeFilter : TxtFilter}; filter[ch]) {
#endif
            tt_ = (tt_ & UINT32_C(-8)) + 1;
            w5_ = (w5_ << 8) | 0x3FF;
            x5_ = (x5_ << 8) + ch;
          }
        }

        tt_ = (tt_ * 8) + WRT_mtt[ch];
        w5_ = (w5_ * 4) + static_cast<uint32_t>(0xFU & (0x21000000111111111111224333144402_xxl >> (4 * (ch >> 3))));  // WRT_mpw
        x5_ = (x5_ << 8) + ch;

        //                                                       0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F              0 1 2 3 4 5 6 7 8 9 A B C D E F
        static constexpr std::array<const uint8_t, 256> WRT_wrd{{2, 3, 1, 1, 0, 1, 3, 0, 0, 0, 0, 1, 0, 0, 1, 0,    // 00-0F . . . . . . . . . . . . . . . .
                                                                 0, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0,    // 10-1F . . . . . . . . . . . . . . . .
                                                                 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3,    // 20-2F   ! " # $ % & ' ( ) * + , - . /
                                                                 3, 3, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 2, 0, 2, 0,    // 30-3F 0 1 2 3 4 5 6 7 8 9 : ; < = > ?
                                                                 3, 0, 3, 1, 1, 1, 1, 1, 3, 3, 1, 1, 1, 1, 1, 1,    // 40-4F @ A B C D E F G H I J K L M N O
                                                                 3, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 3, 0, 2, 3,    // 50-5F P Q R S T U V W X Y Z [ \ ] ^ _
                                                                 0, 1, 2, 1, 2, 1, 1, 1, 1, 1, 1, 2, 1, 2, 2, 3,    // 60-6F ` a b c d e f g h i j k l m n o
                                                                 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 0, 0,    // 70-7F p q r s t u v w x y z { | } ~ .
                                                                 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,    // 80-8F . . . . . . . . . . . . . . . .
                                                                 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,    // 90-9F . . . . . . . . . . . . . . . .
                                                                 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,    // A0-AF . . . . . . . . . . . . . . . .
                                                                 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,    // B0-BF . . . . . . . . . . . . . . . .
                                                                 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,    // C0-CF . . . . . . . . . . . . . . . .
                                                                 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,    // D0-DF . . . . . . . . . . . . . . . .
                                                                 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,    // E0-EF . . . . . . . . . . . . . . . .
                                                                 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3}};  // F0-FF . . . . . . . . . . . . . . . .
        _bc4cp0 = WRT_wrd[ch];
        _pw += _pw + (_bc4cp0 ? 1 : 0);

        if (const auto pc{static_cast<uint8_t>(cx_ >> 8)}; (ch > 127) ||                  //
                                                           (Utilities::is_lower(ch)) ||   //
                                                           (Utilities::is_number(ch)) ||  //
                                                           (Utilities::is_number(pc) && ('.' == ch))) {
          word_ = Combine64(word_, ch);
        } else if (Utilities::is_upper(ch)) {
          word_ = Combine64(word_, Utilities::to_lower(ch));
        } else {
          word_ = 0;
        }

        const auto c4{cx_ & 0xFFFFFFFF};
        const auto c8{cx_ >> 32};
        const auto ctx{_is_binary ? ExeContext(_buf) : (cx_ & 0x0080FFFF)};
        hh_[0] = Finalise64(Hash(ctx), 32);
        hh_[1] = Finalise64(Hash(c4, WRT_mxr[static_cast<uint8_t>(cx_ >> 24)]), 32);
        hh_[2] = Finalise64(Hash(c4, c8 & 0x0000C0FF), 32);
        hh_[3] = Finalise64(Hash(c4, c8 & 0x00FEFFFF, WRT_mxr[static_cast<uint8_t>(cx_ >> 56)]), 32);
        hh_[4] = Finalise64(Combine64(word_, WRT_mxr[ch]), 32);
        cp_[0] = _t4b.get1x(0xE0, hh_[0]);  // 111 (7)
        cp_[1] = _t4a.get1x(0x60, hh_[1]);  // 011 (3)
        cp_[2] = _t4a.get3a(0xE0, hh_[2]);  // 111 (7)
        cp_[3] = _t4b.get3a(0x60, hh_[3]);  // 011 (3)
        cp_[4] = _t4b.get1x(0xE0, hh_[4]);  // 111 (7)

        _dmc.Update();
        _lzp.Update();
        _smm.Update();
        _txt.Update();

        if (const auto pos{_buf.Pos()}; 0 == (pos & (256 * 1024 - 1))) {
          if (((16 == dp_shift_) && (pos == (25 * 256 * 1024))) ||  // 22 or 25 based on enwik9 (little influence)
              ((15 == dp_shift_) && (pos == (4 * 256 * 1024))) ||   // 2 or 4 based on enwik9 (little influence)
              (14 == dp_shift_)) {
            ++dp_shift_;
            _mixer.ScaleUp();
          }
        }

        c0_ = 1;
      } break;
    }

    _dmc.Predict(bit);
    _smm.Predict(bit);

    uint32_t pr;

    if (32 == _buf(1)) {
      pr = (7 == bcount_) ? Predict_was32s(bit) : Predict_was32(bit);
    } else {
      pr = (7 == bcount_) ? Predict_not32s(bit) : Predict_not32(bit);
    }

    if (const auto [has_prediction, prediction]{_txt.Predict(bit)}; has_prediction) {
      assert((0x000 == prediction) || (0xFFF == prediction));  // Predicts only 0 or 1 with certainty of 100%
      _pt = pr = prediction;
    } else {
      _pt = 0x7FF;  // No prediction
    }
    assert(pr < 0x1000);
    return pr;
  }
};
Predict_t::~Predict_t() noexcept = default;

/**
 * @class Encoder_t
 * @brief Arithmetic coding
 *
 * Arithmetic coding, encoder and decoder
 */
class Encoder_t final : public iEncoder_t {
public:
  explicit Encoder_t(Buffer_t& __restrict buf, bool encode, File_t& file) noexcept
      : _stream{file},  //
        _predict{std::make_unique<Predict_t>(buf)} {
    if (!encode) {
      _x = _stream.get32();
    }

    std::fill(&smt_[0][0], &smt_[0][0] + sizeof(smt_) / sizeof(smt_[0][0]), 0x07FFFF);

    for (uint32_t i{6}; i--;) {
      int32_t* j{&smt_[0xF & (0x578046 >> (i * 4))][0]};
      uint8_t p1{state_table_y0_[i][0]};
      uint8_t p2{state_table_y0_[i][0]};
      uint8_t p3{state_table_y1_[i][0]};
      uint8_t p4{state_table_y1_[i][0]};
      p1 = state_table_y0_[i][p1];
      j[p1] = (0xFFFFF * 1) / 4;
      p2 = state_table_y1_[i][p2];
      j[p2] = (0xFFFFF * 2) / 4;
      p3 = state_table_y0_[i][p3];
      j[p3] = (0xFFFFF * 2) / 4;
      p4 = state_table_y1_[i][p4];
      j[p4] = (0xFFFFF * 3) / 4;
      uint8_t p5{p4};
      uint8_t p6{p1};
      for (auto z{5}; z < 70; ++z) {
        uint8_t px;
        // clang-format off
        px = p1; p1 = state_table_y0_[i][p1];                           if (p1 != px) { j[p1] = (0xFFFFF * (    1)) / z; }
        px = p2; p2 = state_table_y1_[i][p2];                           if (p2 != px) { j[p2] = (0xFFFFF * (z - 2)) / z; }
        px = p3; p3 = state_table_y0_[i][p3];                           if (p3 != px) { j[p3] = (0xFFFFF * (    2)) / z; }
        px = p4; p4 = state_table_y1_[i][p4];                           if (p4 != px) { j[p4] = (0xFFFFF * (z - 1)) / z; }
        px = p5; p5 = state_table_y0_[i][p5]; if (p5 < px) { p5 = px; } if (p5 != px) { j[p5] = (0xFFFFF * (    3)) / z; }
        px = p6; p6 = state_table_y1_[i][p6]; if (p6 < px) { p6 = px; } if (p6 != px) { j[p6] = (0xFFFFF * (z - 3)) / z; }
        // clang-format on
      }
    }

    memcpy(&smt_[0x1], &smt_[0x0], smt_[0x0].size());
    memcpy(&smt_[0x2], &smt_[0x0], smt_[0x0].size());
    memcpy(&smt_[0x3], &smt_[0x0], smt_[0x0].size());
    memcpy(&smt_[0x9], &smt_[0x8], smt_[0x8].size());
    memcpy(&smt_[0xA], &smt_[0x7], smt_[0x7].size());
    memcpy(&smt_[0xB], &smt_[0x7], smt_[0x7].size());
  }
  ~Encoder_t() noexcept override;

  Encoder_t() = delete;
  Encoder_t(const Encoder_t&) = delete;
  Encoder_t(Encoder_t&&) = delete;
  auto operator=(const Encoder_t&) -> Encoder_t& = delete;
  auto operator=(Encoder_t&&) -> Encoder_t& = delete;

  void Compress(const int32_t c) noexcept final {
    for (auto n{8}; n--;) {
      Code((c >> n) & 1);
    }
  }

  [[nodiscard]] auto Decompress() noexcept -> int32_t final {
    auto c{0};
    for (auto n{8}; n--;) {
      c += c + Code();
    }
    return c;
  }

  void CompressN(const int32_t N, const int64_t c) noexcept final {
    for (auto n{N}; n--;) {
      Code((c >> n) & 1);
    }
  }

  [[nodiscard]] auto DecompressN(const int32_t N) noexcept -> int64_t final {
    int64_t c{0};
    for (auto n{N}; n--;) {
      c += c + Code();
    }
    return c;
  }

  void CompressVLI(int64_t c) noexcept final {
    while (c > 0x7F) {
      Compress(static_cast<int32_t>(0x80 | (0x7F & c)));
      c >>= 7;
    }
    Compress(static_cast<int32_t>(c));
  }

  [[nodiscard]] auto DecompressVLI() noexcept -> int64_t final {
    int64_t c{0};
    int32_t k{0};
    int32_t b{0};
    do {
      b = Decompress();
      c |= static_cast<int64_t>(0x7F & b) << k;
      k += 7;
    } while ((k < 127) && (0x80 & b));
    return c;
  }

  void Flush() noexcept final {
    // Flush first unequal byte of range
    _stream.putc(static_cast<int32_t>(_low >> 24));
    _stream.Flush();
  }

  void SetBinary(const bool is_binary) noexcept final {
    _predict->SetBinary(is_binary);
  }
  void SetDataPos(const int64_t data_pos) noexcept final {
    _predict->SetDataPos(data_pos);
  }
  void SetStart(const bool state) noexcept final {
    _predict->SetStart(state);
  }
  void SetDicStartOffset(const int64_t dic_start_offset) noexcept final {
    _predict->SetDicStartOffset(dic_start_offset);
  }
  void SetDicEndOffset(const int64_t dic_end_offset) noexcept final {
    _predict->SetDicEndOffset(dic_end_offset);
  }
  void SetDicWords(const int64_t number_of_words) noexcept final {
    _predict->SetDicWords(number_of_words);
  }

private:
  static constexpr auto _mask{UINT32_C(0xFF000000)};
  File_t& _stream;
  std::unique_ptr<Predict_t> _predict;
  uint32_t _high{UINT32_C(~0)};
  uint32_t _low{0};
  uint32_t _x{0};
  uint32_t _pr{0x7FFF};  // Prediction 0x0000..0xFFFF

  [[nodiscard]] ALWAYS_INLINE constexpr auto Rescale() const noexcept -> uint32_t {
    assert(_pr < 0x10000);
    assert(_high > _low);
#if 0
    const auto delta{_high - _low};
    const auto mid{_low + ((delta / 0x10000) * _pr) + (((delta & 0xFFFF) * _pr) / 0x10000)};
#else
    const uint64_t delta{_high - _low};
    const auto mid{_low + static_cast<uint32_t>((delta * _pr) / 0x10000)};
#endif
    assert(_high > mid);
    assert(mid >= _low);
    return mid;
  }

  void Code(const bool bit) noexcept {
    if (const auto mid{Rescale()}; bit) {
      _high = mid;
    } else {
      _low = mid + 1;
    }
    while (!(_mask & (_low ^ _high))) {  // Shift out identical leading bytes
      _stream.putc(static_cast<int32_t>(_high >> 24));
      _high = (_high << 8) | 0xFF;
      _low <<= 8;
    }
    _pr = _predict->Next(bit);  // Update models and Predict next bit probability
  }

  [[nodiscard]] auto Code() noexcept -> bool {
    bool bit;
    if (const auto mid{Rescale()}; _x <= mid) {
      _high = mid;
      bit = true;
    } else {
      _low = mid + 1;
      bit = false;
    }
    while (!(_mask & (_low ^ _high))) {  // Shift out identical leading bytes
      _high = (_high << 8) | 0xFF;
      _low <<= 8;
      _x = (_x << 8) | (_stream.getc() & 0xFF);  // EOF is OK
    }
    _pr = _predict->Next(bit);  // Update models and Predict next bit probability
    return bit;
  }
};
Encoder_t::~Encoder_t() noexcept = default;
iEncoder_t::~iEncoder_t() noexcept = default;

/**
 * @class Monitor_t
 * @brief Monitor interface for tracking encode/decode progress
 *
 * Monitor interface for tracking encode/decode progress
 */
class Monitor_t final : public iMonitor_t {
public:
  explicit Monitor_t(const File_t& in, const File_t& out, const int64_t workLength, const int64_t layoutLength) noexcept
      : _in{in},  //
        _out{out},
        _workLength{workLength},
        _layoutLength{layoutLength} {}
  ~Monitor_t() noexcept override;

  Monitor_t(const Monitor_t&) = delete;
  Monitor_t(Monitor_t&&) = delete;
  auto operator=(const Monitor_t&) -> Monitor_t& = delete;
  auto operator=(Monitor_t&&) -> Monitor_t& = delete;

  [[nodiscard]] auto InputLength() const noexcept -> int64_t final {
    return _in.Position();
  }
  [[nodiscard]] auto OutputLength() const noexcept -> int64_t final {
    return _out.Position();
  }
  [[nodiscard]] auto WorkLength() const noexcept -> int64_t final {
    return _workLength;
  }
  [[nodiscard]] auto LayoutLength() const noexcept -> int64_t final {
    return _layoutLength;
  }

private:
  const File_t& _in;
  const File_t& _out;
  const int64_t _workLength;
  const int64_t _layoutLength;
};
Monitor_t::~Monitor_t() noexcept = default;

namespace {
  [[nodiscard]] auto Checksum(const uint8_t* __restrict data, size_t len) noexcept -> uint8_t {
    uint8_t sum{0};
    while (len--) {
      sum = static_cast<uint8_t>(sum + *data++);
    }
    return sum;
  }

  constexpr std::array<const char, 17> short_options{{"cdhvV0123456789x"}};
  constexpr std::array<const struct option, 10> long_options{{{"verbose", no_argument, &verbose_, 1},     //
                                                              {"brief", no_argument, &verbose_, 0},       //
                                                              {"compress", no_argument, nullptr, 'c'},    //
                                                              {"decompress", no_argument, nullptr, 'd'},  //
                                                              {"best", no_argument, nullptr, '9'},        //
                                                              {"fast", no_argument, nullptr, '0'},        //
                                                              {"help", no_argument, nullptr, 'h'},        //
                                                              {"version", no_argument, nullptr, 'V'},     //
#if defined(TUNING) || defined(GENERATE_SQUASH_STRETCH)
                                                              {"xx", required_argument, nullptr, 'x'},
#else
                                                              {nullptr, no_argument, nullptr, 0},
#endif
                                                              {nullptr, no_argument, nullptr, 0}}};
};  // namespace

auto main(int32_t argc, char* const argv[]) -> int32_t {
  // clang-format off
  std::set_new_handler([]() { fprintf(stderr, "\nFailed to allocate memory!"); std::abort(); });
  std::set_terminate  ([]() { fprintf(stderr, "\nUnhandled exception");        std::abort(); });
  // clang-format on

  fprintf(stdout,
          "Moruga compressor (C) 2023, M.W. Hessel.\n"
          "Based on PAQ compressor series by M. Mahoney.\n"
          "Free under GPL, https://www.gnu.org/licenses/\n"
          "https://github.com/the-m-master/Moruga/\n");

  level_ = DEFAULT_OPTION;
  bool help{false};
  bool compress{true};

  for (int32_t command{0}; -1 != (command = getopt_long(argc, argv, short_options.data(), long_options.data(), nullptr));) {
    switch (command) {  // clang-format off
      case 0:                     break;
      case 'h':                          // --help
      default: help = true;       break;
      case 'c': compress = true;  break; // --compress
      case 'd': compress = false; break; // --decompress
      case 'v': verbose_ = 1;     break; // --verbose
      case 'V': return EXIT_SUCCESS;     // --version
      case '0':                          // --fast
      case '1': case '2': case '3': case '4':
      case '5': case '6': case '7': case '8':
      case '9': {                        // --best
        try {
          const auto level{std::clamp((std::abs)(std::stoi(argv[optind - 1], nullptr, 10)), 0, 12)};
          level_ = level;
          compress = true;
        } catch(...) {}
      } break;
#if defined(GENERATE_SQUASH_STRETCH)
      case 'x': {                        // --xx
        const double value{double(std::stoi(optarg, nullptr, 10)) / 10.0};
        fprintf(stdout, "\nValue : %g\n", value);
        _squash = new Squash_t(598.0); // 756.1, 598.0
        _stretch = new Stretch_t(738.2);    // 738.2
      } break;
#elif defined(TUNING)
      case 'x': {
#if 0
         {
           File_t result("tra.bin", "rb");
           result.Read(tra, sizeof(tra));
         }
          const auto XX{ std::stoul(optarg, nullptr, 16)};
          const auto idx{uint16_t(XX>>16)};
          const auto chg{uint16_t(XX&0xFFFF)};
          tra[idx]=chg;
#else
          XX = std::stol(optarg, nullptr, 10);
          fprintf(stdout, "\nValue : %" PRIu32 "\n", XX);
#endif
      } break;
#endif
    }  // clang-format on
  }
  while (optind < argc) {
    if (nullptr != inFileName_) {
      outFileName_ = argv[optind++];
    } else {
      inFileName_ = argv[optind++];
    }
  }

  if (help || (nullptr == inFileName_) || (nullptr == outFileName_)) {
    static constexpr std::array<const uint32_t, 11> use{{85, 115, 177, 303, 554, 1057, 1933, 3687, 7193, 14207, 27209}};  // TODO verify this base on ewik8
    fprintf(stderr,                                                                                                       // clang-format off
            "\nUsage: Moruga <option> <infile> <outfile>\n\n"
            "  -c, --compress   Compress a file (default)\n"
            "  -d, --decompress Decompress a file\n"
            "  -h, --help       Display this short help and exit\n"
            "  -v, --verbose    Verbose mode\n"
            "  -V, --version    Display the version number and exit\n"
            "  -0 ... -10       Uses about %" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",\n"
            "                   %" PRIu32 ",%" PRIu32 ",%" PRIu32 " or %" PRIu32 " MiB memory\n"
            "                   Default is option %" PRIu32 ", uses %" PRIu32 " MiB of memory\n", use[0], use[1], use[2], use[3], use[4], use[5], use[6],
                                                                                                  use[7], use[8], use[9], use[10],
                                                                                                  DEFAULT_OPTION, use[DEFAULT_OPTION]);
    // clang-format on
    return EXIT_SUCCESS;
  }

#if defined(__linux__) || defined(_MSC_VER)
  if (!strcmp(inFileName_, outFileName_)) {
#else
  if (!strcasecmp(inFileName_, outFileName_)) {
#endif
    fprintf(stderr, "\n<infile> and <outfile> can not be identical!");
    return EXIT_FAILURE;
  }

  File_t infile{inFileName_, "rb"};
  File_t outfile{outFileName_, "wb+"};  // write/read otherwise decode will fail

  const auto originalLength{infile.Size()};

  const auto start_time{std::chrono::high_resolution_clock::now()};

  if (compress) {
    fprintf(stdout, "\nEncoding file '%s' ... with memory option %d\n", inFileName_, level_);

#if !defined(DISABLE_TEXT_PREP)
    File_t tmp{};  // {"_tmp_.txt", "wb+"};
    const auto iLen{infile.Size()};
    if (iLen <= 0) {
      fprintf(stderr, "\nFile '%s' has no length, encoding not possible!", inFileName_);
      return EXIT_FAILURE;
    }
    const auto [data_pos, dic_start_offset, dic_end_offset, dic_words]{EncodeText(infile, tmp)};
    assert((data_pos > 0) && (data_pos < 0x07FFFFFF));
    assert(dic_start_offset >= 0);
    assert(dic_end_offset >= 0);
    assert(dic_words >= 0);
    const auto oLen{tmp.Size()};
    const auto reduction{((iLen - oLen) * 100) / iLen};
    // Achieve at least 25% reduction, otherwise the chance of a worse end result is larger
    if (reduction >= 25) {
      infile.Close();
      infile = tmp;
      tmp = nullptr;
    } else {
      fprintf(stdout, "<binary file>\n");
      tmp.Close();
    }
#else
    const auto data_pos{INT64_C(0)};
    const int64_t iLen{infile.Size()};
    if (iLen <= 0) {
      fprintf(stderr, "\nFile '%s' has no length, encoding not possible!", inFileName_);
      return EXIT_FAILURE;
    }
#endif
    infile.Rewind();

    assert((level_ >= 0) && (level_ <= 12));
    outfile.putc(level_);  // Write memory level

    Buffer_t _buf{};
    Encoder_t en{_buf, true, outfile};

    // Original file length
    en.CompressVLI(iLen);

    // Increasing the buffer size above the file length is not useful
    _buf.Resize(static_cast<uint64_t>(iLen), MEM());

    // File length after text preparation (successful or not)
    const auto len{infile.Size()};
    en.CompressVLI(len);

    const bool is_txtprep{iLen != len};  // Set if there was text preparation done

    if (is_txtprep) {
      en.CompressVLI(data_pos);  // Start point text preparation
#if !defined(DISABLE_TEXT_PREP)
      en.CompressVLI(dic_start_offset);
      en.CompressVLI(dic_end_offset);
      en.CompressVLI(dic_words);
#endif

      en.SetDataPos(data_pos);
#if !defined(DISABLE_TEXT_PREP)
      en.SetDicStartOffset(dic_start_offset);
      en.SetDicEndOffset(dic_end_offset);
      en.SetDicWords(dic_words);
#endif
    }

    uint8_t csum{Checksum(reinterpret_cast<const uint8_t*>(&originalLength), sizeof(originalLength))};
    csum = static_cast<uint8_t>(csum + Checksum(reinterpret_cast<const uint8_t*>(&len), sizeof(len)));
    en.Compress(csum);

    const Monitor_t monitor{infile, outfile, len, iLen};
    const Progress_t progress{"ENC", true, monitor};

    en.SetBinary(!is_txtprep);
    en.SetStart(is_txtprep);

#if defined(DEBUG_WRITE_ANALYSIS_ENCODER)
    File_t analysis("Analysis.csv", "wb");
#endif

    if (is_txtprep) {
#if defined(DEBUG_WRITE_ANALYSIS_ENCODER)
      int64_t pos{0};
#endif
      for (int32_t ch; EOF != (ch = infile.getc());) {
        en.Compress(ch);

#if defined(DEBUG_WRITE_ANALYSIS_ENCODER)
        if (!(++pos % (1 << 12))) {
          fprintf(analysis, "%" PRIi64 ",%" PRIi64 "\n", pos, outfile.Position());
        }
#endif
      }
    } else {
#if defined(DEBUG_WRITE_ANALYSIS_ENCODER)
      int64_t pos{0};
#endif
      Filter_t filter{_buf, len, infile, &en};

      for (int32_t ch; EOF != (ch = infile.getc());) {
        if (filter.Scan(ch)) {
          continue;
        }
        en.Compress(ch);

#if defined(DEBUG_WRITE_ANALYSIS_ENCODER)
        if (!(++pos % (1 << 12))) {
          fprintf(analysis, "%" PRIi64 ",%" PRIi64 "\n", pos, outfile.Position());
        }
#endif
      }
    }
    en.Flush();
  } else {
    if (infile.Size() <= 0) {
      fprintf(stderr, "\nFile '%s' has no length, decoding not possible!", inFileName_);
      return EXIT_FAILURE;
    }

    level_ = infile.getc();  // Read memory level
    if (!((level_ >= 0) && (level_ <= 12))) {
      fprintf(stderr, "\nFile '%s' is damaged, decoding not possible!", inFileName_);
      return EXIT_FAILURE;
    }

    fprintf(stdout, "\nDecoding file '%s' ... with memory option %d\n", inFileName_, level_);

    Buffer_t _buf{};
    Encoder_t en{_buf, false, infile};

    // Original file length
    const auto iLen{en.DecompressVLI()};

    // Increasing the buffer size above the file length is not useful
    _buf.Resize(static_cast<uint64_t>(iLen), MEM());

    // File length after text preparation (successful or not)
    auto len{en.DecompressVLI()};

    const bool is_txtprep{iLen != len};  // Set if there was text preparation done

    if (is_txtprep) {
      const auto data_pos{en.DecompressVLI()};  // Start point text preparation
      assert((data_pos >= 0) && (data_pos < 0x07FFFFFF));
      en.SetDataPos(data_pos);

#if !defined(DISABLE_TEXT_PREP)
      const auto dic_start_offset{en.DecompressVLI()};
      const auto dic_end_offset{en.DecompressVLI()};
      const auto dic_words{en.DecompressVLI()};

      assert(dic_start_offset >= 0);
      assert(dic_end_offset >= 0);
      assert(dic_words >= 0);

      en.SetDicStartOffset(dic_start_offset);
      en.SetDicEndOffset(dic_end_offset);
      en.SetDicWords(dic_words);
#endif
    }

    uint8_t csum{Checksum(reinterpret_cast<const uint8_t*>(&iLen), sizeof(iLen))};
    csum = static_cast<uint8_t>(csum + Checksum(reinterpret_cast<const uint8_t*>(&len), sizeof(len)));
    const auto valid{static_cast<uint8_t>(en.Decompress())};
    if (csum != valid) {
      fprintf(stderr, "\nFile '%s' is damaged, decoding not possible!", inFileName_);
      return EXIT_FAILURE;
    }

    const Monitor_t monitor{infile, outfile, len, iLen};

    {
      const Progress_t progress{"DEC", false, monitor};

      en.SetBinary(!is_txtprep);
      en.SetStart(is_txtprep);

      if (is_txtprep) {
        for (int64_t pos{0}; pos < len; ++pos) {
          auto ch{en.Decompress()};
          outfile.putc(ch);
        }
      } else {
        Filter_t filter{_buf, len, outfile, nullptr};

        for (int64_t pos{0}; pos < len; ++pos) {
          auto ch{en.Decompress()};
          if (filter.Scan(ch, pos)) {
            continue;
          }
          assert(outfile.Position() == pos);
          outfile.putc(ch);
        }
      }
    }

    if (iLen != len) {
      outfile.Rewind();
      File_t tmp{};  // {"_tmp_.txt", "wb+"};
      const auto oLen{DecodeText(outfile, tmp)};
      if (oLen > len) {
        tmp.Rewind();
        outfile.Rewind();

        auto i{oLen};
        while (i--) {
          outfile.putc(tmp.getc());
        }
        len = oLen;
      }
      tmp.Close();
    }
  }

  int64_t bytes_done{0};
  if (compress) {
    bytes_done = originalLength;
    fprintf(stdout, "\nEncoded from %" PRId64 " bytes to %" PRId64 " bytes.", bytes_done, outfile.Size());
#if 1                                             // TODO clean-up
    if (INT64_C(1000000000) == originalLength) {  // enwik9
      const auto improvement{INT64_C(135412689) - outfile.Size()};
      fprintf(stdout, "\nImprovement %" PRId64 " bytes\n", improvement);
    } else if (INT64_C(100000000) == originalLength) {  // enwik8
      const auto improvement{INT64_C(17168802) - outfile.Size()};
      fprintf(stdout, "\nImprovement %" PRId64 " bytes\n", improvement);
    }
#endif
  } else {
    bytes_done = outfile.Size();
    fprintf(stdout, "\nDecoded from %" PRId64 " bytes to %" PRId64 " bytes.", originalLength, bytes_done);
  }
  fprintf(stdout, "\nMaximum memory used: %" PRIu32 " KiB", Progress_t::PeakMemoryUse());

  const auto end_time{std::chrono::high_resolution_clock::now()};
  const std::chrono::high_resolution_clock::time_point delta_time{end_time - start_time};
  const auto duration_ns{double(std::chrono::duration_cast<std::chrono::nanoseconds>(delta_time.time_since_epoch()).count())};

  fprintf(stdout, "\nTotal time %3.1f sec (%3.0f ns/byte)\n\n", duration_ns / 1e9, round(duration_ns / double(bytes_done)));

  return EXIT_SUCCESS;
}
