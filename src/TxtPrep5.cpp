/* TxtPrep5, is a text preparation for text encoding/decoding
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file LICENSE.
 * If not, see <https://www.gnu.org/licenses/>
 *
 * https://github.com/the-m-master/Moruga
 */
#include "TxtPrep5.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cinttypes>
#include <climits>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>
#include "CaseSpace.h"
#include "File.h"
#include "IntegerXXL.h"
#include "Progress.h"
#include "TxtWords.h"
#include "Utilities.h"
#include "gzip/gzip.h"
#include "iMonitor.h"

// #define DEBUG_WRITE_ANALYSIS_BMP
// #define DEBUG_WRITE_DICTIONARY
#if !defined(CLANG_TIDY)
#  define USE_BYTELL_HASH_MAP  // Enable the fastest hash table by Malte Skarupke
#endif

#if defined(USE_BYTELL_HASH_MAP)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Waggregate-return"
#  pragma GCC diagnostic ignored "-Wc++98-c++11-compat-binary-literal"
#  pragma GCC diagnostic ignored "-Wc++98-compat-pedantic"
#  pragma GCC diagnostic ignored "-Wconversion"
#  pragma GCC diagnostic ignored "-Wdeprecated"
#  pragma GCC diagnostic ignored "-Weffc++"
#  pragma GCC diagnostic ignored "-Wpadded"
#  pragma GCC diagnostic ignored "-Wshadow"
#  pragma GCC diagnostic ignored "-Wsign-conversion"
#  include "ska/bytell_hash_map.hpp"
#  pragma GCC diagnostic pop
#else
#  include <unordered_map>
#endif

#if defined(USE_BYTELL_HASH_MAP)
using map_string2uint_t = ska::bytell_hash_map<std::string, uint32_t>;  // Does encode enwik9 25% faster!
using map_uint2string_t = ska::bytell_hash_map<uint32_t, std::string>;
#else
using map_string2uint_t = std::unordered_map<std::string, uint32_t>;
using map_uint2string_t = std::unordered_map<uint32_t, std::string>;
#endif

#if defined(DEBUG_WRITE_ANALYSIS_BMP)
#  include "Analysis.h"
#endif

// Word count can be verified by using (on Linux or MSYS2):
// sed -e 's/[^[:alpha:]]/ /g' file2count.txt | tr '\n' " " |  tr -s " " | tr " " '\n'| tr 'A-Z' 'a-z' | sort | uniq -c | sort -nr | nl

static constexpr auto MAX_WORD_SIZE{UINT32_C(256)};                        // Default 256
static constexpr auto MIN_WORD_SIZE{UINT32_C(2)};                          // Default 2, range 1 .. MAX_WORD_SIZE
static constexpr auto MIN_WORD_FREQ{UINT32_C(4)};                          // Default 4, range 1 .. 256
static constexpr auto MIN_SHORTER_WORD_SIZE{MIN_WORD_SIZE + UINT32_C(5)};  // Default 5, range always larger then MIN_WORD_SIZE
static constexpr auto MIN_NUMBER_SIZE{UINT32_C(7)};                        // Default 7, range 1 .. 26
static constexpr auto MAX_NUMBER_SIZE{UINT32_C(20)};                       // Default 20, range 1 .. 26

static_assert(MIN_WORD_SIZE < MIN_SHORTER_WORD_SIZE, "MIN_SHORTER_WORD_SIZE must be bigger then MIN_WORD_SIZE");
static_assert(MIN_WORD_FREQ >= 1, "MIN_WORD_FREQ must be equal or larger then one");
static_assert(MIN_NUMBER_SIZE < MAX_NUMBER_SIZE, "MAX_NUMBER_SIZE must be bigger then MIN_NUMBER_SIZE");

static constexpr auto BITS_OUT{UINT32_C(6)};                                // Limit of 6 bits
static constexpr auto LOW{UINT32_C(1) << BITS_OUT};                         //    0..3F    --> 0..64 (6 bits)
static constexpr auto MID{LOW + (UINT32_C(1) << ((2 * BITS_OUT) - 1))};     //   40..880   --> 0..840 (2^6 + 2^(6+5)                             --> 64 + 2048                  -->   2112)
static constexpr auto HIGH{MID + (UINT32_C(1) << ((3 * BITS_OUT) - 3))};    //  881..8880  --> 0..8840 (2^6 + 2^(6+5) + 2^(6+5+4)                --> 64 + 2048 + 32768          -->  34880)
static constexpr auto LIMIT{HIGH + (UINT32_C(1) << ((4 * BITS_OUT) - 6))};  // 8881..510C0 --> 0..48840 (2^6 + 2^(6+5) + 2^(6+5+4) + 2^(6+5+4+3) --> 64 + 2048 + 32768 + 262144 --> 297024)

static_assert(BITS_OUT > 1, "Bit range error");
static_assert(LOW > 1, "Bit range error, LOW must larger then one");
static_assert(LOW < MID, "Bit range error, LOW must be less then MID");
static_assert(MID < HIGH, "Bit range error, MID must be less then HIGH");
static_assert(HIGH < LIMIT, "Bit range error, HIGH must be less then LIMIT");

static constexpr auto HGH_SECTION{UINT32_C(0x00FFFFFF)};
static constexpr auto MID_SECTION{UINT32_C(0x0000FFFF)};
static constexpr auto LOW_SECTION{UINT32_C(0x000000FF)};

static bool to_numbers_{false};

namespace TxtPrep5 {
  template <typename T>
  ALWAYS_INLINE constexpr auto is_word_char(const T ch) noexcept -> bool {
    if (('>' == ch) || Utilities::is_lower(ch) || (ch > 127)) {  // Default for text files
      return true;
    }
    return to_numbers_ && ((' ' == ch) || ('.' == ch) || Utilities::is_number(ch));  // In case of a file with a lot of values
  }
}  // namespace TxtPrep5

class GZip_t final {
public:
  GZip_t() noexcept = default;
  ~GZip_t() noexcept {
    _emap.clear();
    _dmap.clear();
#if defined(USE_BYTELL_HASH_MAP)
    _emap.shrink_to_fit();  // Release memory
    _dmap.shrink_to_fit();  // Release memory
#endif
  }

  GZip_t(const GZip_t&) = delete;
  GZip_t(GZip_t&&) = delete;
  auto operator=(const GZip_t&) -> GZip_t& = delete;
  auto operator=(GZip_t&&) -> GZip_t& = delete;

  auto load_word_emap() noexcept -> map_string2uint_t {
    _emap.reserve(static_words.size());
    const int32_t status{gzip::unzip(static_words.data(), static_cast<uint32_t>(static_words.size()), write_ebuffer, this)};
    assert(GZip_OK == status);
    (void)status;  // Avoid warning in release mode
    assert(LIMIT == _emap.size());
    return _emap;
  }

  auto load_word_dmap() noexcept -> map_uint2string_t {
    _dmap.reserve(static_words.size());
    const int32_t status{gzip::unzip(static_words.data(), static_cast<uint32_t>(static_words.size()), write_dbuffer, this)};
    assert(GZip_OK == status);
    (void)status;  // Avoid warning in release mode
    assert(LIMIT == _dmap.size());
    return _dmap;
  }

private:
  std::string _word{};
  map_string2uint_t _emap{};
  map_uint2string_t _dmap{};
  uint32_t _word_index{0};
  int32_t : 32;  // Padding

  auto write_ebuffer(const void* buf_, uint32_t cnt) noexcept -> uint32_t {
    const char* buf{static_cast<const char*>(buf_)};
    for (uint32_t n{0}; n < cnt; ++n) {
      const char ch{buf[n]};
      if ('\n' == ch) {
        _emap[_word] = _word_index++;
        _word.clear();
      } else {
        _word.push_back(ch);
      }
    }
    return cnt;
  }

  static auto write_ebuffer(const void* buf, uint32_t cnt, void* this_pointer) noexcept -> uint32_t {
    GZip_t* const self{static_cast<GZip_t*>(this_pointer)};
    assert(buf && cnt && self);
    return self->write_ebuffer(buf, cnt);
  }

  auto write_dbuffer(const void* buf_, uint32_t cnt) noexcept -> uint32_t {
    const char* buf{static_cast<const char*>(buf_)};
    for (uint32_t n{0}; n < cnt; ++n) {
      const char ch{buf[n]};
      if ('\n' == ch) {
        _dmap[_word_index++] = _word;
        _word.clear();
      } else {
        _word.push_back(ch);
      }
    }
    return cnt;
  }

  static auto write_dbuffer(const void* buf, uint32_t cnt, void* this_pointer) noexcept -> uint32_t {
    GZip_t* const self{static_cast<GZip_t*>(this_pointer)};
    assert(buf && cnt && self);
    return self->write_dbuffer(buf, cnt);
  }
};

class Dictionary final : public iMonitor_t {
public:
  Dictionary() noexcept = default;
  ~Dictionary() noexcept override;

  Dictionary(const Dictionary&) = delete;
  Dictionary(Dictionary&&) = delete;
  auto operator=(const Dictionary&) -> Dictionary& = delete;
  auto operator=(Dictionary&&) -> Dictionary& = delete;

  void AppendChar(const int32_t ch) noexcept {
    if (const auto wlength{_word.length()}; TxtPrep5::is_word_char(ch) && (wlength < MAX_WORD_SIZE)) {
      _word.push_back(static_cast<char>(ch));
    } else {
      if (wlength >= MIN_WORD_SIZE) {
        AppendWord();
      }
      _word.clear();
    }
  }

  void Create(const File_t& in, File_t& out, const int8_t* const quote, const size_t quoteLength) noexcept {
    _original_length = in.Size();
    const Progress_t progress("DIC", true, *this);

    _word_map.reserve(LIMIT);

    size_t quoteState{0};

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

    std::for_each(_word_map.begin(), _word_map.end(), [&dictionary](const auto& entry) noexcept {
      if (const auto frequency{entry.second + 1}; frequency >= MIN_WORD_FREQ) {  // The first element has count 0 (!)
        dictionary.emplace_back(CaseSpace_t::Dictionary_t(entry.first, frequency));
      }
    });
    _word_map.clear();
#if defined(USE_BYTELL_HASH_MAP)
    _word_map.shrink_to_fit();  // Release memory
#endif

    // Sort all by frequency and length
    std::stable_sort(dictionary.begin(), dictionary.end(), [](const auto& a, const auto& b) noexcept -> bool {  //
      if (a.frequency == b.frequency) {
        if (a.word.length() == b.word.length()) {
          return a.word.compare(b.word) < 0;
        }
        return a.word.length() > b.word.length();
      }
      return a.frequency > b.frequency;
    });

    // Remove items that are too short in relation to their frequency
    {
      uint32_t index{0};
      uint32_t bytes{MIN_WORD_SIZE};
      dictionary.erase(std::remove_if(dictionary.begin(), dictionary.end(),
                                      [&](const auto& item) noexcept -> bool {
                                        if (item.word.length() < bytes) {
                                          return true;
                                        }
                                        if ((LOW == index) || (MID == index) || (HIGH == index)) {
                                          ++bytes;
                                        }
                                        ++index;
                                        return false;
                                      }),
                       dictionary.end());
    }

    _dic_length = static_cast<uint32_t>(dictionary.size());

#if defined(DEBUG_WRITE_DICTIONARY)
    {
      File_t txt("dictionary_raw.txt", "wb+");
      for (size_t n{0}, bytes{1}; n < _dic_length; ++n) {
        if ((LOW == n) || (MID == n) || (HIGH == n)) {
          ++bytes;
        }
        if (LIMIT == n) {
          bytes = 0;
        }
        fprintf(txt, "%" PRIu32 ", %7" PRIu32 " %s\n", bytes, dictionary[n].frequency, dictionary[n].word.c_str());
      }
    }
#endif

    _dic_length = (std::min)(_dic_length, LIMIT);

    constexpr auto name_compare{[](const auto& a, const auto& b) noexcept -> bool {  //
      return a.word.compare(b.word) < 0;
    }};

    // Sort 0 .. LOW by name too improve compression
    std::stable_sort(dictionary.begin(), dictionary.begin() + (std::min)(LOW, _dic_length), name_compare);

    if (_dic_length >= LOW) {
      // Sort LOW .. MID by name too improve compression
      std::stable_sort(dictionary.begin() + LOW, dictionary.begin() + (std::min)(MID, _dic_length), name_compare);
      if (_dic_length >= MID) {
        // Sort MID .. HIGH by name too improve compression
        std::stable_sort(dictionary.begin() + MID, dictionary.begin() + (std::min)(HIGH, _dic_length), name_compare);
        if (_dic_length >= HIGH) {
          // Sort HIGH .. _index by name too improve compression
          std::stable_sort(dictionary.begin() + HIGH, dictionary.begin() + _dic_length, name_compare);
        }
      }
    }

    _word_map.reserve(_dic_length);
    for (uint32_t n{0}; n < _dic_length; ++n) {
      _word_map[dictionary[n].word] = frequency2bytes(n);
    }
    assert(_dic_length == _word_map.size());

    out.putVLI(_dic_length);  // write length of dictionary
    if (_dic_length > 0) {
      GZip_t nice{};
      auto map{nice.load_word_emap()};

      bool in_sync{false};
      for (uint32_t n{0}, m{0}, delta{0}; n < _dic_length; ++n) {
        const auto& word{dictionary[n].word};

        const auto it{map.find(word)};
        if (it != map.end()) {
          if (n == it->second) {
            in_sync = false;
            m = n;
          } else {
            write_value(out, frequency2bytes(n));
            auto frequency{static_cast<int32_t>(it->second - delta)};
            if (frequency < 0) {
              out.putc('-');
              frequency = -frequency;
            }
            write_value(out, frequency2bytes(static_cast<uint32_t>(frequency)));
            delta = it->second;
            in_sync = true;
            m = n + 1;
          }
        } else {
          in_sync = true;
          if (n != m) {
            write_value(out, frequency2bytes(n));
            auto frequency{static_cast<int32_t>(m - delta)};
            if (frequency < 0) {
              out.putc('-');
              frequency = -frequency;
            }
            write_value(out, frequency2bytes(static_cast<uint32_t>(frequency)));
            delta = m;
            m = n;
          }
          ++m;
          WriteLiteral(out, word.c_str(), word.length());
        }
      }

      if (!in_sync) {
        write_value(out, frequency2bytes(_dic_length - 1));
        write_value(out, frequency2bytes(_dic_length - 1));
      }
    }

#if defined(DEBUG_WRITE_DICTIONARY)
    {
      File_t txt("dictionary_final.txt", "wb+");
      for (uint32_t n{0}, bytes{1}; n < _dic_length; ++n) {
        if ((LOW == n) || (MID == n) || (HIGH == n)) {
          ++bytes;
        }
        if (LIMIT == n) {
          bytes = 0;
        }
        // fprintf(txt, "%" PRIu32 ", %7" PRIu32 " %s\n", bytes, dictionary[n].frequency, dictionary[n].word.c_str());
        fprintf(txt, "%s\n", dictionary[n].word.c_str());
      }
    }
#endif
  }

  void Read(const File_t& stream) noexcept {
    _dic_length = static_cast<uint32_t>(stream.getVLI());
    assert(_dic_length <= LIMIT);

    _byte_map.reserve(_dic_length);

    GZip_t nice{};
    auto map{nice.load_word_dmap()};

    bool sign{false};
    int32_t delta{0};
    std::string word{};
    for (uint32_t n{0}; n < _dic_length; ++n) {
      int32_t ch{stream.getc()};
      if ('-' == ch) {
        sign = true;
        continue;
      }
      if (0x80 == (0x80 & ch)) {
        const auto origin{stream.Position()};
        const uint32_t sync_index{read_value(stream, ch)};  // n
        if ((sync_index < n) || (sync_index > _dic_length)) {
          stream.Seek(origin);
          const auto bytes{frequency2bytes(n)};
          _byte_map[bytes] = ReadLiteral(stream, ch);
        } else {
          ch = stream.getc();
          if ('-' == ch) {
            sign = true;
            ch = stream.getc();
          }
          auto freqency{static_cast<int32_t>(read_value(stream, ch))};  // --> word
          if (sign) {
            sign = false;
            freqency = -freqency;
          }
          const auto word_index{static_cast<uint32_t>(freqency + delta)};
          delta = static_cast<int32_t>(word_index);

          if (((_dic_length - 1) == sync_index) && (sync_index == word_index)) {
            for (; n < _dic_length; ++n) {
              const auto bytes{frequency2bytes(n)};
              _byte_map[bytes] = map[n];
            }
            continue;
          }

          while (n < sync_index) {
            const auto bytes{frequency2bytes(n)};
            word = map[n];
            _byte_map[bytes] = word;
            ++n;
          }

          if (map[word_index].compare(word)) {
            const auto bytes{frequency2bytes(n)};  // index == sync_index
            _byte_map[bytes] = map[word_index];
          } else {
            --n;
          }
          word.clear();
        }
      } else {
        const auto bytes{frequency2bytes(n)};
        _byte_map[bytes] = ReadLiteral(stream, ch);
      }
    }

#if defined(DEBUG_WRITE_DICTIONARY)
    {
      File_t txt("dictionary_decode.txt", "wb+");
      for (uint32_t n{0}; n < _dic_length; ++n) {
        const auto bytes{frequency2bytes(n)};
        auto w = _byte_map[bytes];
        fprintf(txt, "%s\n", w.c_str());
      }
    }
#endif
  }

  [[nodiscard]] auto word2frequency(const std::string& word) const noexcept -> std::pair<bool, uint32_t> {
    if (const auto it{_word_map.find(word)}; it != _word_map.end()) {
      return {true, it->second};  // Is found, frequency
    }
    return {false, 0};  // Not found...
  }

  auto bytes2word(const uint32_t bytes) noexcept -> const std::string& {
    return _byte_map[bytes];
  }

private:
  static constexpr auto BLOCK_SIZE{UINT32_C(1) << 16};

  [[nodiscard]] auto frequency2bytes(uint32_t frequency) noexcept -> uint32_t {
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

  void write_value(const File_t& stream, const uint32_t bytes) noexcept {
    if (bytes > HGH_SECTION) {
      stream.putc(static_cast<uint8_t>(bytes >> 24));
      stream.putc(static_cast<uint8_t>(bytes >> 16));
      stream.putc(static_cast<uint8_t>(bytes >> 8));
    } else if (bytes > MID_SECTION) {
      stream.putc(static_cast<uint8_t>(bytes >> 16));
      stream.putc(static_cast<uint8_t>(bytes >> 8));
    } else if (bytes > LOW_SECTION) {
      stream.putc(static_cast<uint8_t>(bytes >> 8));
    }
    stream.putc(static_cast<uint8_t>(bytes));
  }

  auto read_to_value(const uint32_t bytes) noexcept -> uint32_t {
    uint32_t value;
    if (bytes > HGH_SECTION) {
      value = /*          */ ((bytes >> 24) & 0x07);  // 3 bits
      value = (value << 4) | ((bytes >> 16) & 0x0F);  // 4 bits
      value = (value << 5) | ((bytes >> 8) & 0x1F);   // 5 bits
      value = (value << 6) | (bytes & 0x3F);          // 6 bits
      value += HIGH;
    } else if (bytes > MID_SECTION) {
      value = /*          */ ((bytes >> 16) & 0x0F);  // 4 bits
      value = (value << 5) | ((bytes >> 8) & 0x1F);   // 5 bits
      value = (value << 6) | (bytes & 0x3F);          // 6 bits
      value += MID;
    } else if (bytes > LOW_SECTION) {
      value = /*          */ (bytes >> 8) & 0x1F;  // 5 bits
      value = (value << 6) | (bytes & 0x3F);       // 6 bits
      value += LOW;
    } else {
      value = bytes & 0x3F;  // 6 bits
    }
    return value;
  }

  auto read_value(const File_t& stream, int32_t ch) noexcept -> uint32_t {
    int32_t bytes{ch};
    if (0xC0 == (0xC0 & ch)) {
      ch = stream.getc();
      assert(EOF != ch);
      bytes = (bytes << 8) | ch;
      if (0xC0 == (0xC0 & ch)) {
        ch = stream.getc();
        assert(EOF != ch);
        bytes = (bytes << 8) | ch;
        if (0xC0 == (0xC0 & ch)) {
          ch = stream.getc();
          assert(EOF != ch);
          bytes = (bytes << 8) | ch;
        }
      }
    }
    return read_to_value(static_cast<uint32_t>(bytes));
  }

  [[nodiscard]] auto ReadLiteral(const File_t& stream, int32_t ch) noexcept -> std::string {
    std::string word{};
    if (TP5_ESCAPE_CHAR != ch) {
      word.push_back(static_cast<char>(ch));
    }
    while ((TP5_SEPARATE_CHAR != (ch = stream.getc()))) {
      assert(EOF != ch);
      if (TP5_ESCAPE_CHAR == ch) {
        continue;
      }
      word.push_back(static_cast<char>(ch));
    }
    return word;
  }

  void WriteLiteral(const File_t& stream, const char* __restrict literal, size_t length) noexcept {
    assert(length > 0);
    if (length > 0) {
      while (length-- > 0) {
        const int32_t ch{*literal++};
        if (0x80 & ch) {
          stream.putc(TP5_ESCAPE_CHAR);
        }
        stream.putc(ch);
      }
    }
    stream.putc(TP5_SEPARATE_CHAR);
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

  map_string2uint_t _word_map{};
  map_uint2string_t _byte_map{};
  int64_t _original_length{0};
  int64_t _input_length{0};
  uint32_t _dic_length{0};
  int32_t : 32;  // Padding
  std::string _word{};
};
Dictionary::~Dictionary() noexcept = default;

class TxtPrep final : public iMonitor_t {
public:
  explicit TxtPrep(File_t& in, File_t& out, const std::array<int64_t, 256>* const charFreq, const std::string& quote) noexcept
      : _in{in},  //
        _out{out},
        _qlength{quote.length()} {
    assert(_qlength < 256);            // Must fit in a byte of 8 bit
    assert(_qlength < _quote.size());  // Should fit
    memcpy(_quote.data(), quote.c_str(), _qlength);
    to_numbers_ = false;
    if (nullptr != charFreq) {
      int64_t chrAZ{0};
      int64_t chr09{0};
      for (uint32_t n{'0'}; n <= '9'; ++n) {
        chr09 += (*charFreq)[n];
      }
      for (uint32_t n{'A'}; n <= 'Z'; ++n) {
        chrAZ += (*charFreq)[n];
      }
      for (uint32_t n{'a'}; n <= 'z'; ++n) {
        chrAZ += (*charFreq)[n];
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
    Putc(static_cast<uint8_t>(_qlength));
    for (size_t n{0}; n < _qlength; ++n) {
      Putc(_quote[n]);
    }

    _dictionary.Create(_in, _out, _quote.data(), _qlength);

    const auto data_pos{_out.Position()};

    _in.Rewind();

    const Progress_t progress("TXT", true, *this);

    uint32_t qoffset{0};

    int32_t ch;
    while (EOF != (ch = _in.getc())) {
      if (_qlength > 0) {
        if (ch == _quote[qoffset]) {
          ++qoffset;
          if (qoffset == _qlength) {
            EncodeWordValue();
            Putc(TP5_QUOTING_CHAR);
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

    return data_pos + static_cast<uint32_t>(sizeof(int64_t));  // Add original length used in CaseSpace
  }

  auto Decode() noexcept -> int64_t {
    _original_length = _in.getVLI();
    assert(_original_length > 0);
    _qlength = static_cast<size_t>(_in.getc());
    for (size_t n{_qlength}; n--;) {  // Reverse read
      _quote[n] = static_cast<int8_t>(_in.getc());
    }

    const Progress_t progress("TXT", false, *this);

    _dictionary.Read(_in);

    while (_output_length < _original_length) {
      int32_t ch{_in.getc()};
      assert(EOF != ch);
      if (TP5_ESCAPE_CHAR == ch) {
        ch = _in.getc();
        assert(EOF != ch);
        if (!to_numbers_ && (0xF0 == (0xF0 & ch)) && ((0x0F & ch) >= 4)) {
          const auto safe_position{_in.Position()};
          int32_t costs{0x0F & ch};
          uint128_t value{0};
          int32_t k{0};
          int32_t b{0};
          do {
            if (EOF == (b = _in.getc())) {
              assert(false);
              break;  // Should never happen...
            }
            value |= static_cast<uint128_t>(0x3F & b) << k;
            k += 6;
            --costs;
          } while ((k < 127) && (0x80 == (0xC0 & b)));
          if (0 == costs) {
            std::array<char, 64> tmp{xxltostr(value)};
            for (const char* __restrict str{tmp.data()}; *str;) {
              Putc(*str++);
            }
          } else {
            _in.Seek(safe_position);
            Putc(ch);
          }
        } else {
          Putc(ch);
        }
      } else if (TP5_QUOTING_CHAR == ch) {
        for (size_t n{_qlength}; n--;) {  // Reverse write
          Putc(_quote[n]);
        }
        continue;
      } else if (0x80 & ch) {
        int32_t bytes{ch};
        if (0xC0 == (0xC0 & ch)) {
          ch = _in.getc();
          assert(EOF != ch);
          bytes = (bytes << 8) | ch;
          if (0xC0 == (0xC0 & ch)) {
            ch = _in.getc();
            assert(EOF != ch);
            bytes = (bytes << 8) | ch;
            if (0xC0 == (0xC0 & ch)) {
              ch = _in.getc();
              assert(EOF != ch);
              bytes = (bytes << 8) | ch;
            }
          }
        }

        const std::string& word{_dictionary.bytes2word(static_cast<uint32_t>(bytes))};
        assert(word.length() > 0);
        for (const char* __restrict str{word.c_str()}; *str;) {
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
    if (bytes > HGH_SECTION) {
      Putc(static_cast<uint8_t>(bytes >> 24));
      Putc(static_cast<uint8_t>(bytes >> 16));
      Putc(static_cast<uint8_t>(bytes >> 8));
    } else if (bytes > MID_SECTION) {
      Putc(static_cast<uint8_t>(bytes >> 16));
      Putc(static_cast<uint8_t>(bytes >> 8));
    } else if (bytes > LOW_SECTION) {
      Putc(static_cast<uint8_t>(bytes >> 8));
    }
    Putc(static_cast<uint8_t>(bytes));
  }

  void Literal(const char* __restrict literal, size_t length) noexcept {
    if (length > 0) {
      while (length-- > 0) {
        const int32_t ch{*literal++};
        if (0x80 & ch) {
          Putc(TP5_ESCAPE_CHAR);
        }
        Putc(ch);
      }
    }
  }

  void PreLiteral(const char* __restrict literal, size_t length) noexcept {
    if (length >= MIN_SHORTER_WORD_SIZE) {
      const std::string word(literal, length);
      const auto [found, frequency]{_dictionary.word2frequency(word)};
      if (found) {
        EncodeCodeWord(frequency);
        return;
      }
    }
    Literal(literal, length);
  }

  void EncodeWord() noexcept {
    const auto wlength{_word.length()};
    const char* __restrict word{_word.c_str()};

    if (wlength >= MIN_WORD_SIZE) {
      if (const auto [found, frequency]{_dictionary.word2frequency(_word)}; found) {
        EncodeCodeWord(frequency);
        return;
      }

      if (wlength > MIN_SHORTER_WORD_SIZE) {
        // Try to find a shorter word, start shortening at the end of the word
        size_t offset_end{0};
        size_t frequency_end{0};
        for (size_t offset{wlength - 1}; offset >= MIN_SHORTER_WORD_SIZE; --offset) {
          const std::string shorter(word, offset);
          if (const auto [found, frequency]{_dictionary.word2frequency(shorter)}; found) {
            offset_end = offset;
            frequency_end = frequency;
            break;
          }
        }

        // Try to find a shorter word, start shortening at the beginning of the word
        size_t offset_begin{0};
        size_t frequency_begin{0};
        for (size_t offset{1}; (wlength - offset) >= MIN_SHORTER_WORD_SIZE; ++offset) {
          const std::string shorter(word + offset, wlength - offset);
          if (const auto [found, frequency]{_dictionary.word2frequency(shorter)}; found) {
            offset_begin = offset;
            frequency_begin = frequency;
            break;
          }
        }

        bool write_begin_word{false};
        bool write_end_word{false};

        if (0 != offset_end) {
          if (0 != offset_begin) {
            if ((wlength - offset_end) <= offset_begin) {
              write_end_word = true;
            } else {
              write_begin_word = true;
            }
          } else {
            write_end_word = true;
          }
        } else {
          if (0 != offset_begin) {
            write_begin_word = true;
          }
        }

        if (write_begin_word) {
          PreLiteral(word, offset_begin);
          EncodeCodeWord(static_cast<uint32_t>(frequency_begin));
          return;
        }
        if (write_end_word) {
          EncodeCodeWord(static_cast<uint32_t>(frequency_end));
          PreLiteral(word + offset_end, wlength - offset_end);
          return;
        }

        // Try to find a shorter word, start shortening at the beginning and limiting the length of the word
        for (size_t offset{1}; offset < (wlength - 1); ++offset) {
          for (size_t length{wlength - offset}; length >= MIN_SHORTER_WORD_SIZE; --length) {
            const std::string shorter(_word, offset, length);
            const auto [found, frequency]{_dictionary.word2frequency(shorter)};
            if (found && (frequency < HIGH)) {
              Literal(_word.c_str(), offset);
              EncodeCodeWord(frequency);
              Literal(_word.c_str() + offset + length, wlength - offset - length);
              return;
            }
          }
        }
      }
    }

    // Not found ...
    Literal(word, wlength);
  }

  void EncodeWordValue() noexcept {
    EncodeWord();
    _word.clear();

    const auto vlength{static_cast<uint32_t>(_value.length())};
    if (vlength > 0) {
      if ((vlength <= MIN_NUMBER_SIZE) || !EncodeValue()) {
        Literal(_value.c_str(), vlength);
      }
      _value.clear();
    }
  }

  [[nodiscard]] auto CostsValue(uint128_t value) const noexcept -> int32_t {
    int32_t i{1};
    while (value > 0x3F) {
      value >>= 6;
      ++i;
    }
    return i;
  }

  [[nodiscard]] auto xxltostr(const uint128_t value) const noexcept -> std::array<char, 64> {
    std::array<char, 64> str{};
    if (value > ULLONG_MAX) {
      //                         ULLONG_MAX 18446744073709551615ULL
      static constexpr uint128_t P10_UINT64{10000000000000000000_xxl};  // 19 zeroes
      const uint64_t leading{static_cast<uint64_t>(value / P10_UINT64)};
      const uint64_t trailing{static_cast<uint64_t>(value % P10_UINT64)};
      snprintf(str.data(), str.size(), "%" PRIu64 "%.19" PRIu64, leading, trailing);
    } else {
      snprintf(str.data(), str.size(), "%" PRIu64, static_cast<uint64_t>(value));
    }
    return str;
  }

  [[nodiscard]] auto strtoxxl(const char* __restrict src) const noexcept -> uint128_t {
    uint128_t value{0};
    while (*src) {
      value *= 10;
      value += static_cast<uint128_t>(*src - '0');
      ++src;
    }
    return value;
  }

  auto EncodeValue() noexcept -> bool {
    uint128_t value{strtoxxl(_value.c_str())};
    std::array<char, 64> tmp{xxltostr(value)};
    if (!_value.compare(tmp.data())) {  // Avoid leading zeros
      Putc(TP5_ESCAPE_CHAR);
      const int32_t costs{CostsValue(value)};
      assert(costs >= 0x04);  // Bottom limit
      assert(costs <= 0x0C);  // Practical limit (Physical limit 0x0F)
      Putc(0xF0 | costs);
      while (value > 0x3F) {
        Putc(0x80 | static_cast<int32_t>(0x3F & value));
        value >>= 6;
      }
      Putc(static_cast<int32_t>(value));
      return true;  // Success
    }
    return false;  // Failure...
  }

  void EncodeChar(const int32_t ch) noexcept {
    if (!to_numbers_) {
      const auto vlength{_value.length()};
      if (Utilities::is_number(ch) && ((vlength > 0) || ((0 == vlength) && ('0' != ch)))) {  // Avoid leading zero's
        _value.push_back(static_cast<char>(ch));
        if (vlength >= MAX_NUMBER_SIZE) {  // The maximum value is 340282366920938463463374607431768211455...
          EncodeWordValue();
        }
        return;
      }
      if (vlength > 0) {
        EncodeWordValue();
      }
    }

    if (TxtPrep5::is_word_char(ch) && (_word.length() < MAX_WORD_SIZE)) {
      _word.push_back(static_cast<char>(ch));
    } else {
      EncodeWordValue();

      if ((0x80 & ch) || (TP5_ESCAPE_CHAR == ch) || (TP5_QUOTING_CHAR == ch)) {
        Putc(TP5_ESCAPE_CHAR);
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

  Dictionary _dictionary{};
  File_t& _in;
  File_t& _out;
  int64_t _original_length{0};
  int64_t _output_length{0};
  size_t _qlength;
  std::array<int8_t, 256> _quote{};
  int32_t : 32;  // Padding
  int32_t : 32;  // Padding
  std::string _word{};
  std::string _value{};
};
TxtPrep::~TxtPrep() noexcept = default;

auto encode_txt(File_t& in, File_t& out) noexcept -> int64_t {
  int64_t length{0};
  if (File_t tmp /*("_tmp_.cse", "wb+")*/; tmp.isOpen()) {
    std::array<int64_t, 256> cf{};
    std::string quote{};

    {
      CaseSpace_t cse(in, tmp);
      cse.Encode();
      cf = cse.charFrequency();
      quote = cse.getQuote();
    }

    tmp.Rewind();

#if defined(DEBUG_WRITE_ANALYSIS_BMP)
    Analysis_t analysis(cse.charFrequency());
    analysis.Write();
#endif

    TxtPrep txtprep(tmp, out, &cf, quote);
    length = txtprep.Encode();
  }

  return length;
}

auto decode_txt(File_t& in, File_t& out) noexcept -> int64_t {
  int64_t length{0};
  if (File_t tmp /*("_tmp_.cse", "wb+")*/; tmp.isOpen()) {
    {
      TxtPrep txtprep(in, tmp, nullptr, "");
      txtprep.Decode();
    }

    tmp.Rewind();

    CaseSpace_t cse(tmp, out);
    length = cse.Decode();
  }
  return length;
}
