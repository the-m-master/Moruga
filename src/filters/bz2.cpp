#include "bz2.h"
#include <cassert>
#include <climits>
#include <cstdint>
#include "Buffer.h"
#include "File.h"
#include "filter.h"
#include "gzip.h"
#include "iEncoder.h"

namespace {
  constexpr auto offset{BZ2_filter::BZ2_HEADER - 1};
};  // namespace

auto Header_t::ScanBZ2(int32_t /*ch*/) noexcept -> Filter {
  // ------------------------------------------------------------------------
  // BZip2 header
  // -------+------+---------------------------------------------------------
  // offset | size | description
  // -------+------+---------------------------------------------------------
  //      0 |   2  | The signature of the local file header. This is always 'BZ'.
  //      2 |   1  | BZip2 version, 'h' for Bzip2 ('H'uffman coding), '0' for Bzip1 (deprecated)
  //      3 |   1  | '1'..'9' block-size 100 kB-900 kB (uncompressed)
  //      4 |   6  | Compressed magic = 0x314159265359 (BCD (pi))
  //     10 |   4  | Checksum for this block
  //     14 |   1  | 0=>normal, 1=>randomised (deprecated)
  //     15 |   3  | Starting pointer into BWT for after untransform
  //     18 |   2  | Bitmap, of ranges of 16 bytes, present/not present
  //     20 |0..256| Bitmap, of symbols used, present/not present (multiples of 16)
  //      x | ...  | 2..6 number of different Huffman tables in use
  //      x | ...  | Number of times that the Huffman tables are swapped (each 50 symbols)
  //      x | ...  | Zero-terminated bit runs (0..62) of MTF'ed Huffman table (*selectors_used)
  //      x | ...  | 0..20 starting bit length for Huffman deltas

  const auto header{_buf.m4(offset)};
  if (('BZh\x0' == (UINT32_C(0xFFFFFF00) & header)) && (UINT32_C(0x31415926) == _buf.m4(offset - 4))) {
    const auto bzlevel = (0xFF & header) - '0';
    if ((bzlevel >= 1) && (bzlevel <= 9)) {
      _di.offset_to_start = 0;   // start now!
      _di.filter_end = INT_MAX;  // end never..
      return Filter::BZ2;
    }
  }

  return Filter::NOFILTER;
}

BZ2_filter::BZ2_filter(File_t& stream, iEncoder_t* const coder, DataInfo_t& di, const int64_t original_length) noexcept
    : _original_length{original_length},  //
      _stream{stream},
      _coder{coder},
      _di{di} {}

BZ2_filter::~BZ2_filter() noexcept = default;

auto BZ2_filter::Handle(int32_t ch) noexcept -> bool {  // encoding
  const int64_t safe_pos{_stream.Position()};
  _coder->Compress(ch);  // Encode last character
  DecodeEncodeCompare(_stream, _coder, safe_pos, _original_length, 0);
  _di.filter_end = 0;
  return true;
}

auto BZ2_filter::Handle(int32_t ch, int64_t& pos) noexcept -> bool {  // decoding
  if (_data && (_block_length > 0)) {
    --_block_length;
    _data->putc(ch);
    if (0 == _block_length) {
      _data->Rewind();
      const bool status{EncodeGZip(*_data, _data->Size(), _stream)};
      (void)status;  // Avoid warning in release mode
      assert(status);
      delete _data;
      _data = nullptr;
      pos = _stream.Position() - 1;
      _di.filter_end = 0;
    }
    return true;
  }

  if (_length > 0) {
    --_length;
    _block_length = (_block_length << 8) | ch;
    if (0 == _length) {
      if ((_DEADBEEF != static_cast<uint32_t>(_block_length)) && (_block_length > 0)) {
        _data = new File_t;
        pos -= _block_length;
      } else {
        _block_length = 0;
        pos = _stream.Position() - 1;
        _di.filter_end = 0;
      }
    }
    return true;
  }

  _stream.putc(ch);
  _block_length = 0;
  _length = 4;
  return true;
}
