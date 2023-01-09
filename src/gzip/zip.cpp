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
  auto FileRead(void* buf, uint32_t size) noexcept -> int32_t {
    if (size < insize) {
      insize -= size;
    } else {
      size = insize;
      insize = 0;
    }
    const uint32_t len{read_buffer(ifd, buf, size)};
    if (0 == len) {
      return int32_t(len);
    }
    if (uint32_t(EOF) == len) {
      return -1;  // read_error();
    }
    bytes_in += len;
    return int32_t(len);
  }

  auto Zip(FILE* const in, const uint32_t size, FILE* const out, const int32_t clevel) noexcept -> int32_t {
    uint16_t attr = 0; /* ASCII/binary flag */

    ifd = in;
    ofd = out;
    outcnt = 0;
    insize = size;
    level = clevel;
    bytes_in = 0;

    BitsInit();
    CtInit(&attr);
    Deflate(level);
    flush_outbuf();
    return GZip_OK;
  }
};  // namespace gzip
