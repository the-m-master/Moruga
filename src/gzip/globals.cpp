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
 *===============================================================================
 */
#include <array>
#include <cstdint>
#include <cstdio>
#include "gzip.h"

namespace gzip {
  const uint8_t* imem{nullptr};
  FILE* ifd{nullptr};
  FILE* ofd{nullptr};
  uint32_t level{9};
  int32_t rsync{0};
  std::array<uint8_t, INBUFSIZ> inbuf{};
  std::array<uint8_t, OUTBUFSIZ> outbuf{};
  std::array<uint8_t, WSIZE + WSIZE> window{};
  uint32_t bytes_in{0};
  uint32_t bytes_out{0};
  uint32_t ifile_size{0};
  uint32_t inptr{0};
  uint32_t insize{0};
  uint32_t outcnt{0};
  void* this_pointer{nullptr};
  write_buffer_t omem{nullptr};
};  // namespace gzip
