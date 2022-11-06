/* TxtPrep5, is a text preparation for text encoding/decoding
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
class File_t;

static constexpr auto TP5_ESCAPE_CHAR{4};     // 0x04
static constexpr auto TP5_QUOTING_CHAR{42};   // 0x2A *
static constexpr auto TP5_SEPARATE_CHAR{20};  // 0x14

/**
 * Encode (text) data
 * @param in Reference to input stream
 * @param out Reference to output stream
 * @return Position where encoded data starts (zero when failure)
 */
auto encode_txt(File_t& in, File_t& out) noexcept -> int64_t;

/**
 * Decode (text) data
 * @param in Reference to input stream
 * @param out Reference to output stream
 * @return Original file length
 */
auto decode_txt(File_t& in, File_t& out) noexcept -> int64_t;
