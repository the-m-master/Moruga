/* GIF, encoding and decoding gif-lzw
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
#include "gif.h"
#include <array>
#include <cassert>
#include <cstdint>
#include <memory>
#include "Buffer.h"
#include "File.h"
#include "Utilities.h"
#include "filter.h"
#include "iEncoder.h"

Gif_t::Gif_t(File_t& in, File_t& out) noexcept : _in{in}, _out{out} {}

Gif_t::~Gif_t() noexcept = default;

static constexpr int32_t marker{0x10FFF};

auto Gif_t::Decode() noexcept -> int64_t {
  _codeSize = _in.getc();
  const int64_t beginIn{_in.Position()};
  int32_t header_size{5};
  _out.putc(header_size >> 8);  // 0
  _out.putc(header_size);       // 1
  _out.putc(_clearPos >> 8);    // 2
  _out.putc(_clearPos);         // 3
  _out.putc(_codeSize);         // 4

  //---------------------------------------------------------------------------
  // Seek all block sizes
  //---------------------------------------------------------------------------
  int32_t block_length;
  while (EOF != (block_length = _in.getc())) {
    if (0 == block_length) {
      break;
    }
    _bsizes.push_back(char(block_length));
    fseek(_in, block_length, SEEK_CUR);
  }
  auto blen = static_cast<int32_t>(_bsizes.size());
  _out.putc(blen >> 24);
  _out.putc(blen >> 16);
  _out.putc(blen >> 8);
  _out.putc(blen);
  const char* ch = _bsizes.c_str();
  while (blen--) {
    _out.putc(*ch++);
  }

  //---------------------------------------------------------------------------
  // Decode LZW blocks
  //---------------------------------------------------------------------------
  for (int32_t phase{0}; phase < 2; ++phase) {
    _in.Seek(beginIn);
    _bits = _codeSize + 1;
    int32_t shift{0};
    int32_t buffer{};
    int32_t maxcode{(1 << _codeSize) + 1};
    int32_t last{-1};
    _table.fill(-1);
    bool end_of_stream{false};
    while ((EOF != (block_length = _in.getc())) && !end_of_stream) {
      for (int32_t i{0}; i < block_length; ++i) {
        buffer |= _in.getc() << shift;
        shift += 8;
        while ((shift >= _bits) && !end_of_stream) {
          _code = buffer & ((1 << _bits) - 1);
          buffer >>= _bits;
          shift -= _bits;

          if ((0 == _bsize) && (_code != (1 << _codeSize))) {
            header_size += 4;
            _out.put32(0);
          }

          if (0 == _bsize) {
            _bsize = block_length;
          }

          if (_code == (1 << _codeSize)) {
            if (maxcode > ((1 << _codeSize) + 1)) {
              if ((0 != _clearPos) && (_clearPos != (marker - maxcode))) {
                return 0;  // Failure
              }
              _clearPos = marker - maxcode;
            }

            _bits = _codeSize + 1;
            maxcode = (1 << _codeSize) + 1;
            last = -1;
            _table.fill(-1);
          } else if (_code == ((1 << _codeSize) + 1)) {
            end_of_stream = true;
          } else if (_code > (maxcode + 1)) {
            return 0;  // Failure
          } else {
            int32_t j{(_code <= maxcode) ? _code : last};
            size_t size{1};

            while (j >= (1 << _codeSize)) {
              _output[4096 - size++] = static_cast<uint8_t>(_dict[static_cast<uint32_t>(j)]);
              j = _dict[static_cast<uint32_t>(j)] >> 8;
            }

            _output[4096 - size] = static_cast<uint8_t>(j);

            if (1 == phase) {
              _out.Write(&_output[4096 - size], size);
            } else {
              _diffPos += size;
            }

            if (_code == maxcode + 1) {
              if (1 == phase) {
                _out.putc(j);
              } else {
                _diffPos++;
              }
            }

            if (-1 != last) {
              if (++maxcode >= 8191) {
                return 0;  // Failure
              }

              if (maxcode <= 4095) {
                const int32_t key{(last << 8) + j};
                const int32_t index{FindMatch(key)};
                _dict[static_cast<uint32_t>(maxcode)] = key;
                _table[static_cast<uint32_t>((index < 0) ? (-index - 1) : _offset)] = maxcode;
                if ((0 == phase) && (index > 0)) {
                  header_size += 4;
                  const uint32_t p{static_cast<uint32_t>(_diffPos - size - (_code == maxcode))};
                  _out.put32(p);
                  _diffPos = size + (_code == maxcode);
                }
              }

              if ((maxcode >= ((1 << _bits) - 1)) && (_bits < 12)) {
                _bits++;
              }
            }
            last = _code;
          }
        }
      }
    }
  }

  _out.Rewind();
  _out.putc(header_size >> 8);  // 0
  _out.putc(header_size);       // 1
  _out.putc(_clearPos >> 8);    // 2
  _out.putc(_clearPos);         // 3
  return _in.Position();
}

auto Gif_t::Encode(int64_t size, const bool compare) noexcept -> int64_t {
  int32_t header_size{_in.getc()};                          // 0
  header_size = ((header_size << 8) + _in.getc() - 5) / 4;  // 1
  _clearPos = _in.getc();                                   // 2
  _clearPos = (_clearPos << 8) + _in.getc();                // 3
  _clearPos = 0xFFFF & (marker - _clearPos);                //
  const int32_t codesize{_in.getc()};                       // 4
  _bits = codesize + 1;
  if ((header_size < 0) || (header_size > 4096) || (_clearPos <= (1 << codesize) + 2)) {
    return 0;  // Failure
  }

  //---------------------------------------------------------------------------
  // Get all block sizes
  //---------------------------------------------------------------------------
  int32_t blen = _in.getc();
  blen = (blen << 8) + _in.getc();
  blen = (blen << 8) + _in.getc();
  blen = (blen << 8) + _in.getc();
  for (int32_t n = 0; n < blen; ++n) {
    _bsizes.push_back(char(_in.getc()));
  }
  _bsize_index = 0;
  _bsize = 0xFF & _bsizes[static_cast<size_t>(_bsize_index++)];

  int32_t curDiff{0};
  std::array<int32_t, 4096> diffPos;
  int32_t maxcode{(1 << codesize) + 1};
  int32_t input{0};

  _table.fill(-1);

  for (uint32_t n{0}; n < static_cast<uint32_t>(header_size); ++n) {
    diffPos[n] = _in.getc();
    diffPos[n] = (diffPos[n] << 8) + _in.getc();
    diffPos[n] = (diffPos[n] << 8) + _in.getc();
    diffPos[n] = (diffPos[n] << 8) + _in.getc();
    if (n > 0) {
      diffPos[n] += diffPos[n - 1];
    }
  }

  size -= static_cast<int64_t>(5 + header_size * 4);
  int32_t last{_in.getc()};
  int64_t total{size + 1};
  _outsize = 1;
  _block_size = 0;

  if (compare) {
    const int32_t ch{_out.getc()};
    if ((ch != codesize) && (0 == _diffFound)) {
      _diffFound = 1;
    }
  } else {
    putc(codesize, _out);
  }

  if ((0 == header_size) || (0 != diffPos[0])) {
    if (WriteCode(1 << codesize, compare)) {
      return 0;  // Failure
    }
  } else {
    curDiff++;
  }

  while ((0 != size) && (EOF != (input = _in.getc()))) {
    size--;
    const int32_t key{(last << 8) + input};
    const int32_t index{(last < 0) ? input : FindMatch(key)};
    _code = index;

    if ((curDiff < header_size) && (static_cast<int32_t>(total - size) > diffPos[static_cast<uint32_t>(curDiff)])) {
      curDiff++;
      _code = -1;
    }

    if (_code < 0) {
      if (WriteCode(last, compare)) {
        return 0;  // Failure
      }

      if (maxcode == _clearPos) {
        if (WriteCode(1 << codesize, compare)) {
          return 0;  // Failure
        }

        _bits = codesize + 1;
        maxcode = (1 << codesize) + 1;
        _table.fill(-1);
      } else {
        ++maxcode;
        if (maxcode <= 4095) {
          _dict[static_cast<uint32_t>(maxcode)] = key;
          _table[static_cast<uint32_t>((index < 0) ? (-index - 1) : _offset)] = maxcode;
        }

        if ((maxcode >= (1 << _bits)) && (_bits < 12)) {
          _bits++;
        }
      }
      _code = input;
    }
    last = _code;
  }

  if (WriteCode(last, compare)) {
    return 0;  // Failure
  }

  if (WriteCode((1 << codesize) + 1, compare)) {
    return 0;  // Failure
  }

  if (_shift > 0) {
    ++_block_size;
    _output[static_cast<uint32_t>(_block_size)] = static_cast<uint8_t>(_buffer);
    if (_block_size == _bsize) {
      if (WriteBlock(_bsize, compare)) {
        return 0;  // Failure
      }
    }
  }

  if (_block_size > 0) {
    if (WriteBlock(_block_size, compare)) {
      return 0;  // Failure
    }
  }

  if (compare) {
    const int32_t ch = _out.getc();
    if ((0 != ch) && (0 == _diffFound)) {
      _diffFound = _outsize + 1;
      return 0;  // Failure
    }
  } else {
    _out.putc(0);
  }
  return _outsize + 1;
}

auto Gif_t::FindMatch(const int32_t k) noexcept -> int32_t {
  auto offset{(Utilities::PHI32 * static_cast<uint32_t>(k)) >> (32 - 13)};
  const auto stride{(0 == offset) ? 1 : (LZW_TABLE_SIZE - offset)};
  for (;;) {
    assert(offset < LZW_TABLE_SIZE);
    auto index{_table[offset]};
    if (index < 0) {
      index = -static_cast<int32_t>(offset) - 1;
      return index;
    }

    if (_dict[static_cast<uint32_t>(index)] == k) {
      return index;
    }

    offset -= stride;
    if (static_cast<int32_t>(offset) < 0) {
      offset += LZW_TABLE_SIZE;
    }
  }
}

auto Gif_t::WriteBlock(int32_t count, const bool compare) noexcept -> bool {
  _output[0] = static_cast<uint8_t>(count);
  if (compare) {
    for (uint32_t n{0}; n < static_cast<uint32_t>(count) + 1; ++n) {
      const auto ch{_out.getc()};
      if ((_output[n] != ch) && (0 == _diffFound)) {
        _diffFound = _outsize + static_cast<int64_t>(n + 1);
        return true;  // Failure
      }
    }
  } else {
    _out.Write(&_output[0], static_cast<size_t>(count + 1));
  }
  _outsize += static_cast<int64_t>(count + 1);
  _block_size = 0;
  _bsize = 0xFF & _bsizes[static_cast<size_t>(_bsize_index++)];
  return false;
}

auto Gif_t::WriteCode(const int32_t code, const bool compare) noexcept -> bool {
  _buffer += code << _shift;
  _shift += _bits;
  while (_shift >= 8) {
    ++_block_size;
    _output[static_cast<uint32_t>(_block_size)] = static_cast<uint8_t>(_buffer);
    _buffer >>= 8;
    _shift -= 8;
    if (_block_size == _bsize) {
      if (WriteBlock(_bsize, compare)) {
        return true;  // Failure
      }
    }
  }
  return false;
}

auto Header_t::ScanGIF(int32_t /*ch*/) noexcept -> Filter {
  // ------------------------------------------------------------------------
  // GIF header
  // -------+------+---------------------------------------------------------
  // offset | size | description
  // -------+------+---------------------------------------------------------
  //      0 |    4 | "GIF8"
  //      4 |    2 | "7a" or "9a"
  //      6 |    2 | Logical screen width in pixels
  //      8 |    2 | Logical screen height in pixels
  //      A |    1 | GCT follows for 256 colours with resolution 3x8 _bits/primary;
  //        |      | the lowest 3 _bits represent the bit depth minus 1,
  //        |      | the highest true bit means that the GCT is present
  //      B |    1 | <Background Colour Index>
  //      C |    1 | <Pixel Aspect Ratio>
  //      D |    ? | <Global Colour Table(0..255 x 3 bytes) if GCTF is set
  //        |    ? | <Blocks>
  //           ...

  static constexpr uint32_t offset{11};

  const auto hdr{m4(offset - 0x0)};
  if (('GIF8' == hdr) && (('7' == _buf(offset - 4)) || ('9' == _buf(offset - 4))) && ('a' == _buf(offset - 5))) {
    const auto width{i2(offset - 6)};
    const auto height{i2(offset - 8)};
    if ((width > 0) && (width < 0x4000) && (height > 0) && (height < 0x4000)) {
      _di.offset_to_start = 0;  // start now!
      _di.filter_end = _encode ? 1 : 0x7FFFFFFF;
#if 0
      fprintf(stderr, "GIF8%ca %ux%ux%u %u\n", _buf(offset - 4), width, height, (7 & _buf(offset - 10)) + 1, _buf.Pos());
      fflush(stderr);
#endif
      return Filter::GIF;
    }
  }

  return Filter::NOFILTER;
}

GIF_filter::GIF_filter(File_t& stream, iEncoder_t* const coder, DataInfo_t& di, const Buffer_t& __restrict buf, const int64_t original_length)
    : _buf{buf},  //
      _original_length{original_length},
      _stream{stream},
      _coder{coder},
      _di{di} {}

auto GIF_filter::read_sub_blocks(bool& eof) const noexcept -> int32_t {
  auto data_length{0};

  for (;;) {
    const auto ch = _stream.getc();
    if (EOF == ch) {
      eof = true;
      return -1;
    }

    const auto block_size{static_cast<uint8_t>(ch)};
    if (0 == block_size) {  // end of sub-blocks
      break;
    }

    data_length += block_size;

    if (0 != fseek(_stream, block_size, SEEK_CUR)) {
      return -1;
    }
  }

  return data_length;
}

GIF_filter::~GIF_filter() noexcept = default;

auto GIF_filter::get_frame(bool& eof) const noexcept -> int32_t {
  Frames_t frame_type{Frames_t(_stream.getc())};
  if (EOF == static_cast<int32_t>(frame_type)) {
    eof = true;
    return -1;  // Failure
  }

  while (IMAGE_DESCRIPTOR != frame_type) {
    if (TRAILER == frame_type) {
      fseek(_stream, -1, SEEK_CUR);
      return 0;  // End of frames
    }
    if (EXTENSION_INTRODUCER == frame_type) {
      frame_type = Frames_t(_stream.getc());
      switch (frame_type) {
        case PLAINTEXT_EXTENSION:
          fseek(_stream, 13, SEEK_CUR);
          break;

        case GRAPHIC_CONTROL:
          fseek(_stream, 5, SEEK_CUR);
          break;

        case COMMENT_EXTENSION:
          // do nothing - all the data is in the sub-blocks that follow.
          break;

        case APPLICATION_EXTENSION:
          fseek(_stream, 1 + 8 + 3, SEEK_CUR);  // Block size (always 0x0B), Application Identifier, Application Authentication Code
          break;

        case EXTENSION_INTRODUCER:
        case IMAGE_DESCRIPTOR:
        case TRAILER:
        default:
          return -1;  // Failure
      }

      if (read_sub_blocks(eof) < 0) {
        return -1;  // Failure
      }
    } else {
      return -1;  // Failure
    }

    frame_type = Frames_t(_stream.getc());
    if (EOF == static_cast<int32_t>(frame_type)) {
      eof = true;
      return -1;  // Failure
    }
  }
  return 1;  // Got new frame
}

auto GIF_filter::Handle(int32_t /*ch*/) noexcept -> bool {  // encoding
  const auto root_origin{_stream.Position() - 1};
  assert(root_origin > 0);

  auto frame_origin{root_origin};

  fseek(_stream, (1 + 1) - 1, SEEK_CUR);  // Background Colour Index, Pixel Aspect Ratio

  const auto fdsz{_buf(1)};
  if (0x80 & fdsz) {
    const auto gct_sz{1 << ((7 & fdsz) + 1)};
    fseek(_stream, 3 * gct_sz, SEEK_CUR);  // Global Colour Table
  }

  uint32_t frame{0};
  bool eof{false};
  int32_t ret;  // 1 got new frame, 0 end of frames, -1 failure
  for (; 1 == (ret = get_frame(eof)); ++frame) {
    fseek(_stream, 8, SEEK_CUR);  // x,y,w,h

    const uint8_t fisrz{static_cast<uint8_t>(_stream.getc())};
    if (0x80 & fisrz) {
      const auto gct_sz{1 << ((7 & fisrz) + 1)};
      fseek(_stream, 3 * gct_sz, SEEK_CUR);  // Global Colour Table
    }

    const int64_t gif_data_position{_stream.Position()};

    File_t gif_raw;
    std::unique_ptr<Gif_t> gif_read(new Gif_t(_stream, gif_raw));
    const auto decoded_position = gif_read->Decode();
    if (decoded_position > 0) {
      gif_raw.Rewind();
      const auto decoded_length = gif_raw.Size();
      _stream.Seek(gif_data_position);
      std::unique_ptr<Gif_t> gif_verify(new Gif_t(gif_raw, _stream));
      const auto length = gif_verify->Encode(decoded_length, true);
      if (length == (decoded_position - gif_data_position)) {
#if 0
        fprintf(stdout, "\nGIF frame %u   \n", frame);
        fflush(stdout);
#endif
        if (frame > 0) {
          _coder->Compress(IMAGE_DESCRIPTOR);
        }
        _stream.Seek(frame_origin);
        auto len = static_cast<int32_t>(gif_data_position - frame_origin);
        _coder->Compress(len >> 24);  // Save GIF header length
        _coder->Compress(len >> 16);
        _coder->Compress(len >> 8);
        _coder->Compress(len);
        while (len--) {
          const auto ch = _stream.getc();
          _coder->Compress(ch);
        }

        gif_raw.Rewind();
        len = static_cast<int32_t>(decoded_length);
        _coder->Compress(len >> 24);  // Save GIF raw data length
        _coder->Compress(len >> 16);
        _coder->Compress(len >> 8);
        _coder->Compress(len);
        while (len--) {
          const auto ch = gif_raw.getc();
          _coder->Compress(ch);
        }
      } else {  // Failure!
#if 0
        fprintf(stderr, "\nGIF decode failure in frame %u!   \n", frame);
        fflush(stderr);
#endif

        assert(frame_origin < gif_data_position);
        if (frame > 0) {
          _coder->Compress(IMAGE_DESCRIPTOR);
        }
        _stream.Seek(frame_origin);
        auto len{static_cast<int32_t>(decoded_position - frame_origin)};
        len = ~len;
        _coder->Compress(len >> 24);
        _coder->Compress(len >> 16);
        _coder->Compress(len >> 8);
        _coder->Compress(len);
        len = ~len;
        int32_t ch{0};
        while (len-- && (EOF != (ch = _stream.getc()))) {
          _coder->Compress(ch);
        }
        if (EOF == ch) {
          eof = true;
          ret = -1;  // Decoder failure
          break;
        }
      }
      frame_origin = decoded_position;
      _stream.Seek(frame_origin);
    } else {
      ret = -1;  // Decoder failure
      break;
    }
  }

  _stream.Seek(frame_origin);

  if (!eof && (-1 == ret) && (0 == frame)) {  // Failure?
    _coder->CompressN(32, _DEADBEEF);
    _stream.Seek(root_origin);
  }
  return true;
}

auto GIF_filter::Handle(int32_t ch, int64_t& pos) noexcept -> bool {  // decoding
  if (_frame_just_decoded) {
    _frame_just_decoded = false;
    if (IMAGE_DESCRIPTOR == ch) {           // An other GIF frame?
      if ((_original_length - pos) > 16) {  // Do not be tricked by end of file junk bytes...
        _gif = true;
        return _gif;
      }
    }
    _di.filter_end = 0;  // Stop!
    return _gif;
  }

  if (_imageEnd < 4) {
    if (0 == _imageEnd) {
      pos -= 5;  // Should not be needed, but the GIF format might be damaged
    }
    ++_imageEnd;
    _length = (_length << 8) | ch;
  } else {
    if (_DEADBEEF == static_cast<uint32_t>(_length)) {  // GIF decoder did fail
      _gif = false;
      _gif_phase = 0;
      _gif_length = 0;
      _imageEnd = 0;
      _frame_just_decoded = false;
      _di.filter_end = 0;  // Stop!
      pos = _stream.Position();
      return _gif;
    }

    if (_length < 0) {
      _length = ~_length;
      _gif_phase = 2;  // Recovery phase
    }

    if (_gif && (2 == _gif_phase)) {  // Recovery
      if (_length > 0) {
        --_length;
        _stream.putc(ch);
      }
      if (0 == _length) {
        _gif = false;
        _gif_phase = 0;
        _gif_length = 0;
        _imageEnd = 0;
        _frame_just_decoded = true;
        pos = _stream.Position() - 1;
        return true;
      }
    }

    if (_gif && (1 == _gif_phase)) {  // GIF Data
      if (nullptr == _gif_raw) {
        pos -= _length;
        _gif_length = _length;
        _gif_raw = new File_t();
      }
      if (_length > 0) {
        --_length;
        _gif_raw->putc(ch);
      }
      if (0 == _length) {
        std::unique_ptr<Gif_t> gif_write(new Gif_t(*_gif_raw, _stream));
        _gif_raw->Rewind();
        gif_write->Encode(_gif_length, false);
        _gif = false;
        _gif_phase = 0;
        _gif_length = 0;
        delete _gif_raw;
        _gif_raw = nullptr;
        _imageEnd = 0;
        _frame_just_decoded = true;
        pos = _stream.Position() - 1;
        return true;
      }
    }

    if (_gif && (0 == _gif_phase)) {  // GIF Header
      if (_length > 0) {
        --_length;
        _stream.putc(ch);
      }
      if (0 == _length) {
        _imageEnd = 0;
        _gif_phase = 1;
      }
    }
  }
  return _gif;
}
