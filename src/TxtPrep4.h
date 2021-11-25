/* TxtPrep4, is a text preparation for text encoding/decoding
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
#ifndef TEXT_PREPARATION4_HDR
#define TEXT_PREPARATION4_HDR

#include <cstdint>
class File_t;

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

#endif /* TEXT_PREPARATION4_HDR */
