/*===============================================================================
 * Moruga project
 *===============================================================================
 * Copyright (c) 2019-2023 Marwijn Hessel
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
 ================================================================================*/

#pragma once

#if !defined(CLANG_TIDY)
#  define USE_BYTELL_HASH_MAP  // Enable the fastest hash table by Malte Skarupke
#endif

#if defined(USE_BYTELL_HASH_MAP)
#  if defined(_MSC_VER)
#    undef max
#    undef min
#    include <stdexcept>
#  else
#    pragma GCC diagnostic push
#    pragma GCC diagnostic ignored "-Waggregate-return"
#    pragma GCC diagnostic ignored "-Wc++98-c++11-compat-binary-literal"
#    pragma GCC diagnostic ignored "-Wc++98-compat-pedantic"
#    pragma GCC diagnostic ignored "-Wconversion"
#    pragma GCC diagnostic ignored "-Wdeprecated"
#    pragma GCC diagnostic ignored "-Weffc++"
#    pragma GCC diagnostic ignored "-Wpadded"
#    pragma GCC diagnostic ignored "-Wshadow"
#    pragma GCC diagnostic ignored "-Wsign-conversion"
#  endif
#  include "bytell_hash_map.hpp"
#  if !defined(_MSC_VER)
#    pragma GCC diagnostic pop
#  endif
#else
#  include <unordered_map>
#endif

#if defined(USE_BYTELL_HASH_MAP)
using map_string2uint_t = ska::bytell_hash_map<std::string, uint32_t>;
using map_uint2string_t = ska::bytell_hash_map<uint32_t, std::string>;
#else
using map_string2uint_t = std::unordered_map<std::string, uint32_t>;
using map_uint2string_t = std::unordered_map<uint32_t, std::string>;
#endif
