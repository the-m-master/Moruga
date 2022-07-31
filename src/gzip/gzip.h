/*===============================================================================
 * Moruga project
 *===============================================================================
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
 *===============================================================================
 */
#pragma once

#include <sys/types.h>
#include <array>
#include <cassert>
#include <climits>
#include <cstdint>
#include <cstdio>

typedef uint32_t (*write_buffer_t)(const void* buf, uint32_t cnt, void* ptr) noexcept;

#define ISPOWEROF2(x) (((x) > 0) && (!((x) & ((x)-1))))

#define GZip_OK 0
#define GZip_ERROR 1
#define GZip_WARNING 2

/* Compression methods (see algorithm.doc) */
#define STORED 0
#define DEFLATED 8

#define BITS 16

#define INBUFSIZ 0x40000    /* input buffer size */
#define INBUF_EXTRA 64      /* required by unlzw() */
#define OUTBUFSIZ 0x40000   /* output buffer size */
#define OUTBUF_EXTRA 2048   /* required by unlzw() */
#define DIST_BUFSIZE 0x8000 /* buffer for distances, see trees.c */
#define WSIZE 0x8000        /* window size--must be a power of two, and at least 32K for zip's deflate method */

#define MIN_MATCH 3
#define MAX_MATCH 258

#define MIN_LOOKAHEAD (MAX_MATCH + MIN_MATCH + 1)
#define MAX_DIST (WSIZE - MIN_LOOKAHEAD)

#define Assert(cond, msg) assert(cond)
#define Trace(x)
#define Tracev(x)
#define Tracevv(x)
#define Tracec(c, x)
#define Tracecv(c, x)

/* gzip flag byte */
#define ASCII_FLAG 0x01  /* bit 0 set: file probably ascii text */
#define HEADER_CRC 0x02  /* bit 1 set: CRC16 for the gzip header */
#define EXTRA_FIELD 0x04 /* bit 2 set: extra field present */
#define ORIG_NAME 0x08   /* bit 3 set: original file name present */
#define COMMENT 0x10     /* bit 4 set: file comment present */
#define ENCRYPTED 0x20   /* bit 5 set: file is encrypted */
#define RESERVED 0xC0    /* bit 6,7:   reserved */
#define OS_CODE 0x03     /* assume Unix */

/* internal file attribute */
#define UNKNOWN 0xffff
#define BINARY 0
#define ASCII 1

namespace gzip {
  extern const uint8_t* imem;                                   // Memory input
  extern FILE* ifd;                                             // input file descriptor
  extern FILE* ofd;                                             // output file descriptor
  extern int32_t block_start;                                   // window offset of current block
  extern int32_t level;                                         // compression level
  extern int32_t rsync;                                         // deflate into rsyncable chunks
  extern std::array<uint8_t, INBUFSIZ + INBUF_EXTRA> inbuf;     // input buffer
  extern std::array<uint8_t, OUTBUFSIZ + OUTBUF_EXTRA> outbuf;  // output buffer
  extern std::array<uint8_t, WSIZE + WSIZE> window;             // Sliding window and suffix table (unlzw)
  extern uint32_t bytes_in;                                     // number of input bytes
  extern uint32_t bytes_out;                                    // number of output bytes
  extern uint32_t ifile_size;                                   // input file size
  extern uint32_t inptr;                                        // index of next byte to be processed in inbuf
  extern uint32_t insize;                                       // valid bytes in inbuf
  extern uint32_t outcnt;                                       // bytes in output buffer
  extern uint32_t strstart;                                     // window offset of current string
  extern void* this_pointer;                                    // Optional helper pointer
  extern write_buffer_t omem;                                   // Write output data to memory

  extern auto bi_reverse(uint32_t value, int32_t length) noexcept -> uint32_t;
  extern auto ct_tally(int32_t dist, int32_t lc) noexcept -> int32_t;
  extern auto deflate(int32_t pack_level) noexcept -> int32_t;
  extern auto file_read(void* buf, uint32_t size) noexcept -> int32_t;
  extern auto fill_inbuf(int32_t eof_ok) noexcept -> int32_t;
  extern auto flush_block(char* buf, uint32_t stored_len, int32_t pad, int32_t eof) noexcept -> int32_t;
  extern auto inflate() noexcept -> int32_t;
  extern auto read_buffer(FILE* fd, void* buf, uint32_t cnt) noexcept -> uint32_t;
  extern void bi_init() noexcept;
  extern void bi_windup() noexcept;
  extern void copy_block(char* buf, uint32_t len, int32_t header) noexcept;
  extern void ct_init(uint16_t* attr) noexcept;
  extern void flush_outbuf() noexcept;
  extern void flush_window() noexcept;
  extern void send_bits(int32_t value, int32_t length) noexcept;

  inline static void put_byte(uint8_t c) noexcept {
    outbuf[outcnt++] = c;
    if (outcnt == OUTBUFSIZ) {
      flush_outbuf();
    }
  }

  /* Output a 16 bit value, lsb first */
  inline static void put_short(uint16_t w) noexcept {
    if (outcnt < OUTBUFSIZ - 2) {
      outbuf[outcnt++] = uint8_t(w & 0xFF);
      outbuf[outcnt++] = uint8_t(w >> 8);
    } else {
      put_byte(uint8_t(w & 0xFF));
      put_byte(uint8_t(w >> 8));
    }
  }

  /* Output a 32 bit value to the bit stream, lsb first */
  inline static void put_long(uint32_t n) noexcept {
    put_short(uint16_t(n & 0xFFFF));
    put_short(uint16_t(n >> 16));
  }

  auto zip(FILE* const in, const uint32_t size, FILE* const out, const int32_t level) noexcept -> int32_t;

  auto unzip(FILE* const in, FILE* const out, uint32_t& ilength) noexcept -> int32_t;
  auto unzip(const uint8_t* const in, const uint32_t ilength, write_buffer_t out, void* const ptr) noexcept -> int32_t;
};  // namespace gzip
