/* CaseSpace, is a text preparation for text compressing/decompressing
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
 * along with this program; see the file COPYING3.
 * If not, see <https://www.gnu.org/licenses/>
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

//#define DEBUG_WRITE_DICTIONARY
#if !defined(_MSC_VER)       // VS2019 has trouble handling this code
#define USE_BYTELL_HASH_MAP  // Enable the fastest hash table by Malte Skarupke
#endif

#if defined(USE_BYTELL_HASH_MAP)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waggregate-return"
#pragma GCC diagnostic ignored "-Wc++98-c++11-compat-binary-literal"
#pragma GCC diagnostic ignored "-Wc++98-compat-pedantic"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Weffc++"
#pragma GCC diagnostic ignored "-Wpadded"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#include "ska/bytell_hash_map.hpp"
#pragma GCC diagnostic pop
#else
#include <unordered_map>
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

#pragma pack(push, 1)
struct HashTable_t {
  uint32_t code_value;
  uint32_t prefix_code : 24;
  uint32_t append_character : 8;
};
#pragma pack(pop)
static_assert(8 == sizeof(HashTable_t), "Alignment failure in HashTable_t");

static constexpr auto MAX_VALUE{(UINT32_C(1) << BITS) - UINT32_C(1)};
static constexpr auto MAX_CODE{MAX_VALUE - UINT32_C(1)};
static constexpr auto UNUSED{UINT32_C(~0)};

static constexpr auto MIN_FREQUENCY{UINT32_C(2048)};
static constexpr auto MIN_WORD_SIZE{UINT32_C(32)};

class LempelZivWelch_t final {
public:
  explicit LempelZivWelch_t() {
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
    _word.push_back(char(ch));

    const auto key{find_match(_string_code, ch)};
    HashTable_t& ht{_hashTable[key]};
    if (UNUSED != ht.code_value) {
      _string_code = int32_t(ht.code_value);

      const auto length{_word.length()};
      if ((length >= MIN_WORD_SIZE) && (length < 256)) {
        auto it{_esteem.find(_word)};
        if (it != _esteem.end()) {
          it->second += 1;  // Increase frequency
        } else {
          _esteem[_word] = 0;  // Start with frequency is zero
        }
      }
    } else {
      _word.clear();

      ht.code_value = _next_code++;
      ht.prefix_code = 0x00FFFFFFu & uint32_t(_string_code);
      ht.append_character = uint8_t(ch);

      _string_code = ch;

      if (_next_code > MAX_CODE) {
        reset();
      }
    }
  }

  auto finish() noexcept -> std::string {
    std::vector<CaseSpace_t::Dictionary_t> dictionary{};
    std::for_each(_esteem.begin(), _esteem.end(), [&dictionary](const auto& entry) {
      const auto frequency{entry.second};
      if (frequency > MIN_FREQUENCY) {
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
        const char* str = dic.word.c_str();
        size_t length = dic.word.length();
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
        const char* str = dictionary[0].word.c_str();
        size_t length = dictionary[0].word.length();
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
    // Golden ratio of 2^32 (not a prime)
    static constexpr auto PHI32{UINT32_C(0x9E3779B9)};  // 2654435769

    uint32_t offset{(PHI32 * uint32_t((prefix_code << 8) | append_character)) >> (32 - BITS)};
    const uint32_t stride{(0 == offset) ? 1 : (TABLE_SIZE - offset)};
    for (;;) {
      assert(offset < TABLE_SIZE);
      const HashTable_t& ht{_hashTable[offset]};

      if (UNUSED == ht.code_value) {
        return offset;
      }

      if ((prefix_code == ht.prefix_code) && (append_character == ht.append_character)) {
        return offset;
      }

      offset -= stride;
      if (int32_t(offset) < 0) {
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
  std::array<HashTable_t, TABLE_SIZE> _hashTable;
};

LempelZivWelch_t::~LempelZivWelch_t() noexcept = default;

template <typename T>
ALWAYS_INLINE constexpr auto is_upper(const T c) noexcept -> bool {
  return (c >= 'A') && (c <= 'Z');
}

template <typename T>
ALWAYS_INLINE constexpr auto is_lower(const T c) noexcept -> bool {
  return (c >= 'a') && (c <= 'z');
}

template <typename T>
ALWAYS_INLINE constexpr auto is_word_char(const T c) noexcept -> bool {
  return is_upper(c) || is_lower(c);
}

template <typename T>
ALWAYS_INLINE constexpr auto to_upper(const T c) noexcept -> T {
  return is_lower(c) ? c - 'a' + 'A' : c;
}

template <typename T>
ALWAYS_INLINE constexpr auto to_lower(const T c) noexcept -> T {
  return is_upper(c) ? c - 'A' + 'a' : c;
}

CaseSpace_t::CaseSpace_t(File_t& in, File_t& out) : _in{in}, _out{out}, _lzw{new LempelZivWelch_t()} {
  _char_freq.fill(0);
}

CaseSpace_t::~CaseSpace_t() noexcept = default;

auto CaseSpace_t::charFrequency() const noexcept -> const int64_t* {
  return &_char_freq[0];
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
  return _originalLength;
}

auto CaseSpace_t::layoutLength() const noexcept -> int64_t {
  return _originalLength;
}

void CaseSpace_t::Encode(int32_t ch) noexcept {
  _out.putc(ch);
  _lzw->append(ch);
}

void CaseSpace_t::Encode() noexcept {
  _originalLength = _in.Size();
  _out.put64(_originalLength);

  Progress_t progress("CSE", true, *this);

  bool set{false};
  std::string word{};
  int32_t ch;
  while (EOF != (ch = _in.getc())) {
    _char_freq[uint32_t(ch)] += 1;

    if (set) {
      set = false;
      EncodeWord(word);
      if ('\n' == ch) {  // 0x0A
        Encode(CRLF_MARKER);
        continue;
      }
      Encode('\r');
    } else if ('\r' == ch) {  // 0x0D
      set = true;
      continue;
    }

    if (is_word_char(ch)) {
      word.push_back(char(ch));
    } else {
      EncodeWord(word);

      if ((ALL_SMALL == ch) || (ALL_BIG == ch) || (FIRST_BIG_REST_SMALL == ch) || (ESCAPE_CHAR == ch) || (CRLF_MARKER == ch)) {
        Encode(ESCAPE_CHAR);
      }
      Encode(ch);
    }
  }

  EncodeWord(word);

  _quote = _lzw->finish();
}

void CaseSpace_t::EncodeWord(std::string& word) noexcept {
  auto wordLength{word.length()};
  if (wordLength > 0) {
    size_t offset{0};
    while (wordLength > 0) {
      size_t length{0};
      if (is_lower(word[offset + length])) {
        length++;
        while ((length < wordLength) && is_lower(word[offset + length])) {
          length++;
        }
        _ch_type = ALL_SMALL;
      } else {
        length++;
        if (is_upper(word[offset + length])) {
          while ((length < wordLength) && is_upper(word[offset + length])) {
            length++;
          }
          _ch_type = ALL_BIG;
        } else {
          while ((length < wordLength) && is_lower(word[offset + length])) {
            length++;
          }
          _ch_type = FIRST_BIG_REST_SMALL;
        }
      }

      if ((0 != offset) || (ALL_SMALL != _ch_type)) {
        Encode(_ch_type);
      }

      wordLength -= length;
      while (length-- > 0) {
        int32_t ch = word[offset++];
        Encode(to_lower(ch));
      }
    }

    word.clear();
  }
}

auto CaseSpace_t::Decode() noexcept -> int64_t {
  _originalLength = _in.get64();
  assert(_originalLength > 0);

  Progress_t progress("CSD", false, *this);

  WordType t{ALL_SMALL};
  std::string word{};
  int32_t ch;
  while (EOF != (ch = _in.getc())) {
    switch (WordType(ch)) {
      case ESCAPE_CHAR:
        DecodeWord(word, t);
        t = ALL_SMALL;
        ch = _in.getc();
        _out.putc(ch);
        break;

      case ALL_SMALL:
      case ALL_BIG:
      case FIRST_BIG_REST_SMALL:
        DecodeWord(word, t);
        t = WordType(ch);
        break;

      case CRLF_MARKER:
        DecodeWord(word, t);
        t = ALL_SMALL;
        _out.putc('\r');
        _out.putc('\n');
        break;

      default:
        if (is_word_char(ch)) {
          word.push_back(char(ch));
        } else {
          DecodeWord(word, t);
          t = ALL_SMALL;
          _out.putc(ch);
        }
        break;
    }
  }

  DecodeWord(word, t);

  return _originalLength;
}

void CaseSpace_t::DecodeWord(std::string& word, const WordType t) noexcept {
  const char* __restrict__ str{word.c_str()};
  auto wordLength{word.length()};

  switch (t) {
    case ALL_BIG:
      while (wordLength-- > 0) {
        int32_t ch{*str++};
        _out.putc(to_upper(ch));
      }
      break;

    case FIRST_BIG_REST_SMALL:
      if (wordLength-- > 0) {
        int32_t ch{*str++};
        _out.putc(to_upper(ch));
      }
      [[fallthrough]];

    default:
    case ALL_SMALL:
    case CRLF_MARKER:
    case ESCAPE_CHAR:
      while (wordLength-- > 0) {
        int32_t ch{*str++};
        _out.putc(ch);
      }
      break;
  }

  word.clear();
}