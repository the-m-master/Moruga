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
#include "gzip.h"

/**
 * @namespace gzip
 * @brief Area for GZIP handling
 *
 * Area for GZIP handling
 */
namespace gzip {
  namespace {
    uint16_t bi_buf{0};
    /* Output buffer. bits are inserted starting at the bottom (least significant bits). */

#define Buf_size (8 * 2)
    /* Number of bits used within bi_buf. (bi_buf might be implemented on
     * more than 16 bits on some systems.)
     */

    uint32_t bi_valid{0};
    /* Number of valid bits in bi_buf.  All bits above the last valid bit
     * are always zero.
     */
  };  // namespace

  /* ===========================================================================
   * Initialise the bit string routines.
   */
  void BitsInit() noexcept {
    bi_buf = 0;
    bi_valid = 0;
  }

  /* ===========================================================================
   * Send a value on a given number of bits.
   * IN assertion: length <= 16 and value fits in length bits.
   */
  void SendBits(const uint32_t value, const uint32_t length) noexcept {
    /* If not enough room in bi_buf, use (valid) bits from bi_buf and
     * (16 - bi_valid) bits from value, leaving (width - (16-bi_valid))
     * unused bits in value.
     */
    if (bi_valid > (Buf_size - length)) {
      bi_buf |= uint16_t(value << bi_valid);
      PutShort(bi_buf);
      bi_buf = uint16_t(value >> (Buf_size - bi_valid));
      bi_valid += length - Buf_size;
    } else {
      bi_buf |= uint16_t(value << bi_valid);
      bi_valid += length;
    }
  }

  /* ===========================================================================
   * Reverse the first len bits of a code, using straightforward code (a faster
   * method would use a table)
   * IN assertion: 1 <= len <= 15
   */
  auto BitsReverse(uint32_t code, int32_t len) noexcept -> uint16_t {
    uint16_t res = 0;
    do {
      res |= code & 1;
      code >>= 1;
      res <<= 1;
    } while (--len > 0);
    return res >> 1;
  }

  /* ===========================================================================
   * Write out any remaining bits in an incomplete byte.
   */
  void BitsWindup() noexcept {
    if (bi_valid > 8) {
      PutShort(bi_buf);
    } else if (bi_valid > 0) {
      PutByte(uint8_t(bi_buf));
    }
    bi_buf = 0;
    bi_valid = 0;
  }

  /* ===========================================================================
   * Copy a stored block to the zip file, storing first the length and its
   * one's complement if requested.
   */
  void CopyBlock(char* buf, uint32_t len, int32_t header) noexcept {
    BitsWindup(); /* align on byte boundary */

    if (header) {
      PutShort(uint16_t(len));
      PutShort(uint16_t(~len));
    }
    while (len--) {
      PutByte(uint8_t(*buf++));
    }
  }
};  // namespace gzip
