/* TxtPrep5, is a text preparation for text encoding/decoding
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
#include "TxtPrep5.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cinttypes>
#include <climits>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
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
#include "ska/ska.h"

// #define DEBUG_WRITE_ANALYSIS_BMP
// #define DEBUG_WRITE_DICTIONARY

#if defined(DEBUG_WRITE_ANALYSIS_BMP)
#  include "Analysis.h"
#endif

// Word count can be verified by using (on Linux or MSYS2):
// sed -e 's/[^[:alpha:]]/ /g' file2count.txt | tr '\n' " " |  tr -s " " | tr " " '\n'| tr 'A-Z' 'a-z' | sort | uniq -c | sort -nr | nl

namespace {
  constexpr auto MAX_WORD_SIZE{UINT32_C(256)};                        // Default 256
  constexpr auto MIN_WORD_SIZE{UINT32_C(2)};                          // Default 2, range 1 .. MAX_WORD_SIZE
  constexpr auto MIN_WORD_FREQ{UINT32_C(4)};                          // Default 4, range 1 .. 256
  constexpr auto MIN_SHORTER_WORD_SIZE{MIN_WORD_SIZE + UINT32_C(5)};  // Default 5, range always larger then MIN_WORD_SIZE
  constexpr auto MIN_NUMBER_SIZE{UINT32_C(7)};                        // Default 7, range 1 .. 26
  constexpr auto MAX_NUMBER_SIZE{UINT32_C(20)};                       // Default 20, range 1 .. 26

  static_assert(MIN_WORD_SIZE < MIN_SHORTER_WORD_SIZE, "MIN_SHORTER_WORD_SIZE must be bigger then MIN_WORD_SIZE");
  static_assert(MIN_WORD_FREQ >= 1, "MIN_WORD_FREQ must be equal or larger then one");
  static_assert(MIN_NUMBER_SIZE < MAX_NUMBER_SIZE, "MAX_NUMBER_SIZE must be bigger then MIN_NUMBER_SIZE");

  constexpr auto BITS_OUT{UINT32_C(6)};                                // Limit of 6 bits
  constexpr auto LOW{UINT32_C(1) << BITS_OUT};                         //    0..3F    --> 0..64 (6 bits)
  constexpr auto MID{LOW + (UINT32_C(1) << ((2 * BITS_OUT) - 1))};     //   40..880   --> 0..840 (2^6 + 2^(6+5)                             --> 64 + 2048                  -->   2112)
  constexpr auto HIGH{MID + (UINT32_C(1) << ((3 * BITS_OUT) - 3))};    //  881..8880  --> 0..8840 (2^6 + 2^(6+5) + 2^(6+5+4)                --> 64 + 2048 + 32768          -->  34880)
  constexpr auto LIMIT{HIGH + (UINT32_C(1) << ((4 * BITS_OUT) - 6))};  // 8881..510C0 --> 0..48840 (2^6 + 2^(6+5) + 2^(6+5+4) + 2^(6+5+4+3) --> 64 + 2048 + 32768 + 262144 --> 297024)

  static_assert(BITS_OUT > 1, "Bit range error");
  static_assert(LOW > 1, "Bit range error, LOW must larger then one");
  static_assert(LOW < MID, "Bit range error, LOW must be less then MID");
  static_assert(MID < HIGH, "Bit range error, MID must be less then HIGH");
  static_assert(HIGH < LIMIT, "Bit range error, HIGH must be less then LIMIT");

  constexpr auto HGH_SECTION{UINT32_C(0x00FFFFFF)};
  constexpr auto MID_SECTION{UINT32_C(0x0000FFFF)};
  constexpr auto LOW_SECTION{UINT32_C(0x000000FF)};
  constexpr auto UNUSED{UINT32_C(~0)};

  bool to_numbers_{false};

  template <typename T>
  ALWAYS_INLINE constexpr auto is_word_char(const T ch) noexcept -> bool {
    if (('>' == ch) || Utilities::is_lower(ch) || (ch > 127)) {  // Default for text files
      return true;
    }
    return to_numbers_ && ((' ' == ch) || ('.' == ch) || Utilities::is_number(ch));  // In case of a file with a lot of values
  }

  // 3 bits encoding 0b1111.0xxx
  // 4 bits encoding 0b1110.xxxx
  // 5 bits encoding 0b110x.xxxx
  // 6 bits encoding 0b10xx.xxxx

  [[nodiscard]] constexpr auto FrequencyToBytes(uint32_t frequency) noexcept -> uint32_t {
    uint32_t bytes;
    if (frequency < LOW) {  // clang-format off
      bytes = UINT32_C(0x80) | frequency;                // 6 bits
    } else if (frequency < MID) {                        //
      frequency -= LOW;                                  // convert to 0 .. 7FF (11 bits)
      assert(frequency < (UINT32_C(1) << 11));           //
      bytes  = UINT32_C(0xC080);                         //
      bytes |= UINT32_C(0x1F00) & (frequency << 2);      // 5 bits
      bytes |= UINT32_C(0x003F) & (frequency     );      // 6 bits
    } else if (frequency < HIGH) {                       //
      frequency -= MID;                                  // convert to 0 .. 7FFF (15 bits)
      assert(frequency < (UINT32_C(1) << 15));           //
      bytes  = UINT32_C(0xE0C080);                       //
      bytes |= UINT32_C(0x0F0000) & (frequency << 5);    // 4 bits
      bytes |= UINT32_C(0x001F00) & (frequency << 2);    // 5 bits
      bytes |= UINT32_C(0x00003F) & (frequency     );    // 6 bits
    } else {                                             //
      frequency -= HIGH;                                 // convert to 0 .. 3FFFF (18 bits)
      assert(frequency < (UINT32_C(1) << 18));           //
      bytes  = UINT32_C(0xF0E0C080);                     //
      bytes |= UINT32_C(0x07000000) & (frequency << 9);  // 3 bits
      bytes |= UINT32_C(0x000F0000) & (frequency << 5);  // 4 bits
      bytes |= UINT32_C(0x00001F00) & (frequency << 2);  // 5 bits
      bytes |= UINT32_C(0x0000003F) & (frequency     );  // 6 bits
    }  // clang-format on
    return bytes;
  }
  // clang-format off
  static_assert(0x80       == FrequencyToBytes(0      ), "Bit-shift failure in 'FrequencyToBytes'");
  static_assert(0xBF       == FrequencyToBytes(LOW-1  ), "Bit-shift failure in 'FrequencyToBytes'");
  static_assert(0xC080     == FrequencyToBytes(LOW    ), "Bit-shift failure in 'FrequencyToBytes'");
  static_assert(0xDFBF     == FrequencyToBytes(MID-1  ), "Bit-shift failure in 'FrequencyToBytes'");
  static_assert(0xE0C080   == FrequencyToBytes(MID    ), "Bit-shift failure in 'FrequencyToBytes'");
  static_assert(0xEFDFBF   == FrequencyToBytes(HIGH-1 ), "Bit-shift failure in 'FrequencyToBytes'");
  static_assert(0xF0E0C080 == FrequencyToBytes(HIGH   ), "Bit-shift failure in 'FrequencyToBytes'");
  static_assert(0xF7EFDFBF == FrequencyToBytes(LIMIT-1), "Bit-shift failure in 'FrequencyToBytes'");
  // clang-format on

  [[nodiscard]] constexpr auto BytesToFrequency(const uint32_t bytes) noexcept -> uint32_t {
    uint32_t value;
    if (bytes > HGH_SECTION) {  // clang-format off
      value  = (0x07 << 15) & (bytes >> 9);  // 3 bits
      value |= (0x0F << 11) & (bytes >> 5);  // 4 bits
      value |= (0x1F <<  6) & (bytes >> 2);  // 5 bits
      value |= (0x3F      ) & (bytes     );  // 6 bits
      value += HIGH;                         //
    } else if (bytes > MID_SECTION) {        //
      value  = (0x0F << 11) & (bytes >> 5);  // 4 bits
      value |= (0x1F <<  6) & (bytes >> 2);  // 5 bits
      value |= (0x3F      ) & (bytes     );  // 6 bits
      value += MID;                          //
    } else if (bytes > LOW_SECTION) {        //
      value  = (0x1F <<  6) & (bytes >> 2);  // 5 bits
      value |= (0x3F      ) & (bytes     );  // 6 bits
      value += LOW;                          //
    } else {                                 //
      value  = (0x3F      ) & (bytes     );  // 6 bits
    }  // clang-format on
    return value;
  }
  // clang-format off
  static_assert(0       == BytesToFrequency(0x80      ), "Bit-shift failure in 'BytesToFrequency'");
  static_assert(LOW-1   == BytesToFrequency(0xBF      ), "Bit-shift failure in 'BytesToFrequency'");
  static_assert(LOW     == BytesToFrequency(0xC080    ), "Bit-shift failure in 'BytesToFrequency'");
  static_assert(MID-1   == BytesToFrequency(0xDFBF    ), "Bit-shift failure in 'BytesToFrequency'");
  static_assert(MID     == BytesToFrequency(0xE0C080  ), "Bit-shift failure in 'BytesToFrequency'");
  static_assert(HIGH-1  == BytesToFrequency(0xEFDFBF  ), "Bit-shift failure in 'BytesToFrequency'");
  static_assert(HIGH    == BytesToFrequency(0xF0E0C080), "Bit-shift failure in 'BytesToFrequency'");
  static_assert(LIMIT-1 == BytesToFrequency(0xF7EFDFBF), "Bit-shift failure in 'BytesToFrequency'");
  // clang-format on

  [[nodiscard]] constexpr auto ReadUTF(const File_t& stream, int32_t ch) noexcept -> int32_t {
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
    return bytes;
  }
};  // namespace

/**
 * @class GZip_t
 * @brief Handler for static dictionary decoding
 *
 * Handler for static dictionary decoding
 */
class GZip_t final {
public:
  GZip_t() noexcept = default;
  ~GZip_t() noexcept = default;

  GZip_t(const GZip_t&) = delete;
  GZip_t(GZip_t&&) = delete;
  auto operator=(const GZip_t&) -> GZip_t& = delete;
  auto operator=(GZip_t&&) -> GZip_t& = delete;

  [[nodiscard]] auto GetStaticDictionary() noexcept -> std::string {
    _word.clear();
    _word.reserve(3000000);
    static_assert((0x54 == static_words[0]) && (0x9B == static_words[1]), "Wrong data header");
    const int32_t status{gzip::Unzip(static_words.data(), static_cast<uint32_t>(static_words.size()), write_buffer, this)};
    assert(GZip_OK == status);
    (void)status;  // Avoid warning in release mode
    std::replace(_word.begin(), _word.end(), '\n', '\0');
    return _word;
  }

private:
  std::string _word{};

  [[nodiscard]] auto write_buffer(const void* buf, uint32_t cnt) noexcept -> uint32_t {
    _word += std::string{static_cast<const char*>(buf), cnt};
    return cnt;
  }

  static auto write_buffer(const void* buf, uint32_t cnt, void* this_pointer) noexcept -> uint32_t {
    GZip_t* const self{static_cast<GZip_t*>(this_pointer)};
    assert(buf && cnt && self);
    return self->write_buffer(buf, cnt);
  }
};

/**
 * @class Dictionary
 * @brief Handling the dictionary for text preparation
 *
 * Handling the dictionary for text preparation
 */
class Dictionary final : public iMonitor_t {
public:
  Dictionary() noexcept {
    _word.reserve(MAX_WORD_SIZE);
  }
  ~Dictionary() noexcept override;

  Dictionary(const Dictionary&) = delete;
  Dictionary(Dictionary&&) = delete;
  auto operator=(const Dictionary&) -> Dictionary& = delete;
  auto operator=(Dictionary&&) -> Dictionary& = delete;

  void AppendChar(const int32_t ch) noexcept {
    const auto wlength{_word.length()};
    if (is_word_char(ch) && (wlength < MAX_WORD_SIZE)) {
      _word.push_back(static_cast<char>(ch));
    } else {
      if (wlength >= MIN_WORD_SIZE) {
#if 1
        //---------------------------------------------------------------------
        // When using super large files. It could be that the dictionary map
        // is going to use large amounts of memory. Try to reduce this by
        // eliminating the low frequent words.
        //---------------------------------------------------------------------
        if (_word_map.size() > flush_limit) {
          flush_limit = Utilities::safe_add(flush_limit, flush_limit / 2);
          for (auto it = _word_map.begin(); it != _word_map.end();) {
            if (it->second < 2) {
              it = _word_map.erase(it);
            } else {
              ++it;
            }
          }
        }
#endif
        AppendWord();
      }
      _word.clear();
    }
  }

  [[nodiscard]] auto StringToIndex(const std::string_view dictionary) const noexcept -> map_string2uint_t {
    map_string2uint_t map{};
    uint32_t word_index{0};
    size_t start{0};
    size_t end{0};
    for (const auto& ch : dictionary) {
      if (('\n' == ch) || ('\0' == ch)) {
        const std::string word{dictionary.substr(start, end - start)};
        map.emplace(word, word_index++);
        start += word.length() + 1;
        end = start;
      } else {
        ++end;
      }
    }
    return map;
  }

  [[nodiscard]] auto IndexToString(const std::string_view dictionary) const noexcept -> map_uint2string_t {
    map_uint2string_t map{};
    uint32_t word_index{0};
    size_t start{0};
    size_t end{0};
    for (const auto& ch : dictionary) {
      if (('\n' == ch) || ('\0' == ch)) {
        const std::string_view word{dictionary.substr(start, end - start)};
        map.emplace(word_index++, word);
        start += word.length() + 1;
        end = start;
      } else {
        ++end;
      }
    }
    return map;
  }

  void Create(const File_t& in, File_t& out, const std::array<int8_t, 256>& quote, const size_t quoteLength) noexcept {
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

    /**
     * @struct Dictionary_t
     * @brief Storage container for every dictionary element
     *
     * Storage container for every dictionary element
     */
    struct Dictionary_t final {
      explicit Dictionary_t() = delete;
      explicit Dictionary_t(std::string w, uint32_t f) noexcept : word{std::move(w)}, frequency{f} {}
      explicit Dictionary_t(const Dictionary_t& other) noexcept = default;
      Dictionary_t(Dictionary_t&& other) noexcept : word{std::move(other.word)}, frequency{std::exchange(other.frequency, 0)} {}

      [[nodiscard]] auto operator=(const Dictionary_t& other) noexcept -> auto& {
        if (this != &other) {  // Self-assignment detection
          word = other.word;
          frequency = other.frequency;
        }
        return *this;
      }

      [[nodiscard]] auto operator=(Dictionary_t&& other) noexcept -> auto& {
        if (this != &other) {  // Self-assignment detection
          word = std::move(other.word);
          frequency = std::exchange(other.frequency, 0);
        }
        return *this;
      }

      std::string word;
      uint32_t frequency;
      int32_t : 32;  // Padding
    };
    static_assert(std::is_move_assignable<Dictionary_t>::value, "Dictionary_t must have move capabilities");
    static_assert(std::is_nothrow_move_assignable<Dictionary_t>::value, "Dictionary_t must have move capabilities");

    std::vector<Dictionary_t> dictionary{};
    dictionary.reserve(_word_map.size());

    std::for_each(_word_map.begin(), _word_map.end(), [&dictionary](const auto& entry) noexcept {
      if (const auto frequency{entry.second + 1}; frequency >= MIN_WORD_FREQ) {  // The first element has count 0 (!)
        dictionary.emplace_back(Dictionary_t(entry.first, frequency));
      }
    });

    // Sort all by frequency and length
    std::stable_sort(dictionary.begin(), dictionary.end(), [](const auto& a, const auto& b) noexcept -> bool {
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
      File_t txt{"dictionary_raw.txt", "wb+"};
      for (size_t n{0}, bytes{1}; n < _dic_length; ++n) {
        if ((LOW == n) || (MID == n) || (HIGH == n)) {
          ++bytes;
        }
        if (LIMIT == n) {
          bytes = 0;
        }
        fprintf(txt, "%" PRIu64 ", %7" PRIu32 " %s\n", bytes, dictionary[n].frequency, dictionary[n].word.data());
        // fprintf(txt, "%s\n", dictionary[n].word.data());
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

    for (auto& item : _word_map) {  // Set all frequencies to illegal value
      item.second = UNUSED;
    }
    for (uint32_t n{0}; n < _dic_length; ++n) {
      _word_map[dictionary[n].word] = FrequencyToBytes(n);
    }

    out.putVLI(_dic_length);  // write length of dictionary
    if (_dic_length > 0) {
      _dic_start = out.Position();
#if 1
      _static_dictionay = GZip_t().GetStaticDictionary();
      const auto static_dictionay_map{StringToIndex(_static_dictionay)};

      bool in_sync{false};
      for (uint32_t n{0}, m{0}, delta{0}; n < _dic_length; ++n) {
        const std::string& word{dictionary[n].word};

        if (const auto& it{static_dictionay_map.find(word)}; it != static_dictionay_map.end()) {  // Found?
          if (n == it->second) {
            in_sync = false;
            m = n;
          } else {
            WriteBytes(out, FrequencyToBytes(n));
            auto frequency{static_cast<int32_t>(it->second - delta)};
            if (frequency < 0) {
              out.putc(TP5_NEGATIVE_CHAR);
              frequency = -frequency;
            }
            WriteBytes(out, FrequencyToBytes(static_cast<uint32_t>(frequency)));
            delta = it->second;
            in_sync = true;
            m = n + 1;
          }
        } else {
          in_sync = true;
          if (n != m) {
            WriteBytes(out, FrequencyToBytes(n));
            auto frequency{static_cast<int32_t>(m - delta)};
            if (frequency < 0) {
              out.putc(TP5_NEGATIVE_CHAR);
              frequency = -frequency;
            }
            WriteBytes(out, FrequencyToBytes(static_cast<uint32_t>(frequency)));
            delta = m;
            m = n;
          }
          ++m;
          WriteLiteral(out, word);
        }
      }

      if (!in_sync) {
        WriteBytes(out, FrequencyToBytes(_dic_length - 1));
        WriteBytes(out, FrequencyToBytes(_dic_length - 1));
      }
#else
      for (uint32_t n{0}; n < _dic_length; ++n) {
        const std::string& word{dictionary[n].word};
        WriteLiteral(out, word);
      }
#endif
      _dic_end = out.Position();
    }

#if defined(DEBUG_WRITE_DICTIONARY)
    {
      File_t txt{"dictionary_final.txt", "wb+"};
      for (uint32_t n{0}, bytes{1}; n < _dic_length; ++n) {
        if ((LOW == n) || (MID == n) || (HIGH == n)) {
          ++bytes;
        }
        if (LIMIT == n) {
          bytes = 0;
        }
        fprintf(txt, "%" PRIu32 ", %7" PRIu32 " %s\n", bytes, dictionary[n].frequency, dictionary[n].word.c_str());
        // fprintf(txt, "%s\n", dictionary[n].word.data());
      }
    }
#endif
  }

  void Read(const File_t& stream) noexcept {
    _dic_length = static_cast<uint32_t>(stream.getVLI());
    assert(_dic_length <= LIMIT);

    _byte_map.reserve(_dic_length);

    _static_dictionay = GZip_t().GetStaticDictionary();
    auto static_dictionay{IndexToString(_static_dictionay)};

    bool sign{false};
    int32_t delta{0};
    std::string word{};
    for (uint32_t n{0}; n < _dic_length; ++n) {
      int32_t ch{stream.getc()};
      if (TP5_NEGATIVE_CHAR == ch) {
        sign = true;
        continue;
      }
      if (0x80 & ch) {
        const auto origin{stream.Position()};
        const auto sync_index{ReadValue(stream, ch)};  // n
        if ((sync_index < n) || (sync_index > _dic_length)) {
          stream.Seek(origin);
          const auto bytes{FrequencyToBytes(n)};
          const auto new_word{ReadLiteral(stream, ch)};
          _byte_map.emplace(bytes, new_word);
        } else {
          ch = stream.getc();
          if (TP5_NEGATIVE_CHAR == ch) {
            sign = true;
            ch = stream.getc();
          }
          auto freqency{static_cast<int32_t>(ReadValue(stream, ch))};  // --> word
          if (sign) {
            sign = false;
            freqency = -freqency;
          }
          const auto word_index{(std::min)(static_cast<uint32_t>(freqency + delta), LIMIT - 1)};
          delta = static_cast<int32_t>(word_index);

          if (((_dic_length - 1) == sync_index) && (sync_index == word_index)) {
            for (; n < _dic_length; ++n) {
              const auto bytes{FrequencyToBytes(n)};
              const auto new_word{static_dictionay[n]};
              _byte_map.emplace(bytes, new_word);
            }
            continue;
          }

          while (n < sync_index) {
            const auto bytes{FrequencyToBytes(n)};
            word = static_dictionay[n];
            _byte_map[bytes] = word;
            ++n;
          }

          if (static_dictionay[word_index].compare(word)) {
            const auto bytes{FrequencyToBytes(n)};  // index == sync_index
            const auto new_word{static_dictionay[word_index]};
            _byte_map.emplace(bytes, new_word);
          } else {
            --n;
          }
          word.clear();
        }
      } else {
        const auto bytes{FrequencyToBytes(n)};
        const auto new_word{ReadLiteral(stream, ch)};
        _byte_map.emplace(bytes, new_word);
      }
    }

#if defined(DEBUG_WRITE_DICTIONARY)
    {
      File_t txt{"dictionary_decode.txt", "wb+"};
      for (uint32_t n{0}; n < _dic_length; ++n) {
        const auto bytes{FrequencyToBytes(n)};
        auto w = _byte_map[bytes];
        fprintf(txt, "%s\n", w.c_str());
      }
    }
#endif
  }

  [[nodiscard]] auto word2frequency(const std::string& word) noexcept -> std::pair<bool, uint32_t> {
    if (const auto it{_word_map.find(word)}; it != _word_map.end()) {
      if (UNUSED != it->second) {
        return {true, it->second};  // Is found, frequency
      }
    }
    return {false, 0};  // Not found...
  }

  [[nodiscard]] auto bytes2word(const uint32_t bytes) const noexcept -> const std::string_view {
    const auto found{_byte_map.find(bytes)};
    assert(found != _byte_map.end());
    return found->second;
  }

  [[nodiscard]] auto GetDicStartOffset() const noexcept -> int64_t {
    return _dic_start;
  }

  [[nodiscard]] auto GetDicEndOffset() const noexcept -> int64_t {
    return _dic_end;
  }

  [[nodiscard]] auto GetDicWords() const noexcept -> int64_t {
    return _dic_length;
  }

private:
  static constexpr auto BLOCK_SIZE{UINT32_C(1) << 16};

  void WriteBytes(const File_t& stream, const uint32_t bytes) const noexcept {
    if (bytes > HGH_SECTION) {
      stream.putc(static_cast<uint8_t>(bytes >> 24));
      stream.putc(static_cast<uint8_t>(bytes >> 16));
      stream.putc(static_cast<uint8_t>(bytes >> 8));
      stream.putc(static_cast<uint8_t>(bytes));
    } else if (bytes > MID_SECTION) {
      stream.putc(static_cast<uint8_t>(bytes >> 16));
      stream.putc(static_cast<uint8_t>(bytes >> 8));
      stream.putc(static_cast<uint8_t>(bytes));
    } else if (bytes > LOW_SECTION) {
      stream.putc(static_cast<uint8_t>(bytes >> 8));
      stream.putc(static_cast<uint8_t>(bytes));
    } else {
      stream.putc(static_cast<uint8_t>(bytes));
    }
  }

  [[nodiscard]] auto ReadValue(const File_t& stream, int32_t ch) const noexcept -> uint32_t {
    const auto bytes{ReadUTF(stream, ch)};
    return BytesToFrequency(static_cast<uint32_t>(bytes));
  }

  [[nodiscard]] auto ReadLiteral(const File_t& stream, int32_t ch) const noexcept -> std::string {
#if 1
    std::string word{};
    if (TP5_ESCAPE_CHAR != ch) {
      word.push_back(static_cast<char>(ch));
    }
    while (TP5_SEPARATE_CHAR != (ch = stream.getc())) {
      assert(EOF != ch);
      if (TP5_ESCAPE_CHAR == ch) {
        continue;
      }
      word.push_back(static_cast<char>(ch));
    }
    return word;
#else
    enum state_t { NO_STATE = 0, IGNORE_CHR, END_OF_WORD };
    state_t state{NO_STATE};
    std::string word{};
    if (TP5_ESCAPE_CHAR == ch) {
      state = IGNORE_CHR;
    } else {
      word.push_back(static_cast<char>(ch));
    }
    while (END_OF_WORD != state) {
      ch = stream.getc();
      assert(EOF != ch);
      if (TP5_ESCAPE_CHAR == ch) {
        state = IGNORE_CHR;
        ch = stream.getc();
        assert(EOF != ch);
      }
      if (IGNORE_CHR == state) {
        state = NO_STATE;
      } else {
        if ((0x80 | TP5_ESCAPE_CHAR) == ch) {
          state = END_OF_WORD;
          ch = stream.getc();
          assert(EOF != ch);
        } else if (0x80 & ch) {
          state = END_OF_WORD;
          ch &= 0x7F;
        }
      }
      word.push_back(static_cast<char>(ch));
    }
    return word;
#endif
  }

  void WriteLiteral(const File_t& stream, const std::string_view literal) const noexcept {
#if 1
    assert(literal.length() > 0);
    for (const auto& ch : literal) {
      if (0x80 & ch) {
        stream.putc(TP5_ESCAPE_CHAR);
      }
      stream.putc(ch);
    }
    stream.putc(TP5_SEPARATE_CHAR);
#else
    auto length{literal.length()};
    for (int32_t ch : literal) {
      --length;
      if (0x80 & ch) {
        if (0 == length) {
          stream.putc(0x80 | TP5_ESCAPE_CHAR);
        } else {
          stream.putc(TP5_ESCAPE_CHAR);
        }
      }
      if (0 == length) {
        ch |= 0x80;
      }
      stream.putc(ch);
    }
#endif
  }

  void AppendWord() noexcept {
    if (auto it{_word_map.find(_word)}; it != _word_map.end()) {
      it->second += 1;  // Increase frequency
    } else {
      _word_map.emplace(_word, 0);  // Start with frequency is zero
    }
  }

  [[nodiscard]] auto InputLength() const noexcept -> int64_t final {
    return _input_length;
  }
  [[nodiscard]] auto OutputLength() const noexcept -> int64_t final {
    return _input_length;
  }
  [[nodiscard]] auto WorkLength() const noexcept -> int64_t final {
    return _original_length;
  }
  [[nodiscard]] auto LayoutLength() const noexcept -> int64_t final {
    return _original_length;
  }

  map_string2uint_t _word_map{};
  map_uint2string_t _byte_map{};
  std::string _static_dictionay{};
  int64_t _original_length{0};
  int64_t _input_length{0};
  int64_t _dic_start{0};
  int64_t _dic_end{0};
  size_t flush_limit{1500000};
  uint32_t _dic_length{0};
  int32_t : 32;  // Padding
  std::string _word{};
};
Dictionary::~Dictionary() noexcept = default;

/**
 * @class TxtPrep
 * @brief Handling the text preparation
 *
 * Handling the text preparation
 */
class TxtPrep final : public iMonitor_t {
public:
  explicit TxtPrep(File_t& in, File_t& out, const std::array<int64_t, 256>* const charFreq, const std::string_view quote) noexcept
      : _in{in},  //
        _out{out},
        _quote_length{quote.length()} {
    assert(_quote_length < 256);            // Must fit in a byte of 8 bit
    assert(_quote_length < _quote.size());  // Should fit
    memcpy(_quote.data(), quote.data(), _quote_length);
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
        _quote_length = 0;
        _quote.fill(0);
      }
    }
    _word.reserve(MAX_WORD_SIZE);
    _value.reserve(MAX_WORD_SIZE);
  }
  ~TxtPrep() noexcept override;

  TxtPrep() = delete;
  TxtPrep(const TxtPrep&) = delete;
  TxtPrep(TxtPrep&&) = delete;
  auto operator=(const TxtPrep&) -> TxtPrep& = delete;
  auto operator=(TxtPrep&&) -> TxtPrep& = delete;

  [[nodiscard]] auto Encode() noexcept -> std::tuple<int64_t, int64_t, int64_t, int64_t> {
    _original_length = _in.Size();
    _out.putVLI(_original_length);
    Putc(static_cast<uint8_t>(_quote_length));
    for (size_t n{0}; n < _quote_length; ++n) {
      Putc(_quote[n]);
    }

    _dictionary.Create(_in, _out, _quote, _quote_length);

    const auto data_pos{_out.Position()};

    _in.Rewind();

    const Progress_t progress("TXT", true, *this);

    uint32_t qoffset{0};

    int32_t ch;
    while (EOF != (ch = _in.getc())) {
      if (_quote_length > 0) {
        if (ch == _quote[qoffset]) {
          ++qoffset;
          if (qoffset == _quote_length) {
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

    _out.Sync();

    return {data_pos + int64_t(sizeof(int64_t)),  //
            _dictionary.GetDicStartOffset(),      //
            _dictionary.GetDicEndOffset(),        //
            _dictionary.GetDicWords()};
  }

  [[nodiscard]] auto Decode() noexcept -> int64_t {
    _original_length = _in.getVLI();
    assert(_original_length > 0);
    _quote_length = static_cast<size_t>(_in.getc());
    for (size_t n{_quote_length}; n--;) {  // Reverse read
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
          uint128_t value{};
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
        for (size_t n{_quote_length}; n--;) {  // Reverse write
          Putc(_quote[n]);
        }
        continue;
      } else if (0x80 & ch) {
        const auto bytes{ReadUTF(_in, ch)};
        const std::string_view word{_dictionary.bytes2word(static_cast<uint32_t>(bytes))};
        assert(word.length() > 0);
        for (const auto& key : word) {
          Putc(key);
        }
      } else {
        Putc(ch);
      }
    }

    _out.Sync();

    return _output_length;
  }

private:
  ALWAYS_INLINE void Putc(int32_t ch) noexcept {
    ++_output_length;
    _out.putc(ch);
  }

  void WriteBytes(const uint32_t bytes) noexcept {
    if (bytes > HGH_SECTION) {
      Putc(static_cast<uint8_t>(bytes >> 24));
      Putc(static_cast<uint8_t>(bytes >> 16));
      Putc(static_cast<uint8_t>(bytes >> 8));
      Putc(static_cast<uint8_t>(bytes));
    } else if (bytes > MID_SECTION) {
      Putc(static_cast<uint8_t>(bytes >> 16));
      Putc(static_cast<uint8_t>(bytes >> 8));
      Putc(static_cast<uint8_t>(bytes));
    } else if (bytes > LOW_SECTION) {
      Putc(static_cast<uint8_t>(bytes >> 8));
      Putc(static_cast<uint8_t>(bytes));
    } else {
      Putc(static_cast<uint8_t>(bytes));
    }
  }

  void Literal(const std::string_view literal) noexcept {
    auto length{literal.length()};
    if (length > 0) {
      const auto* __restrict src{literal.data()};
      while (length-- > 0) {
        const int32_t ch{*src++};
        if (0x80 & ch) {
          Putc(TP5_ESCAPE_CHAR);
        }
        Putc(ch);
      }
    }
  }

  void TryFindShorterSolution(const std::string& word) noexcept {
    const auto wlength{word.length()};
    if (wlength >= MIN_SHORTER_WORD_SIZE) {
      {
        const auto [found, frequency]{_dictionary.word2frequency(word)};
        if (found) {
          WriteBytes(frequency);
          return;
        }
      }

      // Try to find a shorter word, start shortening at the end of the word
      size_t offset_end{0};
      size_t frequency_end{0};
      for (size_t offset{wlength - 1}; offset >= MIN_SHORTER_WORD_SIZE; --offset) {
        const std::string shorter{word.substr(0, offset)};
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
        const std::string shorter{word.substr(offset, wlength - offset)};
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
        const auto left_over{word.substr(0, offset_begin)};
        TryFindShorterSolution(left_over);
        WriteBytes(static_cast<uint32_t>(frequency_begin));
        return;
      }
      if (write_end_word) {
        WriteBytes(static_cast<uint32_t>(frequency_end));
        const auto left_over{word.substr(offset_end, wlength - offset_end)};
        TryFindShorterSolution(left_over);
        return;
      }

      // Try to find a shorter word, start shortening at the beginning and limiting the length of the word
      for (size_t offset{1}; offset < (wlength - 1); ++offset) {
        for (size_t length{wlength - offset}; length >= MIN_SHORTER_WORD_SIZE; --length) {
          const std::string shorter(word, offset, length);
          const auto [found, frequency]{_dictionary.word2frequency(shorter)};
          if (found && (frequency < HIGH)) {
            Literal(word.substr(0, offset));
            WriteBytes(frequency);
            Literal(word.substr(offset + length, wlength - offset - length));
            return;
          }
        }
      }
    }

    // Not found ...
    Literal(word);
  }

  void EncodeWord(const std::string& word) noexcept {
    const auto wlength{word.length()};
    if (wlength >= MIN_WORD_SIZE) {
      if (const auto [found, frequency]{_dictionary.word2frequency(word)}; found) {
        WriteBytes(frequency);
        return;
      }
    }
    TryFindShorterSolution(word);
  }

  void EncodeWordValue() noexcept {
    EncodeWord(_word);
    _word.clear();

    const auto vlength{_value.length()};
    if (vlength > 0) {
      if ((vlength <= MIN_NUMBER_SIZE) || !EncodeValue()) {
        Literal(_value);
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
#if defined(_MSC_VER)
      const uint128_t P10_UINT64{UINT64_C(10000000000000000000)};  // 19 zeroes
#else
      //                         ULLONG_MAX 18446744073709551615ULL
      static constexpr uint128_t P10_UINT64{10000000000000000000_xxl};  // 19 zeroes
#endif
      const uint64_t leading{static_cast<uint64_t>(value / P10_UINT64)};
      const uint64_t trailing{static_cast<uint64_t>(value % P10_UINT64)};
      snprintf(str.data(), str.size(), "%" PRIu64 "%.19" PRIu64, leading, trailing);
    } else {
      snprintf(str.data(), str.size(), "%" PRIu64, static_cast<uint64_t>(value));
    }
    return str;
  }

  [[nodiscard]] auto strtoxxl(const std::string_view text) const noexcept -> uint128_t {
#if 0
    uint128_t value{};
    for (const auto& ch : text) {
      value *= 10;
      value += static_cast<uint64_t>(ch - '0');
    }
#else
    const size_t size{text.size()};
    assert(size <= 40);
    std::array<uint8_t, 50> str{};
    for (size_t i{0}; i < size; ++i) {
      assert((text[i] >= '0') && (text[i] <= '9'));
      str[i] = static_cast<uint8_t>(text[i] - '0');
    }
    uint64_t lsb{0};
    uint64_t msb{0};
    for (size_t start{0}, bit{0}; (start < size) && (bit < 128); ++bit) {
      uint32_t carry{0};
      for (size_t i{start}; i < size; i++) {
        const auto a{str[i] + carry * 10};
        carry = a % 2;
        str[i] = uint8_t(a / 2);
      }
      if (bit < 64) {
        lsb |= static_cast<uint64_t>(carry) << bit;
      } else {
        msb |= static_cast<uint64_t>(carry) << (bit - 64);
      }
      while (str[start] == 0 && (start < size)) {
        start++;
      }
    }
    const uint128_t value{(uint128_t(msb) << 64) | lsb};
#endif
    return value;
  }

  [[nodiscard]] auto EncodeValue() noexcept -> bool {
    uint128_t value{strtoxxl(_value)};
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

    if (is_word_char(ch) && (_word.length() < MAX_WORD_SIZE)) {
      _word.push_back(static_cast<char>(ch));
    } else {
      EncodeWordValue();

      if ((0x80 & ch) || (TP5_ESCAPE_CHAR == ch) || (TP5_QUOTING_CHAR == ch)) {
        Putc(TP5_ESCAPE_CHAR);
      }
      Putc(ch);
    }
  }

  [[nodiscard]] auto InputLength() const noexcept -> int64_t final {
    return _in.Position();
  }
  [[nodiscard]] auto OutputLength() const noexcept -> int64_t final {
    return _out.Position();
  }
  [[nodiscard]] auto WorkLength() const noexcept -> int64_t final {
    return _original_length;
  }
  [[nodiscard]] auto LayoutLength() const noexcept -> int64_t final {
    return _original_length;
  }

  Dictionary _dictionary{};
  File_t& _in;
  File_t& _out;
  int64_t _original_length{0};
  int64_t _output_length{0};
  std::array<int8_t, 256> _quote{};
  size_t _quote_length{0};
  std::string _word{};
  std::string _value{};
};
TxtPrep::~TxtPrep() noexcept = default;

auto EncodeText(File_t& in, File_t& out) noexcept -> std::tuple<int64_t, int64_t, int64_t, int64_t> {
  int64_t data_pos{0};
  int64_t dic_start_offset{0};
  int64_t dic_end_offset{0};
  int64_t dic_words{0};

  if (File_t tmp{} /*{"_tmp_.cse", "wb+"}*/; tmp.isOpen()) {
    std::array<int64_t, 256> charFreq{};
    std::string quote{};

    {
      CaseSpace_t cse(in, tmp);
      cse.Encode();
      charFreq = cse.CharFrequency();
      quote = cse.GetQuote();
    }

    tmp.Rewind();

#if defined(DEBUG_WRITE_ANALYSIS_BMP)
    Analysis_t analysis(charFreq);
    analysis.Write();
#endif

    {
      TxtPrep txtprep(tmp, out, &charFreq, quote);
      const auto [data_pos_, dic_start_, dic_end_, dic_words_]{txtprep.Encode()};
      data_pos = data_pos_;
      dic_start_offset = dic_start_;
      dic_end_offset = dic_end_;
      dic_words = dic_words_;
    }
  }

  return {data_pos, dic_start_offset, dic_end_offset, dic_words};
}

auto DecodeText(File_t& in, File_t& out) noexcept -> int64_t {
  int64_t length{0};
  if (File_t tmp{} /*{"_tmp_.cse", "wb+"}*/; tmp.isOpen()) {
    {
      TxtPrep txtprep(in, tmp, nullptr, "");
      (void)txtprep.Decode();
    }

    tmp.Rewind();

    {
      CaseSpace_t cse(tmp, out);
      length = cse.Decode();
    }
  }

  return length;
}
