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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file LICENSE.
 * If not, see <https://www.gnu.org/licenses/>
 */
#ifndef UTILITIES_HDR
#define UTILITIES_HDR

#include <cstdint>

#if !defined(NDEBUG)
#define ALWAYS_INLINE
#else
#if defined(_MSC_VER)
#define ALWAYS_INLINE __forceinline
#define __restrict__ __restrict
#define strcasecmp stricmp
#elif defined(__GNUC__)
#define ALWAYS_INLINE inline __attribute__((always_inline))
#else
#define ALWAYS_INLINE inline
#endif
#endif

namespace Utilities {

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

#endif /* UTILITIES_HDR */
