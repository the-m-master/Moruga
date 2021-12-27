/* TxtPrep4, is a text preparation for text encoding/decoding
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
#include "TxtPrep4.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cinttypes>
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
static constexpr size_t MIN_NUMBER_SIZE{7};                        // Default 7, range 1 .. 256

static_assert(MIN_WORD_SIZE < MIN_SHORTER_WORD_SIZE, "MIN_SHORTER_WORD_SIZE must be bigger then MIN_WORD_SIZE");
static_assert(MIN_WORD_FREQ >= 1, "MIN_WORD_FREQ must be equal or larger then one");

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

template <typename T>
ALWAYS_INLINE constexpr auto is_word_char(const T ch) noexcept -> bool {
  return ('>' == ch) || is_lower(ch) || (ch > 127) ||  //
         (to_numbers_ && ((' ' == ch) || ('.' == ch) || is_number(ch)));
}

class Dictionary final : public iMonitor_t {
public:
  Dictionary() = default;
  ~Dictionary() noexcept override;

  Dictionary(const Dictionary&) = delete;
  Dictionary(Dictionary&&) = delete;
  auto operator=(const Dictionary&) -> Dictionary& = delete;
  auto operator=(Dictionary&&) -> Dictionary& = delete;

  void AppendChar(const int32_t ch, std::string& word) noexcept {
    if (is_word_char(ch) && (word.length() < MAX_WORD_SIZE)) {
      word.push_back(char(ch));
    } else {
      if (word.length() >= MIN_WORD_SIZE) {
        AppendWord(word);
      }
      word.clear();
    }
  }

  void Create(const File_t& in, File_t& out, const int8_t* const quote, const size_t quoteLength) noexcept {
    _originalLength = in.Size();
    Progress_t progress("DIC", true, *this);

    _word_map.reserve(LIMIT);

    size_t quoteState{0};

    std::string word{};
    for (int32_t ch{0}; EOF != (ch = in.getc()); ++_inputLength) {
      if (quoteLength > 0) {
        if (ch == quote[quoteState]) {
          ++quoteState;
          if (quoteState == quoteLength) {
            quoteState = 0;
          }
          continue;
        }
        if (quoteState > 0) {
          for (size_t n{0}; n < quoteState; ++n) {
            AppendChar(quote[n], word);
          }
          quoteState = 0;
        }
      }

      AppendChar(ch, word);
    }

    std::vector<CaseSpace_t::Dictionary_t> dictionary{};
    dictionary.reserve(_word_map.size());

    std::for_each(_word_map.begin(), _word_map.end(), [&dictionary](const auto entry) {
      const auto frequency{entry.second + 1};  // The first element has count 0 (!)
      if (frequency >= MIN_WORD_FREQ) {
        dictionary.emplace_back(CaseSpace_t::Dictionary_t(entry.first, frequency));
      }
    });
    _word_map.clear();
#if defined(USE_BYTELL_HASH_MAP)
    _word_map.shrink_to_fit();  // Release memory
#endif
    _dicLength = uint32_t(dictionary.size());

    // Sort all by frequency
    std::stable_sort(dictionary.begin(), dictionary.end(), [](const auto& a, const auto& b) -> bool {  //
      return (a.frequency == b.frequency) ? (a.word.compare(b.word) < 0) : (a.frequency > b.frequency);
    });

    _dicLength = (std::min)(_dicLength, LIMIT);

    constexpr auto nameCompare{[](const auto& a, const auto& b) -> bool {  //
      return a.word.compare(b.word) < 0;
    }};

    // Sort 0 .. LOW by name too improve compression
    std::stable_sort(dictionary.begin(), dictionary.begin() + (std::min)(LOW, _dicLength), nameCompare);

    if (_dicLength >= LOW) {
      // Sort LOW .. MID by name too improve compression
      std::stable_sort(dictionary.begin() + LOW, dictionary.begin() + (std::min)(MID, _dicLength), nameCompare);
    }
    if (_dicLength >= MID) {
      // Sort MID .. HIGH by name too improve compression
      std::stable_sort(dictionary.begin() + MID, dictionary.begin() + (std::min)(HIGH, _dicLength), nameCompare);
    }
    if (_dicLength >= HIGH) {
      // Sort HIGH .. _index by name too improve compression
      std::stable_sort(dictionary.begin() + HIGH, dictionary.begin() + _dicLength, nameCompare);
    }

    _word_map.reserve(_dicLength);
    for (uint32_t n{0}; n < _dicLength; ++n) {
      _word_map[dictionary[n].word] = frequency2bytes(n);
    }
    assert(_dicLength == _word_map.size());

    out.putVLI(_dicLength);  // write length of dictionary

    for (uint32_t n{0}; n < _dicLength; ++n) {
      fwrite(dictionary[n].word.c_str(), sizeof(char), dictionary[n].word.length(), out);
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
    _dicLength = uint32_t(stream.getVLI());
    assert(_dicLength <= LIMIT);

    _byte_map.reserve(_dicLength);

    std::string word{};
    for (uint32_t n{0}; n < _dicLength; ++n) {
      int32_t ch;
      while ((SEPARATE_CHAR != (ch = stream.getc()))) {
        word.push_back(char(ch));
      }
      const uint32_t bytes{frequency2bytes(n)};
      _byte_map[bytes] = word;
      word.clear();
    }
  }

  struct word2frequency_result_t {
    uint32_t frequency;
    bool found;
  };
  [[nodiscard]] auto word2frequency(const std::string& word) const noexcept -> word2frequency_result_t {
    const auto it{_word_map.find(word)};
    if (it != _word_map.end()) {
      return {it->second, true};
    }
    return {0, false};
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

  void AppendWord(const std::string& word) noexcept {
    auto it{_word_map.find(word)};
    if (it != _word_map.end()) {
      it->second++;
    } else {
      _word_map[word] = 0;
    }
  }

  [[nodiscard]] auto inputLength() const noexcept -> int64_t final {
    return _inputLength;
  }
  [[nodiscard]] auto outputLength() const noexcept -> int64_t final {
    return _inputLength;
  }
  [[nodiscard]] auto workLength() const noexcept -> int64_t final {
    return _originalLength;
  }
  [[nodiscard]] auto layoutLength() const noexcept -> int64_t final {
    return _originalLength;
  }

#if defined(USE_BYTELL_HASH_MAP)
  ska::bytell_hash_map<std::string, uint32_t> _word_map;  // Does encode enwik9 25% faster!
  ska::bytell_hash_map<uint32_t, std::string> _byte_map;
#else
  std::unordered_map<std::string, uint32_t> _word_map;
  std::unordered_map<uint32_t, std::string> _byte_map;
#endif
  uint32_t _dicLength{0};
  int32_t : 32;  // Padding
  int64_t _originalLength{0};
  int64_t _inputLength{0};
};
Dictionary::~Dictionary() noexcept = default;

class TxtPrep final : public iMonitor_t {
public:
  explicit TxtPrep(File_t& in, File_t& out, const int64_t* charFreq, const std::string& quote)
      : _in{in},  //
        _out{out},
        _quoteLength{uint32_t(quote.length())} {
    assert(_quoteLength < 256);            // Must fit in a byte of 8 bit
    assert(_quoteLength < _quote.size());  // Should fit
    memcpy(&_quote[0], quote.c_str(), _quoteLength);
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
      if (chr09 > (chrAZ * 8)) {
        to_numbers_ = true;
        _quoteLength = 0;
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

  void EncodeChar(const int32_t ch, std::string& word) noexcept {
    if (is_word_char(ch) && (word.length() < MAX_WORD_SIZE)) {
      word.push_back(char(ch));
    } else {
      Encode(word);

      if ((0x80 & ch) || (ESCAPE_CHAR == ch) || (QUOTING_CHAR == ch)) {
        Putc(ESCAPE_CHAR);
      }
      Putc(ch);
    }
  }

  auto Encode() noexcept -> int64_t {
    _originalLength = _in.Size();
    _out.putVLI(_originalLength);
    Putc(uint8_t(_quoteLength));
    for (uint32_t n{0}; n < _quoteLength; ++n) {
      Putc(_quote[n]);
    }

    _dictionary.Create(_in, _out, &_quote[0], _quoteLength);

    const auto data_pos{_out.Position()};

    Progress_t progress("TXT", true, *this);

    _in.Rewind();

    uint32_t quoteState{0};

    std::string value{};

    std::string word{};
    int32_t ch;
    while (EOF != (ch = _in.getc())) {
      if (_quoteLength > 0) {
        if (ch == _quote[quoteState]) {
          ++quoteState;
          if (quoteState == _quoteLength) {
            Encode(word);
            ValidateAndEncodeValue(value);

            Putc(QUOTING_CHAR);
            quoteState = 0;
          }
          continue;
        }
        if (quoteState > 0) {
          Encode(word);
          ValidateAndEncodeValue(value);
          for (uint32_t n{0}; n < quoteState; ++n) {
            EncodeChar(_quote[n], word);
          }
          quoteState = 0;
        }
      }

      if (to_numbers_) {
        EncodeChar(ch, word);
      } else {
        const auto valueLength{value.length()};
        if (is_number(ch) && ((valueLength > 0) || ((0 == valueLength) && ('0' != ch)))) {  // Avoid leading zero's
          value.push_back(char(ch));
          if (valueLength >= 18) {  // The maximum value is 18446744073709551615...
            Encode(word);
            ValidateAndEncodeValue(value);
          }
        } else {
          if (valueLength > 0) {
            Encode(word);
            ValidateAndEncodeValue(value);
          }
          EncodeChar(ch, word);
        }
      }
    }

    for (uint32_t n{0}; n < quoteState; ++n) {
      EncodeChar(_quote[n], word);
    }

    for (const char* __restrict str{value.c_str()}; *str;) {
      Putc(*str++);
    }

    Encode(word);

    return data_pos + uint32_t(sizeof(int64_t));  // Add original length used in CaseSpace
  }

  auto Decode() noexcept -> int64_t {
    _originalLength = _in.getVLI();
    assert(_originalLength > 0);
    _quoteLength = uint32_t(_in.getc());
    for (uint32_t n{_quoteLength}; n--;) {  // Reverse read
      _quote[n] = int8_t(_in.getc());
    }

    Progress_t progress("TXT", false, *this);

    _dictionary.Read(_in);

    while (_outputLength < _originalLength) {
      int32_t ch{_in.getc()};
      assert(EOF != ch);
      if (ESCAPE_CHAR == ch) {
        ch = _in.getc();
        if (!to_numbers_ && (0xF0 == (0xF0 & ch)) && ((0x0F & ch) >= 4)) {
          const auto safe_position{_in.Position()};
          int32_t costs{0x0F & ch};
          uint64_t value{0};
          int32_t k{0};
          int32_t b;
          do {
            b = _in.getc();
            value |= uint64_t(0x3F & b) << k;
            k += 6;
            --costs;
          } while (0x80 & b);
          if (0 == costs) {
            std::array<char, 30> tmp;
            snprintf(&tmp[0], tmp.size(), "%" PRIu64, value);
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
        for (uint32_t n{_quoteLength}; n--;) {  // Reverse write
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

    return _outputLength;
  }

private:
  ALWAYS_INLINE void Putc(int32_t ch) noexcept {
    ++_outputLength;
    _out.putc(ch);
  }

  auto CostsValue(uint64_t value) noexcept -> int32_t {
    int32_t i{1};
    while (value > 0x3F) {
      value >>= 6;
      ++i;
    }
    return i;
  }

  auto EncodeValue(std::string& text) noexcept -> bool {
    uint64_t value{std::strtoull(text.c_str(), nullptr, 10)};
    std::array<char, 32> tmp;
    snprintf(&tmp[0], tmp.size(), "%" PRIu64, value);
    if (!text.compare(&tmp[0])) {  // Avoid leading zeros
      Putc(ESCAPE_CHAR);
      const int32_t costs{CostsValue(value)};
      Putc(0xF0 | costs);
      while (value > 0x3F) {
        Putc(int32_t(0x80 | (0x3F & value)));
        value >>= 6;
      }
      Putc(int32_t(value));
      return true;  // Success
    }
    return false;  // Failure...
  }

  void ValidateAndEncodeValue(std::string& value) noexcept {
    const auto valueLength{value.length()};
    if ((valueLength <= MIN_NUMBER_SIZE) || !EncodeValue(value)) {
      const char* __restrict src{value.c_str()};
      while (*src) {
        Putc(*src++);
      }
    }
    value.clear();
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
        if (0x80 & *literal) {
          Putc(ESCAPE_CHAR);
        }
        Putc(*literal++);
      }
    }
  }

  void Encode(const std::string& cword, const uint32_t length) noexcept {
    const char* __restrict__ word{cword.c_str()};

    if (length >= MIN_WORD_SIZE) {
      const auto wrd{_dictionary.word2frequency(cword)};
      if (wrd.found) {
        EncodeCodeWord(wrd.frequency);
        return;
      }

      uint32_t offset_end{0};
      uint32_t frequency_end{0};

      // Try to find shorter word, strip end of word
      for (uint32_t offset{length - 1}; offset >= MIN_SHORTER_WORD_SIZE; --offset) {
        const std::string shorter(word, offset);
        const auto short_word{_dictionary.word2frequency(shorter)};
        if (short_word.found) {
          offset_end = offset;
          frequency_end = short_word.frequency;
          break;
        }
      }

      uint32_t offset_begin{0};
      uint32_t frequency_begin{0};

      // Try to find shorter word, strip begin of word
      for (uint32_t offset{1}; (length - offset) >= MIN_SHORTER_WORD_SIZE; ++offset) {
        const std::string shorter(word + offset, length - offset);
        const auto short_word{_dictionary.word2frequency(shorter)};
        if (short_word.found) {
          offset_begin = offset;
          frequency_begin = short_word.frequency;
          break;
        }
      }

      if (0 != offset_end) {
        if (0 != offset_begin) {
          if ((length - offset_end) <= offset_begin) {
            EncodeCodeWord(frequency_end);
            Literal(word + offset_end, length - offset_end);
          } else {
            Literal(word, offset_begin);
            EncodeCodeWord(frequency_begin);
          }
        } else {
          EncodeCodeWord(frequency_end);
          Literal(word + offset_end, length - offset_end);
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
    Literal(word, length);
  }

  void Encode(std::string& cword) noexcept {
    auto length{uint32_t(cword.length())};
    Encode(cword, length);
    cword.clear();
  }

  [[nodiscard]] auto inputLength() const noexcept -> int64_t final {
    return _in.Position();
  }
  [[nodiscard]] auto outputLength() const noexcept -> int64_t final {
    return _out.Position();
  }
  [[nodiscard]] auto workLength() const noexcept -> int64_t final {
    return _originalLength;
  }
  [[nodiscard]] auto layoutLength() const noexcept -> int64_t final {
    return _originalLength;
  }

  Dictionary _dictionary;
  File_t& _in;
  File_t& _out;
  int64_t _originalLength{0};
  int64_t _outputLength{0};
  uint32_t _quoteLength;
  int32_t : 32;  // Padding
  std::array<int8_t, 256> _quote{};
};
TxtPrep::~TxtPrep() noexcept = default;

auto encode_txt(File_t& in, File_t& out) noexcept -> int64_t {
  int64_t length{0};
  File_t tmp;  //("_tmp_.cse", "wb+");
  if (tmp.isOpen()) {
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
  File_t tmp;  //("_tmp_.cse", "wb+");
  if (tmp.isOpen()) {
    TxtPrep txtprep(in, tmp, nullptr, "");
    txtprep.Decode();

    tmp.Rewind();

    CaseSpace_t cse(tmp, out);
    length = cse.Decode();
  }
  return length;
}
