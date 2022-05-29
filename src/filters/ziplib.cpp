/* Copyright (c) 2019-2022 Marwijn Hessel
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
#include "ziplib.h"
#include <zconf.h>
#include <zlib.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include "File.h"
#include "filter.h"
#include "iEncoder.h"

static auto parse_zlib_header(const int32_t header) noexcept -> int32_t {
  // clang-format off
  switch (header) {
    case 0x2815: return 0;
    case 0x2853: return 1;
    case 0x2891: return 2;
    case 0x28CF: return 3;
    case 0x3811: return 4;
    case 0x384F: return 5;
    case 0x388D: return 6;
    case 0x38CB: return 7;
    case 0x480D: return 8;
    case 0x484B: return 9;
    case 0x4889: return 10;
    case 0x48C7: return 11;
    case 0x5809: return 12;
    case 0x5847: return 13;
    case 0x5885: return 14;
    case 0x58C3: return 15;
    case 0x6805: return 16;
    case 0x6843: return 17;
    case 0x6881: return 18;
    case 0x68DE: return 19;
    case 0x7801: return 20;
    case 0x785E: return 21;
    case 0x789C: return 22;
    case 0x78DA: return 23;
    default:     return -1;
  }
  // clang-format on
}

static auto zlib_inflateInit(z_streamp strm, const int32_t zh) noexcept -> int32_t {
  if (-1 == zh) {
    return inflateInit2(strm, -MAX_WBITS);
  }
  return inflateInit(strm);
}

static constexpr uint32_t BLOCK_SIZE{1u << 16};
static constexpr uint32_t LIMIT{128};
static constexpr uint32_t ZLIB_NUM_COMBINATIONS{81};

class zLibMTF final {
public:
  explicit zLibMTF() {
    for (uint32_t i{0}; i < ZLIB_NUM_COMBINATIONS; i++) {
      _previous[i] = static_cast<int32_t>(i - 1);
      _next[i] = static_cast<int32_t>(i + 1);
    }
    _next[ZLIB_NUM_COMBINATIONS - 1] = -1;
  }
  virtual ~zLibMTF() noexcept;
  zLibMTF(const zLibMTF&) = delete;
  zLibMTF(zLibMTF&&) = delete;
  auto operator=(const zLibMTF&) -> zLibMTF& = delete;
  auto operator=(zLibMTF&&) -> zLibMTF& = delete;

  auto GetFirst() noexcept -> int32_t {
    return _index = _root;
  }

  auto GetNext() noexcept -> int32_t {
    if (_index >= 0) {
      _index = _next[static_cast<uint32_t>(_index)];
      return _index;
    }
    return _index;  //-1
  }

  void Insert(int32_t i) noexcept {
    if ((_index = i) == _root) {
      return;
    }
    int32_t p = _previous[static_cast<uint32_t>(_index)];
    int32_t n = _next[static_cast<uint32_t>(_index)];
    if (p >= 0) {
      _next[static_cast<uint32_t>(p)] = _next[static_cast<uint32_t>(_index)];
    }
    if (n >= 0) {
      _previous[static_cast<uint32_t>(n)] = _previous[static_cast<uint32_t>(_index)];
    }
    _previous[static_cast<uint32_t>(_root)] = _index;
    _next[static_cast<uint32_t>(_index)] = _root;
    _root = _index;
    _previous[static_cast<uint32_t>(_root)] = -1;
  }

private:
  int32_t _root{0};
  int32_t _index{0};
  std::array<int32_t, ZLIB_NUM_COMBINATIONS> _previous{};
  std::array<int32_t, ZLIB_NUM_COMBINATIONS> _next{};
};

zLibMTF::~zLibMTF() noexcept = default;

static auto decode_zlib(File_t& in, File_t& out, int64_t& len) noexcept -> bool {
  std::array<uint8_t, BLOCK_SIZE * 2> zin;
  std::array<uint8_t, BLOCK_SIZE> zout;
  std::array<uint8_t, BLOCK_SIZE * 2> zrec;
  std::array<uint8_t, ZLIB_NUM_COMBINATIONS * LIMIT> diffByte{};
  std::array<int32_t, ZLIB_NUM_COMBINATIONS * LIMIT> diffPos{};

  //---------------------------------------------------------------------------
  // Step 1 - parse offset type form zlib stream header
  //---------------------------------------------------------------------------
  const int64_t safe_pos{in.Position()};
  const int32_t h1{in.getc()};
  const int32_t h2{in.getc()};
  in.Seek(safe_pos);
  const int32_t zh{parse_zlib_header((h1 * 256) | h2)};
  int32_t window{(-1 == zh) ? 0 : MAX_WBITS + 10 + zh / 4};
  int32_t ctype{zh % 4};
  const uint32_t minclevel{(0 == window) ? 1u : (3 == ctype) ? 7u : (2 == ctype) ? 6u : (1 == ctype) ? 2u : 1u};
  const uint32_t maxclevel{(0 == window) ? 9u : (3 == ctype) ? 9u : (2 == ctype) ? 6u : (1 == ctype) ? 5u : 1u};
  int32_t index{-1};
  int32_t nTrials{0};
  bool found{false};

  //---------------------------------------------------------------------------
  // Step 2 - check recompressiblitiy, determine parameters and save differences
  //---------------------------------------------------------------------------
  std::array<z_stream, ZLIB_NUM_COMBINATIONS> rec_strm;
  std::array<uint32_t, ZLIB_NUM_COMBINATIONS> diffCount;
  std::array<uint32_t, ZLIB_NUM_COMBINATIONS> recpos;
  z_stream main_stream;
  memset(&main_stream, 0, sizeof(main_stream));
  if (Z_OK != zlib_inflateInit(&main_stream, zh)) {
    return false;
  }

  for (uint32_t i{0}; i < ZLIB_NUM_COMBINATIONS; i++) {
    uint32_t clevel{(i / 9) + 1};
    // Early skip if invalid parameter
    if ((clevel < minclevel) || (clevel > maxclevel)) {
      diffCount[i] = LIMIT;
      continue;
    }
    const uint32_t memlevel{(i % 9) + 1};
    memset(&rec_strm[i], 0, sizeof(rec_strm[i]));
    int32_t ret = deflateInit2(&rec_strm[i], static_cast<int32_t>(clevel), Z_DEFLATED, window - MAX_WBITS, static_cast<int32_t>(memlevel), Z_DEFAULT_STRATEGY);
    diffCount[i] = (Z_OK == ret) ? 0 : LIMIT;
    recpos[i] = BLOCK_SIZE * 2;
    diffPos[i * LIMIT] = -1;
    diffByte[i * LIMIT] = 0;
  }

  int32_t main_ret{Z_STREAM_END};
  zLibMTF mtf{};
  for (int64_t i{0}; i < len; i += BLOCK_SIZE) {
    const uint32_t blsize{(std::min)(static_cast<uint32_t>(len - i), BLOCK_SIZE)};
    nTrials = 0;
    for (uint32_t j{0}; j < ZLIB_NUM_COMBINATIONS; j++) {
      if (diffCount[j] == LIMIT) {
        continue;
      }
      nTrials++;
      if (recpos[j] >= BLOCK_SIZE) {
        recpos[j] -= BLOCK_SIZE;
      }
    }
    // early break if nothing left to test
    if (0 == nTrials) {
      break;
    }

    memmove(zrec.data(), &zrec[BLOCK_SIZE], BLOCK_SIZE);
    memmove(zin.data(), &zin[BLOCK_SIZE], BLOCK_SIZE);
    in.Read(&zin[BLOCK_SIZE], blsize);  // Read block from input file

    // Decompress/inflate block
    main_stream.next_in = &zin[BLOCK_SIZE];
    main_stream.avail_in = blsize;
    do {
      main_stream.next_out = zout.data();
      main_stream.avail_out = BLOCK_SIZE;
      main_ret = inflate(&main_stream, Z_FINISH);
      if (Z_STREAM_END == main_ret) {
        len = static_cast<int64_t>(main_stream.total_in);  // True for PKZip, but not for GZ length of file is not known
      }

      nTrials = 0;
      // Recompress/deflate block with all possible parameters
      for (int32_t j{mtf.GetFirst()}; j >= 0; j = mtf.GetNext()) {
        if (LIMIT == diffCount[static_cast<uint32_t>(j)]) {
          continue;
        }
        nTrials++;
        rec_strm[static_cast<uint32_t>(j)].next_in = zout.data();
        rec_strm[static_cast<uint32_t>(j)].avail_in = BLOCK_SIZE - main_stream.avail_out;
        rec_strm[static_cast<uint32_t>(j)].next_out = &zrec[recpos[static_cast<uint32_t>(j)]];
        rec_strm[static_cast<uint32_t>(j)].avail_out = (BLOCK_SIZE * 2) - recpos[static_cast<uint32_t>(j)];
        const int32_t flush{(len >= static_cast<int64_t>(main_stream.total_in)) ? Z_FINISH : Z_NO_FLUSH};
        const int32_t ret{deflate(&rec_strm[static_cast<uint32_t>(j)], flush)};
        if ((Z_BUF_ERROR != ret) && (Z_STREAM_END != ret) && (Z_OK != ret)) {
          diffCount[static_cast<uint32_t>(j)] = LIMIT;
          continue;
        }

        // Compare
        const uint32_t end{(2 * BLOCK_SIZE) - rec_strm[static_cast<uint32_t>(j)].avail_out};
        const uint32_t tail{(std::max)((Z_STREAM_END == main_ret) ? static_cast<uint32_t>(len - static_cast<int64_t>(rec_strm[static_cast<uint32_t>(j)].total_out)) : 0u, 0u)};
        for (uint32_t k{recpos[static_cast<uint32_t>(j)]}; k < (end + tail); k++) {
          if (((k < end) && ((i + k - BLOCK_SIZE) < len) && (zrec[k] != zin[k])) || (k >= end)) {
            if (++diffCount[static_cast<uint32_t>(j)] < LIMIT) {
              const uint32_t p{static_cast<uint32_t>(j) * LIMIT + diffCount[static_cast<uint32_t>(j)]};
              diffPos[p] = static_cast<int32_t>(i + k - BLOCK_SIZE);
              assert(k < zin.size());
              diffByte[p] = zin[k];
            }
          }
        }

        // Early break on perfect match
        if ((Z_STREAM_END == main_ret) && (0 == diffCount[static_cast<uint32_t>(j)])) {
          index = j;
          found = true;
          break;
        }
        recpos[static_cast<uint32_t>(j)] = 2 * BLOCK_SIZE - rec_strm[static_cast<uint32_t>(j)].avail_out;
      }
    } while ((0 == main_stream.avail_out) && (Z_BUF_ERROR == main_ret) && (nTrials > 0));

    if (((Z_BUF_ERROR != main_ret) && (Z_STREAM_END != main_ret)) || (0 == nTrials)) {
      break;
    }
  }
  uint32_t minCount{found ? 0 : LIMIT};
  for (int32_t i{ZLIB_NUM_COMBINATIONS - 1}; i >= 0; i--) {
    const uint32_t clevel{(static_cast<uint32_t>(i) / 9) + 1};
    if ((clevel >= minclevel) && (clevel <= maxclevel)) {
      deflateEnd(&rec_strm[static_cast<uint32_t>(i)]);
    }
    if (!found && (diffCount[static_cast<uint32_t>(i)] < minCount)) {
      index = i;
      minCount = diffCount[static_cast<uint32_t>(i)];
    }
  }
  inflateEnd(&main_stream);
  if (LIMIT == minCount) {
    return false;
  }
  assert(-1 != index);
  mtf.Insert(index);

  //---------------------------------------------------------------------------
  // Step 3 - write parameters, differences and precompressed (inflated) data
  //---------------------------------------------------------------------------
  out.putc(static_cast<int32_t>(diffCount[static_cast<uint32_t>(index)]));
  out.putc(window);
  out.putc(index);
  for (uint32_t i{0}; i <= diffCount[static_cast<uint32_t>(index)]; i++) {
    const auto v{(i == diffCount[static_cast<uint32_t>(index)]) ? static_cast<uint32_t>(len - diffPos[static_cast<uint32_t>(index) * LIMIT + i]) :  //
                     static_cast<uint32_t>(diffPos[static_cast<uint32_t>(index) * LIMIT + i + 1] - diffPos[static_cast<uint32_t>(index) * LIMIT + i] - 1)};
    out.put32(v);
  }
  for (uint32_t i{0}; i < diffCount[static_cast<uint32_t>(index)]; i++) {
    out.putc(diffByte[static_cast<uint32_t>(index) * LIMIT + i + 1]);
  }

  in.Seek(safe_pos);
  memset(&main_stream, 0, sizeof(main_stream));
  if (zlib_inflateInit(&main_stream, zh) != Z_OK) {
    return false;
  }
  for (uint32_t i{0}; i < len; i += BLOCK_SIZE) {
    const uint32_t blsize{(std::min)(static_cast<uint32_t>(len - i), BLOCK_SIZE)};
    in.Read(zin.data(), blsize);
    main_stream.next_in = zin.data();
    main_stream.avail_in = blsize;
    do {
      main_stream.next_out = zout.data();
      main_stream.avail_out = BLOCK_SIZE;
      main_ret = inflate(&main_stream, Z_FINISH);
      out.Write(zout.data(), BLOCK_SIZE - main_stream.avail_out);
    } while ((0 == main_stream.avail_out) && (Z_BUF_ERROR == main_ret));
    if ((Z_BUF_ERROR != main_ret) && (Z_STREAM_END != main_ret)) {
      break;
    }
  }
  inflateEnd(&main_stream);
  return Z_STREAM_END == main_ret;
}

auto encode_zlib(File_t& in, int64_t size, File_t& out, const bool compare) noexcept -> bool {
  uint64_t diffFound{0};
  std::array<uint8_t, BLOCK_SIZE> zin;
  std::array<uint8_t, BLOCK_SIZE> zout;
  const uint32_t diffCount = (std::min)(static_cast<uint32_t>(in.getc()), LIMIT - 1);
  uint32_t window{static_cast<uint32_t>(in.getc() - MAX_WBITS)};
  const int32_t index{in.getc()};
  const int32_t memlevel{(index % 9) + 1};
  const int32_t clevel{(index / 9) + 1};
  uint32_t len{0};

  std::array<int32_t, LIMIT> diffPos;
  diffPos[0] = -1;
  for (uint32_t i{0}; i <= diffCount; i++) {
    auto v{static_cast<int32_t>(in.get32())};
    if (i == diffCount) {
      len = static_cast<uint32_t>(v + diffPos[i]);
    } else {
      diffPos[i + 1] = v + diffPos[i] + 1;
    }
  }
  std::array<uint8_t, LIMIT> diffByte;
  diffByte[0] = 0;
  for (uint32_t i{0}; i < diffCount; i++) {
    diffByte[i + 1] = static_cast<uint8_t>(in.getc());
  }
  size -= 7 + 5 * diffCount;

  z_stream rec_strm;
  memset(&rec_strm, 0, sizeof(rec_strm));
  int32_t ret = deflateInit2(&rec_strm, clevel, Z_DEFLATED, static_cast<int32_t>(window), memlevel, Z_DEFAULT_STRATEGY);
  if (Z_OK != ret) {
    return false;
  }
  uint32_t diffIndex{1};
  uint32_t recpos{0};
  for (uint32_t i{0}; i < size; i += BLOCK_SIZE) {
    const uint32_t blsize{(std::min)(static_cast<uint32_t>(size - i), BLOCK_SIZE)};
    in.Read(zin.data(), blsize);
    rec_strm.next_in = zin.data();
    rec_strm.avail_in = blsize;
    do {
      rec_strm.next_out = zout.data();
      rec_strm.avail_out = BLOCK_SIZE;
      ret = deflate(&rec_strm, i + blsize == size ? Z_FINISH : Z_NO_FLUSH);
      if ((Z_BUF_ERROR != ret) && (Z_STREAM_END != ret) && (Z_OK != ret)) {
        break;
      }
      const uint32_t have{(std::min)(BLOCK_SIZE - rec_strm.avail_out, len - recpos)};
      while ((diffIndex <= diffCount) && (static_cast<uint32_t>(diffPos[diffIndex]) >= recpos) && (static_cast<uint32_t>(diffPos[diffIndex]) < (recpos + have))) {
        zout[static_cast<uint32_t>(diffPos[diffIndex]) - recpos] = diffByte[diffIndex];
        diffIndex++;
      }
      if (compare) {
        for (uint32_t j{0}; j < have; j++) {
          if ((zout[j] != out.getc()) && !diffFound) {
            diffFound = recpos + j + 1;
          }
        }
      } else {
        out.Write(zout.data(), have);
      }
      recpos += have;
    } while (0 == rec_strm.avail_out);
  }
  while (diffIndex <= diffCount) {
    if (compare) {
      if (diffByte[diffIndex] != out.getc() && !diffFound) {
        diffFound = recpos + 1;
      }
    } else {
      out.putc(diffByte[diffIndex]);
    }
    diffIndex++;
    recpos++;
  }
  deflateEnd(&rec_strm);
  return (recpos == len) && !diffFound;
}

#if 0

#define BITS 12
#define HASHING_SHIFT BITS - 8
#define MAX_VALUE (1 << BITS) - 1
#define MAX_CODE MAX_VALUE - 1

#if BITS == 14
#  define TABLE_SIZE 18041
#endif
#if BITS == 13
#  define TABLE_SIZE 9029
#endif
#if BITS <= 12
#  define TABLE_SIZE 5021
#endif

static std::array<uint32_t, TABLE_SIZE> prefix_code;
static std::array<uint8_t, 4000> decode_stack;
static std::array<uint8_t, TABLE_SIZE> append_character;

static auto decode_string(uint8_t* buffer, uint32_t code) noexcept -> uint8_t* {
  int32_t i{0};
  while (code > 255) {
    *buffer++ = append_character[code];
    code = prefix_code[code];
    if (i++ > 4000) {
      return nullptr;  // Failure!
    }
  }
  *buffer = uint8_t(code);
  return buffer;
}

static auto input_code(File_t& input) noexcept -> uint32_t {
  static int32_t input_bit_count{0};
  static uint32_t input_bit_buffer{0};

  while (input_bit_count <= 24) {
    input_bit_buffer |= uint32_t(input.getc()) << (24 - input_bit_count);
    input_bit_count += 8;
  }
  uint32_t return_value = input_bit_buffer >> (32 - BITS);
  input_bit_buffer <<= BITS;
  input_bit_count -= BITS;
  return return_value;
}

static auto decode_lzw(File_t& in, File_t& out, int64_t& /*len*/) noexcept -> bool {
  uint32_t next_code{256};
  uint32_t old_code{input_code(in)};
  uint32_t character{old_code};

  out.putc(int32_t(old_code));

  uint8_t* string;
  uint32_t new_code;
  while ((new_code = input_code(in)) != MAX_VALUE) {
    if (new_code >= next_code) {
      decode_stack[0] = uint8_t(character);
      string = decode_string(&decode_stack[1], old_code);
    } else {
      string = decode_string(&decode_stack[0], new_code);
    }
    if (string) {
      character = *string;
      while (string >= &decode_stack[0]) {
        out.putc(*string--);
      }
    } else {
      return false;  // Failure
    }
    if (next_code <= MAX_CODE) {
      prefix_code[next_code] = old_code;
      append_character[next_code] = uint8_t(character);
      next_code++;
    }
    old_code = new_code;
  }
  return true;
}

static auto output_code(File_t& output, const uint32_t code, const bool compare) noexcept -> bool {
  static int32_t output_bit_count{0};
  static uint32_t output_bit_buffer{0};

  output_bit_buffer |= code << (32 - BITS - output_bit_count);
  output_bit_count += BITS;
  while (output_bit_count >= 8) {
    if (compare) {
      const uint32_t cmp{uint32_t(output.getc())};
      if (cmp != (output_bit_buffer >> 24)) {
        return false;
      }
    } else {
      output.putc(uint8_t(output_bit_buffer >> 24));
    }
    output_bit_buffer <<= 8;
    output_bit_count -= 8;
  }
  return true;
}

static std::array<uint32_t, TABLE_SIZE> code_value;

static auto find_match(uint32_t hash_prefix, uint32_t hash_character) noexcept -> uint32_t {
  uint32_t index{(hash_character << (HASHING_SHIFT)) ^ hash_prefix};
  uint32_t offset{(0 == index) ? 1 : TABLE_SIZE - index};
  for (;;) {
    if (UINT_MAX == code_value[index]) {
      return index;
    }
    if (prefix_code[index] == hash_prefix && append_character[index] == hash_character) {
      return index;
    }
    index -= offset;
    if (int32_t(index) < 0) {
      index += TABLE_SIZE;
    }
  }
}

static auto encode_lzw(File_t& in, int64_t /*size*/, File_t& out, const bool compare) noexcept -> bool {
  code_value.fill(UINT_MAX);

  uint32_t next_code{256};

  uint32_t string_code{uint32_t(in.getc())};

  int32_t character;
  while (EOF != (character = in.getc())) {
    uint32_t index{find_match(string_code, uint32_t(character))};
    if (UINT_MAX != code_value[index]) {
      string_code = code_value[index];
    } else {
      if (next_code <= MAX_CODE) {
        code_value[index] = next_code++;
        prefix_code[index] = string_code;
        append_character[index] = uint8_t(character);
      }
      if (!output_code(out, string_code, compare)) {
        return false;  // Failure
      }
      string_code = uint32_t(character);
    }
  }

  if (!output_code(out, string_code, compare)) {
    return false;  // Failure
  }
  if (!output_code(out, MAX_VALUE, compare)) {
    return false;  // Failure
  }
  if (!output_code(out, 0, compare)) {
    return false;  // Failure
  }
  return true;
}

#endif

auto decodeEncodeCompare(File_t& stream, iEncoder_t* const coder, const int64_t safe_pos, const int64_t block_length) noexcept -> int64_t {
  if (block_length > 0) {
    stream.Seek(safe_pos);
    File_t pkzip_tmp /*("_pkzip_tmp_.bin", "wb+")*/;
    int64_t length{block_length};
    if (decode_zlib(stream, pkzip_tmp, length)) {  // Try to decode
      pkzip_tmp.Rewind();
      stream.Seek(safe_pos);
      if (encode_zlib(pkzip_tmp, pkzip_tmp.Size(), stream, true)) {  // Encode and compare
#if 0
        fprintf(stdout, "\nzlib %" PRId64 " --> %" PRId64 "  \n", block_length, pkzip_tmp.Size());
        fflush(stdout);
#endif
        if (nullptr != coder) {
          coder->CompressN(32, pkzip_tmp.Size());
          pkzip_tmp.Rewind();
          for (int32_t c; EOF != (c = pkzip_tmp.getc());) {
            coder->Compress(c);
          }
        }
        return length;  // Success
      }
    } else {
#if 0
      if (decode_lzw(stream, pkzip_tmp, length)) {  // Try to decode
        pkzip_tmp.Rewind();
        stream.Seek(safe_pos);
        if (encode_lzw(pkzip_tmp, pkzip_tmp.Size(), stream, true)) {  // Encode and compare
        }
      }
#endif
    }

#if 0
    fprintf(stderr, "\n>>> zlib %" PRIX64 " / %" PRId64 " / %" PRId64 "\n", safe_pos, length, block_length);
    fflush(stderr);
#endif
  }

  if (nullptr != coder) {
    coder->CompressN(32, iFilter_t::_DEADBEEF);  // Failure...
  }
  stream.Seek(safe_pos);
  return 0;
}
