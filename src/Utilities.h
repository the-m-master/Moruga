/* Utilities for Moruga file compressor
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
#pragma once

#include <cstdint>

#if !defined(NDEBUG)
#  define ALWAYS_INLINE
#else
#  if defined(_MSC_VER)
#    define ALWAYS_INLINE __forceinline
#  elif defined(__GNUC__)
#    define ALWAYS_INLINE inline __attribute__((always_inline))
#  else
#    define ALWAYS_INLINE inline
#  endif
#endif

#if defined(_MSC_VER)
#  define strncasecmp _strnicmp
#  define strcasecmp _stricmp
#endif

namespace Utilities {
  // Fibonacci constants for a number of bits b
  // floor(2^(b)/ ((1 + sqrt(5))/2))
  // 2^8 = 158
  // 2^16 = 40503
  // 2^32 = 2654435769
  // 2^64 = 11400714819323198485
  // 2^128 = 210306068529402873165736369884012333108
  // 2^256 = 71563446777022297856526126342750658392501306254664949883333486863006233104021
  // 2^512 = 8286481015334893988907527251732611664457280877896990125350747801032912124181934735572335005532987901856694870697621088413914768940958605061563703415234102

  // Golden ratio of 2^32 (not a prime)
  static constexpr auto PHI32{UINT32_C(0x9E3779B9)};  // 2654435769

  // Golden ratio of 2^64 (not a prime)
  static constexpr auto PHI64{UINT64_C(0x9E3779B97F4A7C15)};  // 11400714819323198485

  template <typename T>
  ALWAYS_INLINE constexpr auto is_number(const T ch) noexcept -> bool {
    return (ch >= '0') && (ch <= '9');
  }

  template <typename T>
  ALWAYS_INLINE constexpr auto is_upper(const T ch) noexcept -> bool {
    return (ch >= 'A') && (ch <= 'Z');
  }

  template <typename T>
  ALWAYS_INLINE constexpr auto is_lower(const T ch) noexcept -> bool {
    return (ch >= 'a') && (ch <= 'z');
  }

  template <typename T>
  ALWAYS_INLINE constexpr auto to_upper(const T ch) noexcept -> T {
    return is_lower(ch) ? ch - 'a' + 'A' : ch;
  }

  template <typename T>
  ALWAYS_INLINE constexpr auto to_lower(const T ch) noexcept -> T {
    return is_upper(ch) ? ch - 'A' + 'a' : ch;
  }
}  // namespace Utilities
