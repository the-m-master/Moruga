/* Copyright (c) 2019-2022 Marwijn Hessel
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
class iEncoder_t;

auto EncodeGZip(File_t& in, const int64_t size, File_t& out) noexcept -> bool;

auto DecodeEncodeCompare(File_t& stream, iEncoder_t* const coder, const int64_t safe_pos, const int64_t compressed_data_length, const uint32_t uncompressed_data_length) noexcept -> int64_t;
