/* TxtPrep4, is a text preparation for text encoding/decoding
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
 */
#include "TxtPrep4.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cinttypes>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "CaseSpace.h"
#include "File.h"
#include "IntegerXXL.h"
#include "Progress.h"
#include "Utilities.h"
#include "iMonitor.h"

//#define DEBUG_WRITE_ANALYSIS_BMP
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

#if defined(DEBUG_WRITE_ANALYSIS_BMP)
#include "Analysis.h"
#endif

// Word count can be verified by using (on Linux or MSYS2):
// sed -e 's/[^[:alpha:]]/ /g' file2count.txt | tr '\n' " " |  tr -s " " | tr " " '\n'| tr 'A-Z' 'a-z' | sort | uniq -c | sort -nr | nl

static constexpr size_t MAX_WORD_SIZE{256};                        // Default 256
static constexpr size_t MIN_WORD_SIZE{2};                          // Default 2, range 1 .. MAX_WORD_SIZE
static constexpr size_t MIN_WORD_FREQ{4};                          // Default 4, range 1 .. 256
static constexpr size_t MIN_SHORTER_WORD_SIZE{MIN_WORD_SIZE + 5};  // Default 5, range always larger then MIN_WORD_SIZE
static constexpr size_t MIN_NUMBER_SIZE{7};                        // Default 7, range 1 .. 26
static constexpr size_t MAX_NUMBER_SIZE{20};                       // Default 20, range 1 .. 26

static_assert(MIN_WORD_SIZE < MIN_SHORTER_WORD_SIZE, "MIN_SHORTER_WORD_SIZE must be bigger then MIN_WORD_SIZE");
static_assert(MIN_WORD_FREQ >= 1, "MIN_WORD_FREQ must be equal or larger then one");
static_assert(MIN_NUMBER_SIZE < MAX_NUMBER_SIZE, "MAX_NUMBER_SIZE must be bigger then MIN_NUMBER_SIZE");

static constexpr int32_t ESCAPE_CHAR{4};       // 0x04
static constexpr int32_t QUOTING_CHAR{42};     // 0x2A *
static constexpr int32_t SEPARATE_CHAR{'\n'};  // default '\n'

static constexpr uint32_t BITS_OUT{6};                                // Practical limit, 6 bits
static constexpr uint32_t LOW{1 << BITS_OUT};                         //    0 ..    3F --> 0 ..    64 (6 bits)
static constexpr uint32_t MID{LOW + (1 << ((2 * BITS_OUT) - 1))};     //   40 ..   880 --> 0 ..   840 (2^6 + 2^(6+5)                           --> 64 + 2048                  -->   2112)
static constexpr uint32_t HIGH{MID + (1 << ((3 * BITS_OUT) - 3))};    //  881 ..  8880 --> 0 ..  8840 (2^6 + 2^(6+5) + 2^(6+5+4)               --> 64 + 2048 + 32768          -->  34880)
static constexpr uint32_t LIMIT{HIGH + (1 << ((4 * BITS_OUT) - 6))};  // 8881 .. 510C0 --> 0 .. 48840 (2^6 + 2^(6+5) + 2^(6+5+4) + 2^(6+5+4+3) --> 64 + 2048 + 32768 + 262144 --> 297024)

static_assert(BITS_OUT > 1, "Bit range error");
static_assert(LOW > 1, "Bit range error, LOW must larger then one");
static_assert(LOW < MID, "Bit range error, LOW must be less then MID");
static_assert(MID < HIGH, "Bit range error, MID must be less then HIGH");
static_assert(HIGH < LIMIT, "Bit range error, HIGH must be less then LIMIT");

static bool to_numbers_{false};

namespace TxtPrep4 {

template <typename T>
ALWAYS_INLINE constexpr auto is_word_char(const T ch) noexcept -> bool {
  return ('>' == ch) || Utilities::is_lower(ch) || (ch > 127) ||                     // Default for text files
         (to_numbers_ && ((' ' == ch) || ('.' == ch) || Utilities::is_number(ch)));  // In case of a file with a lot of values
}

}  // namespace TxtPrep4

class Dictionary final : public iMonitor_t {
public:
  Dictionary() noexcept = default;
  ~Dictionary() noexcept override;

  Dictionary(const Dictionary&) = delete;
  Dictionary(Dictionary&&) = delete;
  auto operator=(const Dictionary&) -> Dictionary& = delete;
  auto operator=(Dictionary&&) -> Dictionary& = delete;

  void AppendChar(const int32_t ch) noexcept {
    if (const auto wlength{_word.length()}; TxtPrep4::is_word_char(ch) && (wlength < MAX_WORD_SIZE)) {
      _word.push_back(char(ch));
    } else {
      if (wlength >= MIN_WORD_SIZE) {
        AppendWord();
      }
      _word.clear();
    }
  }

  void Create(const File_t& in, File_t& out, const int8_t* const quote, const uint32_t quoteLength) noexcept {
    _original_length = in.Size();
    Progress_t progress("DIC", true, *this);

    _word_map.reserve(LIMIT);

    uint32_t quoteState{0};

    for (int32_t ch{0}; EOF != (ch = in.getc()); ++_input_length) {
      if (quoteLength > 0) {
        if (ch == quote[quoteState]) {
          ++quoteState;
          if (quoteState == quoteLength) {
            quoteState = 0;
          }
          continue;
        }
        if (quoteState > 0) {
          for (uint32_t n{0}; n < quoteState; ++n) {
            AppendChar(quote[n]);
          }
          quoteState = 0;
        }
      }

      AppendChar(ch);
    }

    std::vector<CaseSpace_t::Dictionary_t> dictionary{};
    dictionary.reserve(_word_map.size());

    std::for_each(_word_map.begin(), _word_map.end(), [&dictionary](const auto entry) {
      if (const auto frequency{entry.second + 1}; frequency >= MIN_WORD_FREQ) {  // The first element has count 0 (!)
        dictionary.emplace_back(CaseSpace_t::Dictionary_t(entry.first, frequency));
      }
    });
    _word_map.clear();
#if defined(USE_BYTELL_HASH_MAP)
    _word_map.shrink_to_fit();  // Release memory
#endif
    _dic_length = uint32_t(dictionary.size());

    // Sort all by frequency
    std::stable_sort(dictionary.begin(), dictionary.end(), [](const auto& a, const auto& b) noexcept -> bool {  //
      return (a.frequency == b.frequency) ? (a.word.compare(b.word) < 0) : (a.frequency > b.frequency);
    });

    _dic_length = (std::min)(_dic_length, LIMIT);

    constexpr auto name_compare{[](const auto& a, const auto& b) noexcept -> bool {  //
      return a.word.compare(b.word) < 0;
    }};

    // Sort 0 .. LOW by name too improve compression
    std::stable_sort(dictionary.begin(), dictionary.begin() + (std::min)(LOW, _dic_length), name_compare);

    if (_dic_length >= LOW) {
      // Sort LOW .. MID by name too improve compression
      std::stable_sort(dictionary.begin() + LOW, dictionary.begin() + (std::min)(MID, _dic_length), name_compare);
    }
    if (_dic_length >= MID) {
      // Sort MID .. HIGH by name too improve compression
      std::stable_sort(dictionary.begin() + MID, dictionary.begin() + (std::min)(HIGH, _dic_length), name_compare);
    }
    if (_dic_length >= HIGH) {
      // Sort HIGH .. _index by name too improve compression
      std::stable_sort(dictionary.begin() + HIGH, dictionary.begin() + _dic_length, name_compare);
    }

    _word_map.reserve(_dic_length);
    for (uint32_t n{0}; n < _dic_length; ++n) {
      _word_map[dictionary[n].word] = frequency2bytes(n);
    }
    assert(_dic_length == _word_map.size());

    out.putVLI(_dic_length);  // write length of dictionary

    for (uint32_t n{0}; n < _dic_length; ++n) {
      out.Write(dictionary[n].word.c_str(), dictionary[n].word.length());
      out.putc(SEPARATE_CHAR);
    }

#if defined(DEBUG_WRITE_DICTIONARY)
    File_t txt("dictionary.txt", "wb+");
    for (auto dic : dictionary) {
      fprintf(txt, "%u,%" PRIu64 ",%s\n", dic.frequency, dic.word.length(), dic.word.c_str());
    }
#endif
  }

  void Read(const File_t& stream) noexcept {
    _dic_length = uint32_t(stream.getVLI());
    assert(_dic_length <= LIMIT);

    _byte_map.reserve(_dic_length);

    std::string word{};
    for (uint32_t n{0}; n < _dic_length; ++n) {
      int32_t ch;
      while ((SEPARATE_CHAR != (ch = stream.getc()))) {
        word.push_back(char(ch));
      }
      const auto bytes{frequency2bytes(n)};
      _byte_map[bytes] = word;
      word.clear();
    }
  }

  [[nodiscard]] auto word2frequency(const std::string& word) const noexcept -> std::pair<bool, uint32_t> {
    if (const auto it{_word_map.find(word)}; it != _word_map.end()) {
      return {true, it->second};  // Is found, frequency
    }
    return {false, 0};  // Not found...
  }

  auto bytes2word(uint32_t bytes) noexcept -> const std::string& {
    return _byte_map[bytes];
  }

private:
  [[nodiscard]] static auto frequency2bytes(uint32_t frequency) noexcept -> uint32_t {
    // clang-format off
    uint32_t bytes;
    if (frequency < LOW) {
      bytes = 0x80 | frequency;                            // 6 bits
    } else if (frequency < MID) {
      frequency -= LOW;                                    // convert to 0 .. 7FF (11 bits)
      assert(frequency < (1 << 11));
      bytes  = (0xC0 | (       frequency >> 6)) << 8;      // 5 bits
      bytes |= (0x80 | (0x3F & frequency));                // 6 bits
    } else if (frequency < HIGH) {
      frequency -= MID;                                    // convert to 0 .. 7FFF (15 bits)
      assert(frequency < (1 << 15));
      bytes  = (0xE0 | (        frequency >> 11)) << 16;   // 4 bits
      bytes |= (0xC0 | (0x1F & (frequency >> 6))) <<  8;   // 5 bits
      bytes |= (0x80 | (0x3F &  frequency));               // 6 bits
    } else {
      frequency -= HIGH;                                   // convert to 0 .. 3FFFF (18 bits)
      assert(frequency < (1 << 18));
      bytes  = (0xF0 | (        frequency >> 15))  << 24;  // 3 bits
      bytes |= (0xE0 | (0x0F & (frequency >> 11))) << 16;  // 4 bits
      bytes |= (0xC0 | (0x1F & (frequency >> 6)))  <<  8;  // 5 bits
      bytes |= (0x80 | (0x3F &  frequency));               // 6 bits
    }
    // clang-format on
    return bytes;
  }

  void AppendWord() noexcept {
    if (auto it{_word_map.find(_word)}; it != _word_map.end()) {
      it->second++;
    } else {
      _word_map[_word] = 0;
    }
  }

  [[nodiscard]] auto inputLength() const noexcept -> int64_t final {
    return _input_length;
  }
  [[nodiscard]] auto outputLength() const noexcept -> int64_t final {
    return _input_length;
  }
  [[nodiscard]] auto workLength() const noexcept -> int64_t final {
    return _original_length;
  }
  [[nodiscard]] auto layoutLength() const noexcept -> int64_t final {
    return _original_length;
  }

#if defined(USE_BYTELL_HASH_MAP)
  ska::bytell_hash_map<std::string, uint32_t> _word_map;  // Does encode enwik9 25% faster!
  ska::bytell_hash_map<uint32_t, std::string> _byte_map;
#else
  std::unordered_map<std::string, uint32_t> _word_map;
  std::unordered_map<uint32_t, std::string> _byte_map;
#endif
  int64_t _original_length{0};
  int64_t _input_length{0};
  uint32_t _dic_length{0};
  int32_t : 32;  // Padding
  std::string _word{};
};
Dictionary::~Dictionary() noexcept = default;

class TxtPrep final : public iMonitor_t {
public:
  explicit TxtPrep(File_t& in, File_t& out, const int64_t* const charFreq, const std::string& quote) noexcept
      : _in{in},  //
        _out{out},
        _qlength{uint32_t(quote.length())} {
    assert(_qlength < 256);            // Must fit in a byte of 8 bit
    assert(_qlength < _quote.size());  // Should fit
    memcpy(&_quote[0], quote.c_str(), _qlength);
    to_numbers_ = false;
    if (nullptr != charFreq) {
      int64_t chrAZ{0};
      int64_t chr09{0};
      for (uint8_t n{'0'}; n <= '9'; ++n) {
        chr09 += charFreq[n];
      }
      for (uint8_t n{'A'}; n <= 'Z'; ++n) {
        chrAZ += charFreq[n];
      }
      for (uint8_t n{'a'}; n <= 'z'; ++n) {
        chrAZ += charFreq[n];
      }
      const int64_t originalLength{_in.Size()};
      if (((chrAZ * 64) > originalLength) && (chr09 > (chrAZ * 8))) {
        to_numbers_ = true;
        _qlength = 0;
        _quote.fill(0);
      }
    }
  }
  ~TxtPrep() noexcept override;

  TxtPrep() = delete;
  TxtPrep(const TxtPrep&) = delete;
  TxtPrep(TxtPrep&&) = delete;
  auto operator=(const TxtPrep&) -> TxtPrep& = delete;
  auto operator=(TxtPrep&&) -> TxtPrep& = delete;

  auto Encode() noexcept -> int64_t {
    _original_length = _in.Size();
    _out.putVLI(_original_length);
    Putc(uint8_t(_qlength));
    for (uint32_t n{0}; n < _qlength; ++n) {
      Putc(_quote[n]);
    }

    _dictionary.Create(_in, _out, &_quote[0], _qlength);

    const auto data_pos{_out.Position()};

    Progress_t progress("TXT", true, *this);

    _in.Rewind();

    uint32_t qoffset{0};

    int32_t ch;
    while (EOF != (ch = _in.getc())) {
      if (_qlength > 0) {
        if (ch == _quote[qoffset]) {
          ++qoffset;
          if (qoffset == _qlength) {
            EncodeWordValue();
            Putc(QUOTING_CHAR);
            qoffset = 0;
          }
          continue;
        }
        if (qoffset > 0) {
          for (uint32_t n{0}; n < qoffset; ++n) {
            EncodeChar(_quote[n]);
          }
          qoffset = 0;
        }
      }

      EncodeChar(ch);
    }

    for (uint32_t n{0}; n < qoffset; ++n) {
      EncodeChar(_quote[n]);
    }

    EncodeWordValue();

    return data_pos + uint32_t(sizeof(int64_t));  // Add original length used in CaseSpace
  }

  auto Decode() noexcept -> int64_t {
    _original_length = _in.getVLI();
    assert(_original_length > 0);
    _qlength = uint32_t(_in.getc());
    for (uint32_t n{_qlength}; n--;) {  // Reverse read
      _quote[n] = int8_t(_in.getc());
    }

    Progress_t progress("TXT", false, *this);

    _dictionary.Read(_in);

    while (_output_length < _original_length) {
      int32_t ch{_in.getc()};
      assert(EOF != ch);
      if (ESCAPE_CHAR == ch) {
        ch = _in.getc();
        if (!to_numbers_ && (0xF0 == (0xF0 & ch)) && ((0x0F & ch) >= 4)) {
          const auto safe_position{_in.Position()};
          int32_t costs{0x0F & ch};
          uint128_t value{0};
          int32_t k{0};
          int32_t b;
          do {
            b = _in.getc();
            value |= uint128_t(0x3F & b) << k;
            k += 6;
            --costs;
          } while (0x80 & b);
          if (0 == costs) {
            std::array<char, 64> tmp{xxltostr(value)};
            for (const char* __restrict__ str{&tmp[0]}; *str;) {
              Putc(*str++);
            }
          } else {
            _in.Seek(safe_position);
            Putc(ch);
          }
        } else {
          Putc(ch);
        }
      } else if (QUOTING_CHAR == ch) {
        for (uint32_t n{_qlength}; n--;) {  // Reverse write
          Putc(_quote[n]);
        }
        continue;
      } else if (0x80 & ch) {
        uint32_t bytes{uint8_t(ch)};
        if (0 != (0x40 & ch)) {
          ch = _in.getc();
          bytes = (bytes << 8) | uint8_t(ch);
          if (0 != (0x40 & ch)) {
            ch = _in.getc();
            bytes = (bytes << 8) | uint8_t(ch);
            if (0 != (0x40 & ch)) {
              ch = _in.getc();
              bytes = (bytes << 8) | uint8_t(ch);
            }
          }
        }

        const std::string& word{_dictionary.bytes2word(bytes)};
        assert(word.length() > 0);
        for (const char* __restrict__ str{word.c_str()}; *str;) {
          Putc(*str++);
        }
      } else {
        Putc(ch);
      }
    }

    return _output_length;
  }

private:
  ALWAYS_INLINE void Putc(int32_t ch) noexcept {
    ++_output_length;
    _out.putc(ch);
  }

  void EncodeCodeWord(const uint32_t bytes) noexcept {
    uint8_t tmp{uint8_t(bytes >> 24)};
    if (0 != tmp) {
      Putc(tmp);
    }
    tmp = uint8_t(bytes >> 16);
    if (0 != tmp) {
      Putc(tmp);
    }
    tmp = uint8_t(bytes >> 8);
    if (0 != tmp) {
      Putc(tmp);
    }
    Putc(int32_t(bytes));
  }

  void Literal(const char* __restrict__ literal, uint32_t length) noexcept {
    if (length > 0) {
      while (length-- > 0) {
        int32_t ch{*literal++};
        if (0x80 & ch) {
          Putc(ESCAPE_CHAR);
        }
        Putc(ch);
      }
    }
  }

  void EncodeWord() noexcept {
    auto wlength{uint32_t(_word.length())};
    const char* __restrict__ word{_word.c_str()};

    if (wlength >= MIN_WORD_SIZE) {
      if (const auto whole_word{_dictionary.word2frequency(_word)}; whole_word.first) {
        EncodeCodeWord(whole_word.second);
        return;
      }

      uint32_t offset_end{0};
      uint32_t frequency_end{0};

      // Try to find shorter word, strip end of word
      for (uint32_t offset{wlength - 1}; offset >= MIN_SHORTER_WORD_SIZE; --offset) {
        const std::string shorter(word, offset);
        if (const auto short_word{_dictionary.word2frequency(shorter)}; short_word.first) {
          offset_end = offset;
          frequency_end = short_word.second;
          break;
        }
      }

      uint32_t offset_begin{0};
      uint32_t frequency_begin{0};

      // Try to find shorter word, strip begin of word
      for (uint32_t offset{1}; (wlength - offset) >= MIN_SHORTER_WORD_SIZE; ++offset) {
        const std::string shorter(word + offset, wlength - offset);
        if (const auto short_word{_dictionary.word2frequency(shorter)}; short_word.first) {
          offset_begin = offset;
          frequency_begin = short_word.second;
          break;
        }
      }

      if (0 != offset_end) {
        if (0 != offset_begin) {
          if ((wlength - offset_end) <= offset_begin) {
            EncodeCodeWord(frequency_end);
            Literal(word + offset_end, wlength - offset_end);
          } else {
            Literal(word, offset_begin);
            EncodeCodeWord(frequency_begin);
          }
        } else {
          EncodeCodeWord(frequency_end);
          Literal(word + offset_end, wlength - offset_end);
        }
        return;
      }

      if (0 != offset_begin) {
        Literal(word, offset_begin);
        EncodeCodeWord(frequency_begin);
        return;
      }
    }

    // Not found ...
    Literal(word, wlength);
  }

  void EncodeWordValue() noexcept {
    EncodeWord();
    _word.clear();

    const auto vlength{uint32_t(_value.length())};
    if (vlength > 0) {
      if ((vlength <= MIN_NUMBER_SIZE) || !EncodeValue()) {
        Literal(_value.c_str(), vlength);
      }
      _value.clear();
    }
  }

  auto CostsValue(uint128_t value) noexcept -> int32_t {
    int32_t i{1};
    while (value > 0x3F) {
      value >>= 6;
      ++i;
    }
    return i;
  }

  //                         ULLONG_MAX 18446744073709551615ULL
  static constexpr uint128_t P10_UINT64{10000000000000000000_xxl};  // 19 zeroes

  [[nodiscard]] auto xxltostr(const uint128_t value) const noexcept -> std::array<char, 64> {
    std::array<char, 64> str;
    if (value > ULLONG_MAX) {
      const uint64_t leading{uint64_t(value / P10_UINT64)};
      const uint64_t trailing{uint64_t(value % P10_UINT64)};
      snprintf(&str[0], str.size(), "%" PRIu64 "%.19" PRIu64, leading, trailing);
    } else {
      snprintf(&str[0], str.size(), "%" PRIu64, uint64_t(value));
    }
    return str;
  }

  [[nodiscard]] auto strtoxxl(const char* __restrict__ src) const noexcept -> uint128_t {
    uint128_t value{0};
    while (*src) {
      value *= 10;
      value += uint128_t(*src - '0');
      ++src;
    }
    return value;
  }

  auto EncodeValue() noexcept -> bool {
    uint128_t value{strtoxxl(_value.c_str())};
    std::array<char, 64> tmp{xxltostr(value)};
    if (!_value.compare(&tmp[0])) {  // Avoid leading zeros
      Putc(ESCAPE_CHAR);
      const int32_t costs{CostsValue(value)};
      assert(costs >= 0x04);  // Bottom limit
      assert(costs <= 0x0F);  // Physical limit
      Putc(0xF0 | costs);
      while (value > 0x3F) {
        Putc((0x80 | int32_t(0x3F & value)));
        value >>= 6;
      }
      Putc(int32_t(value));
      return true;  // Success
    }
    return false;  // Failure...
  }

  void EncodeChar(const int32_t ch) noexcept {
    if (!to_numbers_) {
      const auto vlength{_value.length()};
      if (Utilities::is_number(ch) && ((vlength > 0) || ((0 == vlength) && ('0' != ch)))) {  // Avoid leading zero's
        _value.push_back(char(ch));
        if (vlength >= MAX_NUMBER_SIZE) {  // The maximum value is 340282366920938463463374607431768211455...
          EncodeWordValue();
        }
        return;
      }
      if (vlength > 0) {
        EncodeWordValue();
      }
    }

    if (TxtPrep4::is_word_char(ch) && (_word.length() < MAX_WORD_SIZE)) {
      _word.push_back(char(ch));
    } else {
      EncodeWordValue();

      if ((0x80 & ch) || (ESCAPE_CHAR == ch) || (QUOTING_CHAR == ch)) {
        Putc(ESCAPE_CHAR);
      }
      Putc(ch);
    }
  }

  [[nodiscard]] auto inputLength() const noexcept -> int64_t final {
    return _in.Position();
  }
  [[nodiscard]] auto outputLength() const noexcept -> int64_t final {
    return _out.Position();
  }
  [[nodiscard]] auto workLength() const noexcept -> int64_t final {
    return _original_length;
  }
  [[nodiscard]] auto layoutLength() const noexcept -> int64_t final {
    return _original_length;
  }

  Dictionary _dictionary;
  File_t& _in;
  File_t& _out;
  int64_t _original_length{0};
  int64_t _output_length{0};
  uint32_t _qlength;
  std::array<int8_t, 256> _quote{};
  int32_t : 32;  // Padding
  std::string _word{};
  std::string _value{};
};
TxtPrep::~TxtPrep() noexcept = default;

auto encode_txt(File_t& in, File_t& out) noexcept -> int64_t {
  int64_t length{0};
  if (File_t tmp /*("_tmp_.cse", "wb+")*/; tmp.isOpen()) {
    CaseSpace_t cse(in, tmp);
    cse.Encode();

    tmp.Rewind();

#if defined(DEBUG_WRITE_ANALYSIS_BMP)
    Analysis_t analysis(cse.charFrequency());
    analysis.Write();
#endif

    TxtPrep txtprep(tmp, out, cse.charFrequency(), cse.getQuote());
    length = txtprep.Encode();
  }
  return length;
}

auto decode_txt(File_t& in, File_t& out) noexcept -> int64_t {
  int64_t length{0};
  if (File_t tmp /*("_tmp_.cse", "wb+")*/; tmp.isOpen()) {
    TxtPrep txtprep(in, tmp, nullptr, "");
    txtprep.Decode();

    tmp.Rewind();

    CaseSpace_t cse(tmp, out);
    length = cse.Decode();
  }
  return length;
}
