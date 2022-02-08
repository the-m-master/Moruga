/* CaseSpace, is a text preparation for text compressing/decompressing
 *
 * Copyright (c) 2019-2022 Marwijn Hessel
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
 *
 * https://github.com/the-m-master/Moruga
 */
#include "CaseSpace.h"
#include <algorithm>
#include <cassert>
#include <cinttypes>
#include <cstdio>
#include <utility>
#include <vector>
#include "File.h"
#include "Progress.h"
#include "Utilities.h"

//#define DEBUG_WRITE_DICTIONARY
#if !defined(_MSC_VER)         // VS2019 has trouble handling this code
#  define USE_BYTELL_HASH_MAP  // Enable the fastest hash table by Malte Skarupke
#endif

#if defined(USE_BYTELL_HASH_MAP)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Waggregate-return"
#  pragma GCC diagnostic ignored "-Wc++98-c++11-compat-binary-literal"
#  pragma GCC diagnostic ignored "-Wc++98-compat-pedantic"
#  pragma GCC diagnostic ignored "-Wconversion"
#  pragma GCC diagnostic ignored "-Weffc++"
#  pragma GCC diagnostic ignored "-Wpadded"
#  pragma GCC diagnostic ignored "-Wshadow"
#  pragma GCC diagnostic ignored "-Wsign-conversion"
#  include "ska/bytell_hash_map.hpp"
#  pragma GCC diagnostic pop
#else
#  include <unordered_map>
#endif

#define BITS 19

#if BITS == 24
static constexpr int32_t TABLE_SIZE{16777259};
#elif BITS == 23
static constexpr int32_t TABLE_SIZE{8388617};
#elif BITS == 22
static constexpr int32_t TABLE_SIZE{4194319};
#elif BITS == 21
static constexpr int32_t TABLE_SIZE{2097169};
#elif BITS == 20
static constexpr int32_t TABLE_SIZE{1048583};
#elif BITS == 19
static constexpr int32_t TABLE_SIZE{524309};
#elif BITS == 18
static constexpr int32_t TABLE_SIZE{262147};
#elif BITS == 17
static constexpr int32_t TABLE_SIZE{131101};
#elif BITS == 16
static constexpr int32_t TABLE_SIZE{65537};
#elif BITS == 15
static constexpr int32_t TABLE_SIZE{32771};
#elif BITS == 14
static constexpr int32_t TABLE_SIZE{18041};
#elif BITS == 13
static constexpr int32_t TABLE_SIZE{9029};
#elif BITS == 12
static constexpr int32_t TABLE_SIZE{5021};
#elif BITS == 11
static constexpr int32_t TABLE_SIZE{2053};
#elif BITS == 10
static constexpr int32_t TABLE_SIZE{1031};
#elif BITS <= 9
static constexpr int32_t TABLE_SIZE{521};
#endif

static constexpr auto MAX_VALUE{(UINT32_C(1) << BITS) - UINT32_C(1)};
static constexpr auto MAX_CODE{MAX_VALUE - UINT32_C(1)};
static constexpr auto UNUSED{UINT32_C(~0)};

static constexpr auto MIN_FREQUENCY{UINT32_C(2048)};
static constexpr auto MIN_WORD_SIZE{UINT32_C(32)};

class LempelZivWelch_t final {
public:
  explicit LempelZivWelch_t() noexcept {
    reset();
  }

  virtual ~LempelZivWelch_t() noexcept;

  LempelZivWelch_t(const LempelZivWelch_t&) = delete;
  LempelZivWelch_t(LempelZivWelch_t&&) = delete;
  auto operator=(const LempelZivWelch_t&) -> LempelZivWelch_t& = delete;
  auto operator=(LempelZivWelch_t&&) -> LempelZivWelch_t& = delete;

  void reset() noexcept {
    _next_code = 256;
    for (uint32_t n{TABLE_SIZE}; n--;) {
      _hashTable[n].code_value = UNUSED;
    }
  }

  void append(const int32_t ch) noexcept {
    _word.push_back(static_cast<char>(ch));

    const auto key{find_match(_string_code, ch)};

    if (HashTable_t & ht{_hashTable[key]}; UNUSED != ht.code_value) {
      _string_code = static_cast<int32_t>(ht.code_value);

      if (const auto length{_word.length()}; (length >= MIN_WORD_SIZE) && (length < 256)) {
        if (auto it{_esteem.find(_word)}; it != _esteem.end()) {
          it->second += 1;  // Increase frequency
        } else {
          _esteem[_word] = 0;  // Start with frequency is zero
        }
      }
    } else {
      _word.clear();

      ht.code_value = _next_code++;
      ht.prefix_code = 0x00FFFFFFu & static_cast<uint32_t>(_string_code);
      ht.append_character = static_cast<uint8_t>(ch);

      _string_code = ch;

      if (_next_code > MAX_CODE) {
        reset();
      }
    }
  }

  auto finish() noexcept -> std::string {
    std::vector<CaseSpace_t::Dictionary_t> dictionary{};
    std::for_each(_esteem.begin(), _esteem.end(), [&dictionary](const auto& entry) {
      if (const auto frequency{entry.second}; frequency > MIN_FREQUENCY) {
        dictionary.emplace_back(CaseSpace_t::Dictionary_t(entry.first, entry.second));
      }
    });
    _esteem.clear();
#if defined(USE_BYTELL_HASH_MAP)
    _esteem.shrink_to_fit();  // Release memory
#endif

    std::stable_sort(dictionary.begin(), dictionary.end(), [](const auto& a, const auto& b) -> bool {  //
      return (a.frequency * a.word.length()) > (b.frequency * b.word.length());
    });

    if (!dictionary.empty()) {
#if defined(DEBUG_WRITE_DICTIONARY)
      File_t txt("dictionary.txt", "wb+");
      for (auto dic : dictionary) {
        fprintf(txt, "%2" PRIu64 " %8" PRIu32 " %8" PRIu64 " ", dic.word.length(), dic.frequency, dic.frequency * dic.word.length());
        auto* str{dic.word.c_str()};
        auto length{dic.word.length()};
        while (length--) {
          if (isprint(*str)) {
            fprintf(txt, "%c", *str);
          } else {
            fprintf(txt, "\\x%02X", *str);
          }
          ++str;
        }
        fprintf(txt, "\n");
      }
#elif 0  // !defined(NDEBUG)
      {
        fprintf(stdout, "\n");
        auto* str{dictionary[0].word.c_str()};
        auto length{dictionary[0].word.length()};
        while (length--) {
          if (isprint(*str)) {
            fprintf(stdout, "%c", *str);
          } else {
            fprintf(stdout, "\\x%02X", *str);
          }
          ++str;
        }
        fprintf(stdout, "\n");
      }
#endif

      return dictionary[0].word;
    }

    return "";
  }

private:
  auto find_match(const int32_t prefix_code, const int32_t append_character) noexcept -> uint32_t {
    uint32_t offset{(Utilities::PHI32 * static_cast<uint32_t>((prefix_code << 8) | append_character)) >> (32 - BITS)};
    const uint32_t stride{(0 == offset) ? 1 : (TABLE_SIZE - offset)};
    for (;;) {
      assert(offset < TABLE_SIZE);
      const HashTable_t& ht{_hashTable[offset]};

      if (UNUSED == ht.code_value) {
        return offset;
      }

      if ((static_cast<uint32_t>(prefix_code) == ht.prefix_code) &&  //
          (static_cast<uint32_t>(append_character) == ht.append_character)) {
        return offset;
      }

      offset -= stride;
      if (static_cast<int32_t>(offset) < 0) {
        offset += TABLE_SIZE;
      }
    }
  }

  uint32_t _next_code{256};
  int32_t _string_code{0};
  std::string _word{};
#if defined(USE_BYTELL_HASH_MAP)
  ska::bytell_hash_map<std::string, uint32_t> _esteem{};
#else
  std::unordered_map<std::string, uint32_t> _esteem{};
#endif
  struct HashTable_t {
    uint32_t code_value;
    uint32_t prefix_code : 24;
    uint32_t append_character : 8;
  };
  static_assert(8 == sizeof(HashTable_t), "Alignment failure in HashTable_t");
  std::array<HashTable_t, TABLE_SIZE> _hashTable{};
};
LempelZivWelch_t::~LempelZivWelch_t() noexcept = default;

namespace CaseSpace {
  template <typename T>
  ALWAYS_INLINE constexpr auto is_word_char(const T ch) noexcept -> bool {
    return Utilities::is_upper(ch) || Utilities::is_lower(ch);
  }
}  // namespace CaseSpace

CaseSpace_t::CaseSpace_t(File_t& in, File_t& out) noexcept
    : _in{in},  //
      _out{out},
      _lzw{std::make_unique<LempelZivWelch_t>()} {
  _char_freq.fill(0);
}

CaseSpace_t::~CaseSpace_t() noexcept = default;

auto CaseSpace_t::charFrequency() const noexcept -> const int64_t* {
  return _char_freq.data();
}

auto CaseSpace_t::getQuote() const noexcept -> const std::string& {
  return _quote;
}

auto CaseSpace_t::inputLength() const noexcept -> int64_t {
  return _in.Position();
}

auto CaseSpace_t::outputLength() const noexcept -> int64_t {
  return _out.Position();
}

auto CaseSpace_t::workLength() const noexcept -> int64_t {
  return _original_length;
}

auto CaseSpace_t::layoutLength() const noexcept -> int64_t {
  return _original_length;
}

void CaseSpace_t::Encode(const int32_t ch) noexcept {
  _out.putc(ch);
  _lzw->append(ch);
}

void CaseSpace_t::Encode() noexcept {
  _original_length = _in.Size();
  _out.putVLI(_original_length);

  Progress_t progress("CSE", true, *this);

  bool set{false};
  int32_t ch;
  while (EOF != (ch = _in.getc())) {
    _char_freq[static_cast<uint32_t>(ch)] += 1;

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

    if (CaseSpace::is_word_char(ch)) {  // a..z || A..Z
      _word.push_back(static_cast<char>(ch));
    } else {
      EncodeWord();

      if ((WordType::ALL_SMALL == static_cast<WordType>(ch)) ||             //
          (WordType::ALL_BIG == static_cast<WordType>(ch)) ||               //
          (WordType::FIRST_BIG_REST_SMALL == static_cast<WordType>(ch)) ||  //
          (WordType::ESCAPE_CHAR == static_cast<WordType>(ch)) ||           //
          (WordType::CRLF_MARKER == static_cast<WordType>(ch))) {
        Encode(static_cast<int32_t>(WordType::ESCAPE_CHAR));
      }
      Encode(ch);
    }
  }

  EncodeWord();

  _quote = _lzw->finish();
}

void CaseSpace_t::EncodeWord() noexcept {
  auto wlength{static_cast<uint32_t>(_word.length())};
  if (wlength > 0) {
    uint32_t offset{0};
    while (wlength > 0) {
      uint32_t length{0};
      if (Utilities::is_lower(_word[offset + length])) {
        length++;
        while ((length < wlength) && Utilities::is_lower(_word[offset + length])) {
          length++;
        }
        _wtype = WordType::ALL_SMALL;
      } else {
        length++;
        if (Utilities::is_upper(_word[offset + length])) {
          while ((length < wlength) && Utilities::is_upper(_word[offset + length])) {
            length++;
          }
          _wtype = WordType::ALL_BIG;
        } else {
          while ((length < wlength) && Utilities::is_lower(_word[offset + length])) {
            length++;
          }
          _wtype = WordType::FIRST_BIG_REST_SMALL;
        }
      }

      if ((0 != offset) || (WordType::ALL_SMALL != _wtype)) {
        Encode(static_cast<int32_t>(_wtype));
      }

      wlength -= length;
      while (length-- > 0) {
        const auto ch{_word[offset++]};
        Encode(Utilities::to_lower(ch));
      }
    }

    _word.clear();
  }
}

auto CaseSpace_t::Decode() noexcept -> int64_t {
  _original_length = _in.getVLI();
  assert(_original_length > 0);

  Progress_t progress("CSD", false, *this);

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
        if (CaseSpace::is_word_char(ch)) {  // a..z || A..Z
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

  return _original_length;
}

void CaseSpace_t::DecodeWord() noexcept {
  if (_word.length() > 0) {
    switch (const char* __restrict str{_word.c_str()}; _wtype) {
      case WordType::ALL_BIG:
        while (*str) {
          const auto ch{*str++};
          _out.putc(Utilities::to_upper(ch));
        }
        break;

      case WordType::FIRST_BIG_REST_SMALL:
        if (*str) {
          const auto ch{*str++};
          _out.putc(Utilities::to_upper(ch));
        }
        [[fallthrough]];

      default:
      case WordType::ALL_SMALL:
      case WordType::CRLF_MARKER:
      case WordType::ESCAPE_CHAR:
        while (*str) {
          const auto ch{*str++};
          _out.putc(ch);
        }
        break;
    }

    _word.clear();
  }
}
