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
#include "CaseSpace.h"
#include <cassert>
#include <cstdio>
#include <utility>
#include "File.h"
#include "Progress.h"
#include "Utilities.h"
#include "ska/ska.h"

// #define DEBUG_WRITE_DICTIONARY

namespace {
#define BITS UINT32_C(19)

#if BITS == 24
  constexpr auto TABLE_SIZE{UINT32_C(16777259)};
#elif BITS == 23
  constexpr auto TABLE_SIZE{UINT32_C(8388617)};
#elif BITS == 22
  constexpr auto TABLE_SIZE{UINT32_C(4194319)};
#elif BITS == 21
  constexpr auto TABLE_SIZE{UINT32_C(2097169)};
#elif BITS == 20
  constexpr auto TABLE_SIZE{UINT32_C(1048583)};
#elif BITS == 19
  constexpr auto TABLE_SIZE{UINT32_C(524309)};
#elif BITS == 18
  constexpr auto TABLE_SIZE{UINT32_C(262147)};
#elif BITS == 17
  constexpr auto TABLE_SIZE{UINT32_C(131101)};
#elif BITS == 16
  constexpr auto TABLE_SIZE{UINT32_C(65537)};
#elif BITS == 15
  constexpr auto TABLE_SIZE{UINT32_C(32771)};
#elif BITS == 14
  constexpr auto TABLE_SIZE{UINT32_C(18041)};
#elif BITS == 13
  constexpr auto TABLE_SIZE{UINT32_C(9029)};
#elif BITS == 12
  constexpr auto TABLE_SIZE{UINT32_C(5021)};
#elif BITS == 11
  constexpr auto TABLE_SIZE{UINT32_C(2053)};
#elif BITS == 10
  constexpr auto TABLE_SIZE{UINT32_C(1031)};
#elif BITS <= 9
  constexpr auto TABLE_SIZE{UINT32_C(521)};
#endif

  constexpr auto MIN_FREQUENCY{UINT32_C(2048)};  // Default 2048
  constexpr auto MIN_WORD_SIZE{UINT32_C(32)};    // Default 32
  constexpr auto MAX_WORD_SIZE{UINT32_C(256)};   // Default 256

  constexpr auto MAX_VALUE{(UINT32_C(1) << BITS) - UINT32_C(1)};
  constexpr auto MAX_CODE{MAX_VALUE - UINT32_C(1)};
  constexpr auto UNUSED{UINT32_C(~0)};
};  // namespace

/**
 * @class LempelZivWelch_t
 * @brief Lempel-Ziv-Welch model for detection of high-frequency long sentences
 *
 * Lempel-Ziv-Welch model for detection of high-frequency long sentences
 */
class LempelZivWelch_t final {
public:
  explicit LempelZivWelch_t() noexcept = default;
  virtual ~LempelZivWelch_t() noexcept;

  LempelZivWelch_t(const LempelZivWelch_t&) = delete;
  LempelZivWelch_t(LempelZivWelch_t&&) = delete;
  auto operator=(const LempelZivWelch_t&) -> LempelZivWelch_t& = delete;
  auto operator=(LempelZivWelch_t&&) -> LempelZivWelch_t& = delete;

  void Append(const int32_t ch) noexcept {
    _word.push_back(static_cast<char>(ch));

    const auto [found, offset]{FindMatch(_string_code, ch)};
    if (found) {
      _string_code = _hashTable[offset].code_value;

      if (const auto length{_word.length()}; (length >= MIN_WORD_SIZE) && (length < MAX_WORD_SIZE)) {
        if (auto it{_appraisal.find(_word)}; it != _appraisal.end()) {
          it->second += 1;  // Increase frequency
        } else {
          _appraisal.emplace(_word, 0);  // Start with frequency is zero
        }
      }
    } else {
      _word.clear();

      HashTable_t& key{_hashTable[offset]};
      key.code_value = _next_code++;
      key.prefix_code = 0x00FFFFFFu & _string_code;
      key.append_character = static_cast<uint8_t>(ch);

      _string_code = 0xFF & ch;

      if (_next_code > MAX_CODE) {  // Reset
        _next_code = 256;
        for (auto& node : _hashTable) {
          node.code_value = UNUSED;
        }
      }
    }
  }

  auto Finish() noexcept -> std::string {
    std::string_view max_word_view{};

    size_t max_peek_freq{0};
    for (const auto& [word, frequency] : _appraisal) {
      if (frequency > MIN_FREQUENCY) {
        const size_t peek_freq{word.length() * frequency};
        if (peek_freq > max_peek_freq) {
          max_peek_freq = peek_freq;
          max_word_view = word;
        }
      }
    }

#if defined(DEBUG_WRITE_DICTIONARY)
    File_t txt{"dictionary.txt", "wb+"};
    for (auto it : _appraisal) {
      std::string_view word{it.first};
      const uint32_t frequency{it.second};
      if (frequency > MIN_FREQUENCY) {
        fprintf(txt, "%2" PRIu64 " %8" PRIu32 " %8" PRIu64 " ", word.length(), frequency, frequency * word.length());
        auto* str{word.data()};
        auto length{word.length()};
        while (length-- > 0) {
          if (isprint(*str)) {
            fprintf(txt, "%c", *str);
          } else {
            fprintf(txt, "\\x%02X", *str);
          }
          ++str;
        }
        fprintf(txt, "\n");
      }
    }
#endif

    std::string max_word{max_word_view.data(), max_word_view.size()};

    _appraisal.clear();
#if defined(USE_BYTELL_HASH_MAP)
    _appraisal.shrink_to_fit();  // Release memory
#endif

    return max_word;
  }

  void Reserve() noexcept {
    _word.reserve(MAX_WORD_SIZE * 2);
    _appraisal.reserve(1u << 18);
  }

private:
  uint32_t _next_code{256};
  uint32_t _string_code{0};
  std::string _word{};
  map_string2uint_t _appraisal{};

  /**
   * @struct HashTable_t
   * @brief LZW hash table implementation
   *
   * LZW hash table implementation
   */
  struct HashTable_t final {
    uint32_t code_value{UNUSED};
    uint32_t prefix_code : 24 {0};
    uint32_t append_character : 8 {0};
  };
  static_assert(8 == sizeof(HashTable_t), "Alignment failure in HashTable_t");
  std::array<HashTable_t, TABLE_SIZE> _hashTable{};

  [[nodiscard]] auto FindMatch(const uint32_t prefix_code, const int32_t append_character) const noexcept -> std::pair<bool, uint32_t> {
    uint32_t offset{(Utilities::PHI32 * ((prefix_code << 8) | (0xFF & append_character))) >> (32 - BITS)};
    for (const auto stride{(offset > 0) ? (TABLE_SIZE - offset) : 1};;) {
      assert(offset < TABLE_SIZE);
      const HashTable_t& key{_hashTable[offset]};

      if (UNUSED == key.code_value) {
        return {false, offset};  // Not found or unused
      }
      if ((prefix_code == key.prefix_code) && (static_cast<uint32_t>(append_character) == key.append_character)) {
        return {true, offset};  // Found and verified
      }

      offset -= stride;
      if (offset >= TABLE_SIZE) {
        offset += TABLE_SIZE;
      }
    }
  }
};
LempelZivWelch_t::~LempelZivWelch_t() noexcept = default;

namespace {
  template <typename T>
  ALWAYS_INLINE constexpr auto is_word_char(const T ch) noexcept -> bool {
    return Utilities::is_upper(ch) || Utilities::is_lower(ch);
  }
}  // namespace

CaseSpace_t::CaseSpace_t(File_t& in, File_t& out) noexcept
    : _in{in},  //
      _out{out},
      _lzw{std::make_unique<LempelZivWelch_t>()} {
  _word.reserve(MAX_WORD_SIZE * 2);
}

CaseSpace_t::~CaseSpace_t() noexcept = default;

auto CaseSpace_t::CharFrequency() const noexcept -> const std::array<int64_t, 256>& {
  return _char_freq;
}

auto CaseSpace_t::GetQuote() const noexcept -> const std::string_view {
  return _quote;
}

auto CaseSpace_t::InputLength() const noexcept -> int64_t {
  return _in.Position();
}

auto CaseSpace_t::OutputLength() const noexcept -> int64_t {
  return _out.Position();
}

auto CaseSpace_t::WorkLength() const noexcept -> int64_t {
  return _original_length;
}

auto CaseSpace_t::LayoutLength() const noexcept -> int64_t {
  return _original_length;
}

void CaseSpace_t::Encode(const int32_t ch) noexcept {
  _out.putc(ch);
  _lzw->Append(ch);
}

void CaseSpace_t::Encode() noexcept {
  _lzw->Reserve();

  _original_length = _in.Size();
  _out.putVLI(_original_length);

  const Progress_t progress("CSE", true, *this);

  bool set{false};
  int32_t ch;
  while (EOF != (ch = _in.getc())) {
    _char_freq[static_cast<size_t>(ch)] += 1;

    if (set) {
      set = false;
      EncodeWord();
      if ('\n' == ch) {  // 0x0A
        Encode(static_cast<int32_t>(WordType::CRLF_MARKER));
        continue;
      }
      Encode('\r');
    } else if ('\r' == ch) {  // 0x0D
      set = true;
      continue;
    }

    if (is_word_char(ch)) {  // a..z || A..Z
      _word.push_back(static_cast<char>(ch));
    } else {
      EncodeWord();

      const auto wt{static_cast<WordType>(ch)};
      if ((WordType::ALL_SMALL == wt) ||             //
          (WordType::ALL_BIG == wt) ||               //
          (WordType::FIRST_BIG_REST_SMALL == wt) ||  //
          (WordType::ESCAPE_CHAR == wt) ||           //
          (WordType::CRLF_MARKER == wt)) {
        Encode(static_cast<int32_t>(WordType::ESCAPE_CHAR));
      }
      Encode(ch);
    }
  }

  EncodeWord();

  _out.Sync();

  _quote = _lzw->Finish();
}

void CaseSpace_t::EncodeWord() noexcept {
  const std::string_view word{_word};
  auto word_length{word.length()};
  for (size_t offset{0}; word_length > 0;) {
    size_t length{0};
    if (Utilities::is_lower(word[offset + length])) {
      ++length;
      while ((length < word_length) && Utilities::is_lower(word[offset + length])) {
        ++length;
      }
      _wtype = WordType::ALL_SMALL;
    } else {
      ++length;
      if (Utilities::is_upper(_word[offset + length])) {
        while ((length < word_length) && Utilities::is_upper(word[offset + length])) {
          ++length;
        }
        _wtype = WordType::ALL_BIG;
      } else {
        while ((length < word_length) && Utilities::is_lower(word[offset + length])) {
          ++length;
        }
        _wtype = WordType::FIRST_BIG_REST_SMALL;
      }
    }

    if ((0 != offset) || (WordType::ALL_SMALL != _wtype)) {
      Encode(static_cast<int32_t>(_wtype));
    }

    word_length -= length;
    while (length-- > 0) {
      const auto ch{word[offset++]};
      Encode(Utilities::to_lower(ch));
    }
  }

  _word.clear();
}

auto CaseSpace_t::Decode() noexcept -> int64_t {
  _original_length = _in.getVLI();
  assert(_original_length > 0);

  const Progress_t progress("CSD", false, *this);

  int32_t ch;
  while (EOF != (ch = _in.getc())) {
    switch (static_cast<WordType>(ch)) {
      case WordType::ESCAPE_CHAR:
        DecodeWord();
        _wtype = WordType::ALL_SMALL;
        ch = _in.getc();
        _out.putc(ch);
        break;

      case WordType::ALL_SMALL:
      case WordType::ALL_BIG:
      case WordType::FIRST_BIG_REST_SMALL:
        DecodeWord();
        _wtype = static_cast<WordType>(ch);
        break;

      case WordType::CRLF_MARKER:
        DecodeWord();
        _wtype = WordType::ALL_SMALL;
        _out.putc('\r');
        _out.putc('\n');
        break;

      default:
        if (is_word_char(ch)) {  // a..z || A..Z
          _word.push_back(static_cast<char>(ch));
        } else {
          DecodeWord();
          _wtype = WordType::ALL_SMALL;
          _out.putc(ch);
        }
        break;
    }
  }

  DecodeWord();

  _out.Sync();

  return _original_length;
}

void CaseSpace_t::DecodeWord() noexcept {
  auto length{_word.length()};
  if (length > 0) {
    switch (const auto* __restrict str{_word.data()}; _wtype) {
      case WordType::ALL_BIG:
        while (length-- > 0) {
          _out.putc(Utilities::to_upper(*str++));
        }
        break;

      case WordType::FIRST_BIG_REST_SMALL:
        --length;
        _out.putc(Utilities::to_upper(*str++));
        [[fallthrough]];

      default:
      case WordType::ALL_SMALL:
      case WordType::CRLF_MARKER:
      case WordType::ESCAPE_CHAR:
        while (length-- > 0) {
          _out.putc(*str++);
        }
        break;
    }

    _word.clear();
  }
}
