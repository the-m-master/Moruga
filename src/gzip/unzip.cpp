/* Gzip is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Gzip is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file LICENSE.
 * If not, see <https://www.gnu.org/licenses/>
 *
 * https://www.gnu.org/software/gzip/
 */
#include <cstdint>
#include <cstdio>
#include "gzip.h"

namespace gzip {
  auto unzip(FILE* const in, FILE* const out, uint32_t& ilength) noexcept -> int32_t {
    if (in && out && (ilength > 0)) {
      ifd = in;
      ofd = out;
      imem = nullptr;
      ifile_size = ilength;
      omem = nullptr;
      const int32_t res{inflate()};
      ilength = bytes_in;
      return res;
    }

    return GZip_ERROR;
  }

  auto unzip(const uint8_t* const in, const uint32_t ilength, write_buffer_t out, void* const ptr) noexcept -> int32_t {
    if (in && (ilength > 0) && out) {
      ifd = nullptr;
      ofd = nullptr;
      imem = in;
      ifile_size = ilength;
      omem = out;
      this_pointer = ptr;
      const int32_t res{inflate()};
      omem = nullptr;
      return res;
    }
    return GZip_ERROR;
  }
};  // namespace gzip
