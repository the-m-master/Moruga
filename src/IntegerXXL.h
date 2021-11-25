/* IntegerXXL, large integer definition in128_t/uint128_t
 *
 * Copyright (c) 2021 Marwijn Hessel
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
#ifndef _INTEGER_XXL_HDR_
#define _INTEGER_XXL_HDR_

#include <cassert>
#include <cinttypes>

#if defined(__SIZEOF_INT128__)
typedef __int128 int128_t;
typedef unsigned __int128 uint128_t;
#else
// TODO do something for VS
#endif  // __SIZEOF_INT128__

// clang-format off
namespace integer_xxl {
  constexpr auto hexval(char c) noexcept -> uint8_t {
    return (c >= 'a') ? uint8_t(10 + c - 'a') : (c >= 'A') ? uint8_t(10 + c - 'A') : uint8_t(c - '0');
  }

  template <int BASE, uint128_t V>
  constexpr auto largeIntergerEvaluation() noexcept -> uint128_t {
    return V;
  }

  template <int BASE, uint128_t V, char C, char... Cs>
  constexpr auto largeIntergerEvaluation() noexcept -> uint128_t {
    static_assert((16 != BASE) || sizeof...(Cs) <=  32 - 1, "Literal too large for BASE=16");
    static_assert((10 != BASE) || sizeof...(Cs) <=  39 - 1, "Literal too large for BASE=10");
    static_assert(( 8 != BASE) || sizeof...(Cs) <=  44 - 1, "Literal too large for BASE=8");
    static_assert(( 2 != BASE) || sizeof...(Cs) <= 128 - 1, "Literal too large for BASE=2");
    return largeIntergerEvaluation<BASE, BASE * V + hexval(C), Cs...>();
  }

  template <char... Cs>
  struct LitEval                  { static constexpr auto eval() noexcept -> uint128_t { return largeIntergerEvaluation<10, 0, Cs...>(); } };

  template <char... Cs>
  struct LitEval<'0', 'x', Cs...> { static constexpr auto eval() noexcept -> uint128_t { return largeIntergerEvaluation<16, 0, Cs...>(); } };

  template <char... Cs>
  struct LitEval<'0', 'b', Cs...> { static constexpr auto eval() noexcept -> uint128_t { return largeIntergerEvaluation<2, 0, Cs...>();  } };

  template <char... Cs>
  struct LitEval<'0', Cs...>      { static constexpr auto eval() noexcept -> uint128_t { return largeIntergerEvaluation<8, 0, Cs...>();  } };

  template <char... Cs>
  constexpr auto operator"" _xxl() noexcept -> uint128_t {
    return LitEval<Cs...>::eval();
  }
}  // integer_xxl

template <char... Cs>
constexpr auto operator"" _xxl() noexcept -> uint128_t {
  return ::integer_xxl::operator"" _xxl<Cs...>();
}

static_assert(     0_xxl == 0x00, "IntegerXXL error");
static_assert(   0b0_xxl == 0x00, "IntegerXXL error");
static_assert(    00_xxl == 0x00, "IntegerXXL error");
static_assert(   0x0_xxl == 0x00, "IntegerXXL error");

static_assert(     1_xxl == 0x01, "IntegerXXL error");
static_assert(   0b1_xxl == 0x01, "IntegerXXL error");
static_assert(    01_xxl == 0x01, "IntegerXXL error");
static_assert(   0x1_xxl == 0x01, "IntegerXXL error");

static_assert(     2_xxl == 0x02, "IntegerXXL error");
static_assert(  0b10_xxl == 0x02, "IntegerXXL error");
static_assert(    02_xxl == 0x02, "IntegerXXL error");
static_assert(   0x2_xxl == 0x02, "IntegerXXL error");

static_assert(     9_xxl == 0x09, "IntegerXXL error");
static_assert(0b1001_xxl == 0x09, "IntegerXXL error");
static_assert(   011_xxl == 0x09, "IntegerXXL error");
static_assert(   0x9_xxl == 0x09, "IntegerXXL error");

static_assert(    10_xxl == 0x0A, "IntegerXXL error");
static_assert(   0xa_xxl == 0x0A, "IntegerXXL error");
static_assert(   0xA_xxl == 0x0A, "IntegerXXL error");

static_assert(0xABCDEF_xxl == 0xABCDEF, "IntegerXXL error");
static_assert(1122334455667788_xxl == 1122334455667788LLu, "IntegerXXL error");
static_assert(0x80000000000000000000000000000000_xxl >> 126 == 0b10, "IntegerXXL error");
static_assert(0x80000000000000000000000000000000_xxl >> 127 == 0b01, "IntegerXXL error");
static_assert(0xF000000000000000B000000000000000_xxl > 0xB000000000000000, "IntegerXXL error");

// clang-format on

#endif  // _INTEGER_XXL_HDR_
