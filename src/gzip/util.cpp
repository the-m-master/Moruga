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
#include <array>
#include <cerrno>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "gzip.h"

namespace gzip {
  /* ===========================================================================
   * Fill the input buffer. This is called only when the buffer is empty.
   */
  auto fill_inbuf(int32_t eof_ok) noexcept -> int32_t {
    /* Read as much as possible */
    insize = 0;
    do {
      const uint32_t len = read_buffer(ifd, &inbuf[insize], INBUFSIZ - insize);
      if (len == 0) {
        break;
      }
      if (len == uint32_t(EOF)) {  // read_error();
        break;
      }
      insize += len;
    } while (insize < INBUFSIZ);

    if (insize == 0) {
      if (eof_ok) {
        return EOF;
      }
      flush_window();
      errno = 0;
      return 0;  // read_error();
    }
    bytes_in += insize;
    inptr = 1;
    return inbuf[0];
  }

  /* Like the standard read function, except do not attempt to read more
     than INT_MAX bytes at a time.  */
  auto read_buffer(FILE* fd, void* buf, uint32_t cnt) noexcept -> uint32_t {
    if (INT_MAX < cnt) {
      cnt = INT_MAX;
    }
#if 0
    if ((bytes_in + insize) < ifile_size) {
      if (cnt > ifile_size) {
        cnt = ifile_size;
      }
    } else {
      cnt = 0;
    }
#endif
    if (fd) {
      return uint32_t(fread(buf, sizeof(char), cnt, fd));
    }
    if (imem && ((bytes_in + insize) < ifile_size)) {
      if ((bytes_in + cnt) >= ifile_size) {
        cnt = ifile_size - bytes_in;
      }
      memcpy(buf, imem, cnt);
      imem += cnt;
      return cnt;
    }
    return 0;  // EOF
  }

  namespace {
    auto write_buffer(FILE* fd, void* buf, uint32_t cnt) noexcept -> uint32_t {
      if (INT_MAX < cnt) {
        cnt = INT_MAX;
      }
      if (fd) {
        return uint32_t(fwrite(buf, sizeof(char), cnt, fd));
      }
      if (omem) {
        bytes_out += cnt;
        return omem(buf, cnt, this_pointer);
      }
      return 0;  // Failure
    }

    void write_buf(FILE* fd, void* buf, uint32_t cnt) noexcept {
      bytes_out += cnt;

      uint32_t n{0};
      while ((n = write_buffer(fd, buf, cnt)) != cnt) {
        if (n == uint32_t(EOF)) {
          return;  // write_error();
        }
        cnt -= n;
        buf = static_cast<void*>(static_cast<char*>(buf) + n);
      }
    }
  };  // namespace

  /* ===========================================================================
   * Write the output buffer outbuf[0..outcnt-1] and update bytes_out.
   * (used for the compressed data only)
   */
  void flush_outbuf() noexcept {
    if (outcnt == 0) {
      return;
    }
    write_buf(ofd, &outbuf[0], outcnt);
    outcnt = 0;
  }

  /* ===========================================================================
   * Write the output window window[0..outcnt-1] and update crc and bytes_out.
   * (Used for the decompressed data only.)
   */
  void flush_window() noexcept {
    if (outcnt == 0) {
      return;
    }
    write_buf(ofd, &window[0], outcnt);
    outcnt = 0;
  }
};  // namespace gzip
