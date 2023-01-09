/* CaseSpace, is a text preparation for text compressing/decompressing
 *
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
 */
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include "iMonitor.h"
class File_t;
class LempelZivWelch_t;

/**
 * @class CaseSpace_t
 * @brief Transforming capital letters into small letters
 *
 * Transforming capital letters into small letters
 */
class CaseSpace_t final : public iMonitor_t {
public:
  explicit CaseSpace_t(File_t& in, File_t& out) noexcept;
  virtual ~CaseSpace_t() noexcept override;

  CaseSpace_t(const CaseSpace_t&) = delete;
  CaseSpace_t(CaseSpace_t&&) = delete;
  CaseSpace_t& operator=(const CaseSpace_t&) = delete;
  CaseSpace_t& operator=(CaseSpace_t&&) = delete;

  void Encode() noexcept;
  [[nodiscard]] auto Decode() noexcept -> int64_t;

  [[nodiscard]] auto CharFrequency() const noexcept -> const std::array<int64_t, 256>&;
  [[nodiscard]] auto GetQuote() const noexcept -> const std::string_view;

private:
  /**
   * @enum WordType
   * @brief Word type escape values
   *
   * Word type escape values
   */
  enum struct WordType {
    ALL_SMALL = 60,             ///< 0x3C <
    ALL_BIG = 94,               ///< 0x5E ^
    FIRST_BIG_REST_SMALL = 64,  ///< 0x40 @
    ESCAPE_CHAR = 12,           ///< 0x0C
    CRLF_MARKER = 28            ///< 0x1C
  };

  void Encode(int32_t ch) noexcept;
  void EncodeWord() noexcept;
  void DecodeWord() noexcept;

  [[nodiscard]] auto InputLength() const noexcept -> int64_t final;
  [[nodiscard]] auto OutputLength() const noexcept -> int64_t final;
  [[nodiscard]] auto WorkLength() const noexcept -> int64_t final;
  [[nodiscard]] auto LayoutLength() const noexcept -> int64_t final;

  File_t& _in;
  File_t& _out;
  int64_t _original_length{};
  std::array<int64_t, 256> _char_freq{};
  WordType _wtype{WordType::ALL_SMALL};
  int32_t : 32;  // Padding
  std::unique_ptr<LempelZivWelch_t> _lzw;
  std::string _word{};
  std::string _quote{};
};
