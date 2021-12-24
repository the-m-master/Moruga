/* iEncoder_t, Encoder interface to encode or decode data
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
#ifndef _IENCODER_HDR_
#define _IENCODER_HDR_

class iEncoder_t {
public:
  virtual ~iEncoder_t() noexcept;

  /**
   * Encode 8 bits
   * @param c The 8 bits character to compress
   */
  virtual void Compress(const int32_t c) noexcept = 0;

  /**
   * Decoder 8 bits
   * @return The 8 bits decode character
   */
  [[nodiscard]] virtual auto Decompress() noexcept -> int32_t = 0;

  /**
   * Encode N bits
   * @param N The numbers of bits to encode
   * @param c The N bits character to compress
   */
  virtual void CompressN(const int32_t N, const int64_t c) noexcept = 0;

  /**
   * Decoder N bits
   * @param N The numbers of bits to decode
   * @return The N bits decode character
   */
  [[nodiscard]] virtual auto DecompressN(const int32_t N) noexcept -> int64_t = 0;

  /**
   * Encode variable length integer
   * @param c The character to compress
   */
  virtual void CompressVLI(int64_t c) noexcept = 0;

  /**
   * Decide variable length integer
   * @return The decoded integer
   */
  [[nodiscard]] virtual auto DecompressVLI() noexcept -> int64_t = 0;

  /**
   * Flush bit stream at the end of encoding
   */
  virtual void Flush() noexcept = 0;

  /**
   * Set when file is no text file
   * @param is_binary True when binary otherwise false
   */
  virtual void SetBinary(const bool is_binary) noexcept = 0;

  /**
   * Set start position for the forecast model
   * @param data_pos The start position
   */
  virtual void SetDataPos(const int64_t data_pos) noexcept = 0;

  /**
   * Enforce start of forecast model
   * @param state True to enforce forecast model
   */
  virtual void SetStart(const bool state) noexcept = 0;
};

#endif /* _IENCODER_HDR_ */
