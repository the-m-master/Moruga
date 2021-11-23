/* Copyright (c) 2019-2021 Marwijn Hessel
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

static constexpr uint32_t block{1 << 16};
static constexpr uint32_t limit{128};

#define ZLIB_NUM_COMBINATIONS 81

class zLibMTF final {
public:
  explicit zLibMTF() {
    for (uint32_t i{0}; i < ZLIB_NUM_COMBINATIONS; i++) {
      _previous[i] = int32_t(i - 1);
      _next[i] = int32_t(i + 1);
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
      _index = _next[uint32_t(_index)];
      return _index;
    }
    return _index;  //-1
  }

  void Insert(int32_t i) noexcept {
    if ((_index = i) == _root) {
      return;
    }
    int32_t p = _previous[uint32_t(_index)];
    int32_t n = _next[uint32_t(_index)];
    if (p >= 0) {
      _next[uint32_t(p)] = _next[uint32_t(_index)];
    }
    if (n >= 0) {
      _previous[uint32_t(n)] = _previous[uint32_t(_index)];
    }
    _previous[uint32_t(_root)] = _index;
    _next[uint32_t(_index)] = _root;
    _root = _index;
    _previous[uint32_t(_root)] = -1;
  }

private:
  int32_t _root{0};
  int32_t _index{0};
  std::array<int32_t, ZLIB_NUM_COMBINATIONS> _previous{};
  std::array<int32_t, ZLIB_NUM_COMBINATIONS> _next{};
};

zLibMTF::~zLibMTF() noexcept = default;

static auto decode_zlib(File_t& in, File_t& out, int64_t len) noexcept -> bool {
  std::array<uint8_t, block * 2> zin;
  std::array<uint8_t, block> zout;
  std::array<uint8_t, block * 2> zrec;
  std::array<uint8_t, ZLIB_NUM_COMBINATIONS * limit> diffByte{};
  std::array<int32_t, ZLIB_NUM_COMBINATIONS * limit> diffPos{};

  // Step 1 - parse offset type form zlib stream header
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

  // Step 2 - check recompressiblitiy, determine parameters and save differences
  std::array<z_stream, ZLIB_NUM_COMBINATIONS> rec_strm;
  std::array<uint32_t, ZLIB_NUM_COMBINATIONS> diffCount;
  std::array<uint32_t, ZLIB_NUM_COMBINATIONS> recpos;
  int32_t main_ret{Z_STREAM_END};
  z_stream main_strm;
  memset(&main_strm, 0, sizeof(main_strm));
  if (Z_OK != zlib_inflateInit(&main_strm, zh)) {
    return false;
  }

  for (uint32_t i{0}; i < ZLIB_NUM_COMBINATIONS; i++) {
    uint32_t clevel{(i / 9) + 1};
    // Early skip if invalid parameter
    if ((clevel < minclevel) || (clevel > maxclevel)) {
      diffCount[i] = limit;
      continue;
    }
    const uint32_t memlevel{(i % 9) + 1};
    memset(&rec_strm[i], 0, sizeof(rec_strm[i]));
    int32_t ret = deflateInit2(&rec_strm[i], int32_t(clevel), Z_DEFLATED, window - MAX_WBITS, int32_t(memlevel), Z_DEFAULT_STRATEGY);
    diffCount[i] = (Z_OK == ret) ? 0 : limit;
    recpos[i] = block * 2;
    diffPos[i * limit] = -1;
    diffByte[i * limit] = 0;
  }

  zLibMTF mtf{};
  for (int64_t i{0}; i < len; i += block) {
    const uint32_t blsize{(std::min)(uint32_t(len - i), block)};
    nTrials = 0;
    for (uint32_t j{0}; j < ZLIB_NUM_COMBINATIONS; j++) {
      if (diffCount[j] == limit) {
        continue;
      }
      nTrials++;
      if (recpos[j] >= block) {
        recpos[j] -= block;
      }
    }
    // early break if nothing left to test
    if (0 == nTrials) {
      break;
    }

    memmove(&zrec[0], &zrec[block], block);
    memmove(&zin[0], &zin[block], block);
    in.Read(&zin[block], blsize);  // Read block from input file

    // Decompress/inflate block
    main_strm.next_in = &zin[block];
    main_strm.avail_in = blsize;
    do {
      main_strm.next_out = &zout[0];
      main_strm.avail_out = block;
      main_ret = inflate(&main_strm, Z_FINISH);
      nTrials = 0;
      // Recompress/deflate block with all possible parameters
      for (int32_t j{mtf.GetFirst()}; j >= 0; j = mtf.GetNext()) {
        if (limit == diffCount[uint32_t(j)]) {
          continue;
        }
        nTrials++;
        rec_strm[uint32_t(j)].next_in = &zout[0];
        rec_strm[uint32_t(j)].avail_in = block - main_strm.avail_out;
        rec_strm[uint32_t(j)].next_out = &zrec[recpos[uint32_t(j)]];
        rec_strm[uint32_t(j)].avail_out = (block * 2) - recpos[uint32_t(j)];
        int32_t ret{deflate(&rec_strm[uint32_t(j)], (int64_t(main_strm.total_in) == len) ? Z_FINISH : Z_NO_FLUSH)};
        if ((Z_BUF_ERROR != ret) && (Z_STREAM_END != ret) && (Z_OK != ret)) {
          diffCount[uint32_t(j)] = limit;
          continue;
        }

        // Compare
        const uint32_t end{(2 * block) - rec_strm[uint32_t(j)].avail_out};
        const uint32_t tail{(std::max)((Z_STREAM_END == main_ret) ? uint32_t(len - int64_t(rec_strm[uint32_t(j)].total_out)) : 0u, 0u)};
        for (uint32_t k{recpos[uint32_t(j)]}; k < (end + tail); k++) {
          if (((k < end) && ((i + k - block) < len) && (zrec[k] != zin[k])) || (k >= end)) {
            if (++diffCount[uint32_t(j)] < limit) {
              const uint32_t p{uint32_t(j) * limit + diffCount[uint32_t(j)]};
              diffPos[p] = int32_t(i + k - block);
              assert(k < zin.size());
              diffByte[p] = zin[k];
            }
          }
        }
        // Early break on perfect match
        if ((Z_STREAM_END == main_ret) && (0 == diffCount[uint32_t(j)])) {
          index = j;
          found = true;
          break;
        }
        recpos[uint32_t(j)] = 2 * block - rec_strm[uint32_t(j)].avail_out;
      }
    } while ((0 == main_strm.avail_out) && (Z_BUF_ERROR == main_ret) && (nTrials > 0));

    if (((Z_BUF_ERROR != main_ret) && (Z_STREAM_END != main_ret)) || (0 == nTrials)) {
      break;
    }
  }
  uint32_t minCount{found ? 0 : limit};
  for (int32_t i{ZLIB_NUM_COMBINATIONS - 1}; i >= 0; i--) {
    const uint32_t clevel{(uint32_t(i) / 9) + 1};
    if ((clevel >= minclevel) && (clevel <= maxclevel)) {
      deflateEnd(&rec_strm[uint32_t(i)]);
    }
    if (!found && (diffCount[uint32_t(i)] < minCount)) {
      index = i;
      minCount = diffCount[uint32_t(i)];
    }
  }
  inflateEnd(&main_strm);
  if (limit == minCount) {
    return false;
  }
  mtf.Insert(index);

  // Step 3 - write parameters, differences and precompressed (inflated) data
  out.putc(int32_t(diffCount[uint32_t(index)]));
  out.putc(window);
  out.putc(index);
  for (uint32_t i{0}; i <= diffCount[uint32_t(index)]; i++) {
    const auto v{(i == diffCount[uint32_t(index)]) ? uint32_t(len - diffPos[uint32_t(index) * limit + i]) :  //
                     uint32_t(diffPos[uint32_t(index) * limit + i + 1] - diffPos[uint32_t(index) * limit + i] - 1)};
    out.put32(v);
  }
  for (uint32_t i{0}; i < diffCount[uint32_t(index)]; i++) {
    out.putc(diffByte[uint32_t(index) * limit + i + 1]);
  }

  in.Seek(safe_pos);
  memset(&main_strm, 0, sizeof(main_strm));
  if (zlib_inflateInit(&main_strm, zh) != Z_OK) {
    return false;
  }
  for (uint32_t i{0}; i < len; i += block) {
    uint32_t blsize = ((std::min))(uint32_t(len - i), block);
    in.Read(&zin[0], blsize);
    main_strm.next_in = &zin[0];
    main_strm.avail_in = blsize;
    do {
      main_strm.next_out = &zout[0];
      main_strm.avail_out = block;
      main_ret = inflate(&main_strm, Z_FINISH);
      out.Write(&zout[0], block - main_strm.avail_out);
    } while ((0 == main_strm.avail_out) && (Z_BUF_ERROR == main_ret));
    if ((Z_BUF_ERROR != main_ret) && (Z_STREAM_END != main_ret)) {
      break;
    }
  }
  inflateEnd(&main_strm);
  return Z_STREAM_END == main_ret;
}

auto encode_zlib(File_t& in, int64_t size, File_t& out, const bool compare) noexcept -> bool {
  uint64_t diffFound{0};
  std::array<uint8_t, block> zin;
  std::array<uint8_t, block> zout;
  const uint32_t diffCount = (std::min)(uint32_t(in.getc()), limit - 1);
  uint32_t window{uint32_t(in.getc() - MAX_WBITS)};
  const int32_t index{in.getc()};
  const int32_t memlevel{(index % 9) + 1};
  const int32_t clevel{(index / 9) + 1};
  uint32_t len{0};

  std::array<int32_t, limit> diffPos;
  diffPos[0] = -1;
  for (uint32_t i{0}; i <= diffCount; i++) {
    int32_t v{int32_t(in.get32())};
    if (i == diffCount) {
      len = uint32_t(v + diffPos[i]);
    } else {
      diffPos[i + 1] = v + diffPos[i] + 1;
    }
  }
  std::array<uint8_t, limit> diffByte;
  diffByte[0] = 0;
  for (uint32_t i{0}; i < diffCount; i++) {
    diffByte[i + 1] = uint8_t(in.getc());
  }
  size -= 7 + 5 * diffCount;

  z_stream rec_strm;
  memset(&rec_strm, 0, sizeof(rec_strm));
  int32_t ret = deflateInit2(&rec_strm, clevel, Z_DEFLATED, int32_t(window), memlevel, Z_DEFAULT_STRATEGY);
  if (Z_OK != ret) {
    return false;
  }
  uint32_t diffIndex{1};
  uint32_t recpos{0};
  for (uint32_t i{0}; i < size; i += block) {
    const uint32_t blsize{(std::min)(uint32_t(size - i), block)};
    in.Read(&zin[0], blsize);
    rec_strm.next_in = &zin[0];
    rec_strm.avail_in = blsize;
    do {
      rec_strm.next_out = &zout[0];
      rec_strm.avail_out = block;
      ret = deflate(&rec_strm, i + blsize == size ? Z_FINISH : Z_NO_FLUSH);
      if ((Z_BUF_ERROR != ret) && (Z_STREAM_END != ret) && (Z_OK != ret)) {
        break;
      }
      const uint32_t have{(std::min)(block - rec_strm.avail_out, len - recpos)};
      while ((diffIndex <= diffCount) && (uint32_t(diffPos[diffIndex]) >= recpos) && (uint32_t(diffPos[diffIndex]) < (recpos + have))) {
        zout[uint32_t(diffPos[diffIndex]) - recpos] = diffByte[diffIndex];
        diffIndex++;
      }
      if (compare) {
        for (uint32_t j{0}; j < have; j++) {
          if ((zout[j] != out.getc()) && !diffFound) {
            diffFound = recpos + j + 1;
          }
        }
      } else {
        out.Write(&zout[0], have);
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

void decodeEncodeCompare(File_t& stream, iEncoder_t& coder, const int64_t safe_pos, const int64_t block_length) noexcept {
  if (block_length > 0) {
    stream.Seek(safe_pos);
    File_t pkzip_tmp;
    if (decode_zlib(stream, pkzip_tmp, block_length)) {  // Try to decode
      pkzip_tmp.Rewind();
      stream.Seek(safe_pos);
      if (encode_zlib(pkzip_tmp, pkzip_tmp.Size(), stream, true)) {  // Encode and compare
#if 0
        fprintf(stdout, "\nzlib %" PRId64 " --> %" PRId64 "  \n", block_length, pkzip_tmp.Size());
        fflush(stdout);
#endif
        coder.CompressN(32, pkzip_tmp.Size());
        pkzip_tmp.Rewind();
        for (int32_t c; EOF != (c = pkzip_tmp.getc());) {
          coder.Compress(c);
        }
        return;  // Success
      }
    }

#if 0
    fprintf(stderr, "\n>>> zlib %" PRIX64 " / %" PRId64 "   \n", safe_pos, block_length);
    fflush(stderr);
#endif
  }

  coder.CompressN(32, iFilter_t::_DEADBEEF);  // Failure...
  stream.Seek(safe_pos);
}
