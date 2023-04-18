/* Gzip is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Gzip is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file LICENSE.
 * If not, see <https://www.gnu.org/licenses/>
 *
 * https://www.gnu.org/software/gzip/
 */
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "gzip.h"

namespace gzip {

  namespace {
    // prefix code
    std::array<uint16_t, 1u << BITS> prev;

    // hash head
#define head (&prev[WSIZE])

#define HASH_BITS 15

#define UNALIGNED_OK 0
#if (UNALIGNED_OK == 1)
    static_assert(2 == sizeof(uint16_t), "UNALIGNED_OK can only be set as sizeof(uint16_t)==2");
#endif  // UNALIGNED_OK

/* To save space (see unlzw.c), we overlay prev+head with tab_prefix and
 * window with tab_suffix. Check that we can do this:
 */
#if (WSIZE + WSIZE) > (1 << BITS)
#  error cannot overlay window with tab_suffix and prev with tab_prefix0
#endif
#if HASH_BITS > (BITS - 1)
#  error cannot overlay head with tab_prefix1
#endif

#define HASH_SIZE (1u << HASH_BITS)
#define HASH_MASK (HASH_SIZE - 1)
#define WMASK (WSIZE - 1)
    static_assert(ISPOWEROF2(HASH_SIZE), "HASH_SIZE must be powers of two");
    static_assert(ISPOWEROF2(WSIZE), "WSIZE must be powers of two");

#ifndef TOO_FAR
#  define TOO_FAR 4096
#endif
    /* Matches of length 3 are discarded if their distance exceeds TOO_FAR */

#ifndef RSYNC_WIN
#  define RSYNC_WIN 4096
#endif

    static_assert(RSYNC_WIN < MAX_DIST, "verify (RSYNC_WIN < MAX_DIST)");

#define RSYNC_SUM_MATCH(sum) ((sum) % RSYNC_WIN == 0)
    /* Whether window sum matches magic value */

    uint32_t window_size{WSIZE + WSIZE};
    /* window size, 2*WSIZE except for MMAP or BIG_MEM, where it is the
     * input file length plus MIN_LOOKAHEAD.
     */

    uint32_t ins_h{0}; /* hash index of string to be inserted */

#define H_SHIFT ((HASH_BITS + MIN_MATCH - 1) / MIN_MATCH)
    /* Number of bits by which ins_h and del_h must be shifted at each
     * input step. It must be such that after MIN_MATCH steps, the oldest
     * byte no longer takes part in the hash key, that is:
     *   H_SHIFT * MIN_MATCH >= HASH_BITS
     */

    uint32_t prev_length{0};
    /* Length of the best match at previous step. Matches not greater than this
     * are discarded. This is used in the lazy match evaluation.
     */
  };  // namespace

  uint32_t strstart{0}; /* start of string to insert */

  namespace {
    uint32_t match_start{0}; /* start of matching string */
    int32_t eofile{0};       /* flag set at end of input file */
    uint32_t lookahead{0};   /* number of valid bytes ahead in window */

    uint32_t max_chain_length{0};
    /* To speed up deflation, hash chains are never searched beyond this length.
     * A higher limit improves compression ratio but degrades the speed.
     */

    uint32_t max_lazy_match{0};
    /* Attempt to find a better match only when the current match is strictly
     * smaller than this value. This mechanism is used only for compression
     * levels >= 4.
     */

    uint32_t good_match{0};
    /* Use a faster search when the previous match is longer than this */

    uint32_t rsync_sum{0};       /* rolling sum of rsync window */
    uint32_t rsync_chunk_end{0}; /* next rsync sequence point */

    /* Values for max_lazy_match, good_match and max_chain_length, depending on
     * the desired pack level (0..9). The values given below have been tuned to
     * exclude worst case performance for pathological files. Better values may be
     * found for specific files.
     */

    struct config {
      uint16_t good_length; /* reduce lazy search above this match length */
      uint16_t max_lazy;    /* do not perform lazy search above this match length */
      uint16_t nice_length; /* quit search above this match length */
      uint16_t max_chain;
    };

#ifdef FULL_SEARCH
#  define nice_match MAX_MATCH
#else
    /* Stop searching when current match exceeds this */
    uint32_t nice_match;
#endif

    constexpr std::array<config, 10> configuration_table{{{0, 0, 0, 0},            // 0 Store only
                                                          {4, 4, 8, 4},            // 1 Maximum speed, no lazy matches
                                                          {4, 5, 16, 8},           // 2
                                                          {4, 6, 32, 32},          // 3
                                                          {4, 4, 16, 16},          // 4 Lazy matches
                                                          {8, 16, 32, 32},         // 5
                                                          {8, 16, 128, 128},       // 6
                                                          {8, 32, 128, 256},       // 7
                                                          {32, 128, 258, 1024},    // 8
                                                          {32, 258, 258, 4096}}};  // 9 Maximum compression

  };  // namespace

  int32_t block_start{0};
  /* window position at the beginning of the current output block. Gets
   * negative when the window is moved backwards.
   */

  namespace {
/* Note: the Deflate() code requires max_lazy >= MIN_MATCH and max_chain >= 4
 * For DeflateFast() (levels <= 3) good is ignored and lazy has a different
 * meaning.
 */

/* ===========================================================================
 * Update a hash value with the given input byte
 * IN  assertion: all calls to UPDATE_HASH are made with consecutive
 *    input characters, so that a running hash key can be computed from the
 *    previous key instead of complete recalculation each time.
 */
#define UPDATE_HASH(h, c) (h = (((h) << H_SHIFT) ^ (c)) & HASH_MASK)

/* ===========================================================================
 * Insert string s in the dictionary and set match_head to the previous head
 * of the hash chain (the most recent string with same hash key). Return
 * the previous length of the hash chain.
 * IN  assertion: all calls to INSERT_STRING are made with consecutive
 *    input characters and the first MIN_MATCH bytes of s are valid
 *    (except for the last MIN_MATCH-1 bytes of the input file).
 */
#define INSERT_STRING(s, match_head)               \
  UPDATE_HASH(ins_h, window[(s) + MIN_MATCH - 1]); \
  prev[(s)&WMASK] = match_head = head[ins_h];      \
  head[ins_h] = (s)

    /* ===========================================================================
     * Fill the window when the lookahead becomes insufficient.
     * Updates strstart and lookahead, and sets eofile if end of input file.
     * IN assertion: lookahead < MIN_LOOKAHEAD && strstart + lookahead > 0
     * OUT assertions: at least one byte has been read, or eofile is set;
     *    file reads are performed for at least two bytes (required for the
     *    translate_eol option).
     */
    void FillWindow() noexcept {
      uint32_t more = window_size - lookahead - strstart;
      /* Amount of free space at the end of the window. */

      /* If the window is almost full and there is insufficient lookahead,
       * move the upper half to the lower one to make room in the upper half.
       */
      if (more == uint32_t(EOF)) {
        /* Very unlikely, but possible on 16 bit machine if strstart == 0
         * and lookahead == 1 (input done one byte at time)
         */
        more--;
      } else if (strstart >= WSIZE + MAX_DIST) {
        /* By the IN assertion, the window is not empty so we can't confuse
         * more == 0 with more == 64K on a 16 bit machine.
         */
        Assert(window_size == (WSIZE + WSIZE), "no sliding with BIG_MEM");

        memcpy(&window[0], &window[WSIZE], WSIZE);
        match_start -= WSIZE;
        strstart -= WSIZE; /* we now have strstart >= MAX_DIST: */
        if (UINT32_C(~0) != rsync_chunk_end) {
          rsync_chunk_end -= WSIZE;
        }

        block_start -= WSIZE;

        for (uint32_t n = 0; n < HASH_SIZE; n++) {
          const uint32_t m = head[n];
          head[n] = uint16_t(m >= WSIZE ? m - WSIZE : 0);
        }
        for (uint32_t n = 0; n < WSIZE; n++) {
          const uint32_t m = prev[n];
          prev[n] = uint16_t(m >= WSIZE ? m - WSIZE : 0);
          /* If n is not on any hash chain, prev[n] is garbage but
           * its value will never be used.
           */
        }
        more += WSIZE;
      }
      /* At this point, more >= 2 */
      if (!eofile) {
        const auto n{FileRead(&window[strstart + lookahead], more)};
        if ((0 == n) || (EOF == n)) {
          eofile = 1;
          /* Don't let garbage pollute the dictionary.  */
          memset(&window[strstart + lookahead], 0, MIN_MATCH - 1);
        } else {
          lookahead += static_cast<uint32_t>(n);
        }
      }
    }

    /* ===========================================================================
     * Initialise the "longest match" routines for a new file
     * PACK_LEVEL values: 0: store, 1: best speed, 9: best compression
     */
    void LongestMatchInit(uint32_t pack_level) noexcept {
      if (pack_level < 1 || pack_level > 9) {
        return;  // gzip_error("bad pack level");
      }

      /* Initialise the hash table. */
      prev.fill(0);

      /* rsync params */
      rsync_chunk_end = ~0U;
      rsync_sum = 0;

      /* Set the default configuration parameters:
       */
      max_lazy_match = configuration_table[pack_level].max_lazy;
      good_match = configuration_table[pack_level].good_length;
#ifndef FULL_SEARCH
      nice_match = configuration_table[pack_level].nice_length;
#endif
      max_chain_length = configuration_table[pack_level].max_chain;
      /* ??? reduce max_chain_length for binary files */

      strstart = 0;
      block_start = 0L;

      const auto n{FileRead(&window[0], sizeof(int32_t) <= 2 ? WSIZE : (WSIZE + WSIZE))};
      if ((0 == n) || (EOF == n)) {
        eofile = 1;
        lookahead = 0;
        return;
      }
      lookahead = static_cast<uint32_t>(n);

      eofile = 0;
      /* Make sure that we always have enough lookahead. This is important
       * if input comes from a device such as a tty.
       */
      while (lookahead < MIN_LOOKAHEAD && !eofile) {
        FillWindow();
      }

      ins_h = 0;
      for (uint32_t j = 0; j < MIN_MATCH - 1; j++) {
        UPDATE_HASH(ins_h, window[j]);
      }
      /* If lookahead < MIN_MATCH, ins_h is garbage, but this is
       * not important since only literal bytes will be emitted.
       */
    }

    /* ===========================================================================
     * Set match_start to the longest match starting at the given string and
     * return its length. Matches shorter or equal to prev_length are discarded,
     * in which case the result is equal to prev_length and match_start is
     * garbage.
     * IN assertions: cur_match is the head of the hash chain for the current
     *   string (strstart) and its distance is <= MAX_DIST, and prev_length >= 1
     */
    /* For MSDOS, OS/2 and 386 Unix, an optimised version is in match.asm or
     * match.s. The code is functionally equivalent, so you can use the C version
     * if desired.
     */
    auto LongestMatch(uint32_t cur_match) noexcept -> uint32_t {
      uint32_t chain_length = max_chain_length; /* max hash chain length */
      uint8_t* scan = &window[strstart];        /* current string */
      uint8_t* match;                           /* matched string */
      uint32_t len;                             /* length of current match */
      uint32_t best_len = prev_length;          /* best match length so far */
      const uint32_t limit{strstart > uint32_t(MAX_DIST) ? strstart - uint32_t(MAX_DIST) : 0};
      /* Stop when cur_match becomes <= limit. To simplify the code,
       * we prevent matches with the string of window index 0.
       */

/* The code is optimised for HASH_BITS >= 8 and MAX_MATCH-2 multiple of 16.
 * It is easy to get rid of this optimisation if necessary.
 */
#if HASH_BITS < 8 || MAX_MATCH != 258
#  error Code too clever
#endif

#if (UNALIGNED_OK == 1)
      /* Compare two bytes at a time. Note: this is not always beneficial.
       * Try with and without -DUNALIGNED_OK to check.
       */
      uint8_t* strend = &window[strstart + MAX_MATCH - 1];
      uint16_t scan_start = *reinterpret_cast<uint16_t*>(scan);
      uint16_t scan_end = *reinterpret_cast<uint16_t*>(scan + best_len - 1);
#else
      uint8_t* strend = &window[strstart + MAX_MATCH];
      uint8_t scan_end1 = scan[best_len - 1];
      uint8_t scan_end = scan[best_len];
#endif

      /* Do not waste too much time if we already have a good match: */
      if (prev_length >= good_match) {
        chain_length >>= 2;
      }
      Assert(strstart <= window_size - MIN_LOOKAHEAD, "insufficient lookahead");

      do {
        Assert(cur_match < strstart, "no future");
        match = &window[cur_match];

        /* Skip to next match if the match length cannot increase
         * or if the match length is less than 2:
         */
#if (UNALIGNED_OK == 1) && MAX_MATCH == 258
        /* This code assumes sizeof(uint16_t) == 2. Do not use
         * UNALIGNED_OK if your compiler uses a different size.
         */
        if (*reinterpret_cast<uint16_t*>(match + best_len - 1) != scan_end ||  //
            *reinterpret_cast<uint16_t*>(match) != scan_start) {
          continue;
        }

        /* It is not necessary to compare scan[2] and match[2] since they are
         * always equal when the other bytes match, given that the hash keys
         * are equal and that HASH_BITS >= 8. Compare 2 bytes at a time at
         * strstart+3, +5, ... up to strstart+257. We check for insufficient
         * lookahead only every 4th comparison; the 128th check will be made
         * at strstart+257. If MAX_MATCH-2 is not a multiple of 8, it is
         * necessary to put more guard bytes at the end of the window, or
         * to check more often for insufficient lookahead.
         */
        scan++;
        match++;
        do {
        } while (*reinterpret_cast<uint16_t*>(scan += 2) == *reinterpret_cast<uint16_t*>(match += 2) &&  //
                 *reinterpret_cast<uint16_t*>(scan += 2) == *reinterpret_cast<uint16_t*>(match += 2) &&  //
                 *reinterpret_cast<uint16_t*>(scan += 2) == *reinterpret_cast<uint16_t*>(match += 2) &&  //
                 *reinterpret_cast<uint16_t*>(scan += 2) == *reinterpret_cast<uint16_t*>(match += 2) && scan < strend);
        /* The funny "do {}" generates better code on most compilers */

        /* Here, scan <= window+strstart+257 */
        Assert(scan <= &window[window_size - 1], "wild scan");
        if (*scan == *match) {
          scan++;
        }

        len = (MAX_MATCH - 1) - int32_t(strend - scan);
        scan = strend - (MAX_MATCH - 1);
#else  /* UNALIGNED_OK */

        if (match[best_len] != scan_end || match[best_len - 1] != scan_end1 || *match != *scan || *++match != scan[1]) {
          continue;
        }

        /* The check at best_len-1 can be removed because it will be made
         * again later. (This heuristic is not always a win.)
         * It is not necessary to compare scan[2] and match[2] since they
         * are always equal when the other bytes match, given that
         * the hash keys are equal and that HASH_BITS >= 8.
         */
        scan += 2;
        match++;

        /* We check for insufficient lookahead only every 8th comparison;
         * the 256th check will be made at strstart+258.
         */
        do {
        } while (*++scan == *++match && *++scan == *++match &&  //
                 *++scan == *++match && *++scan == *++match &&  //
                 *++scan == *++match && *++scan == *++match &&  //
                 *++scan == *++match && *++scan == *++match && scan < strend);

        len = uint32_t(MAX_MATCH - int32_t(strend - scan));
        scan = strend - MAX_MATCH;
#endif /* UNALIGNED_OK */

        if (len > best_len) {
          match_start = cur_match;
          best_len = len;
          if (len >= nice_match) {
            break;
          }
#if (UNALIGNED_OK == 1)
          scan_end = *reinterpret_cast<uint16_t*>(scan + best_len - 1);
#else
          scan_end1 = scan[best_len - 1];
          scan_end = scan[best_len];
#endif
        }
      } while ((cur_match = prev[cur_match & WMASK]) > limit && --chain_length != 0);

      return best_len;
    }

#ifndef NDEBUG
    /* ===========================================================================
     * Check that the match at match_start is indeed a match.
     */
    void CheckMatch(uint32_t start, uint32_t match, uint32_t length) noexcept {
      /* check that the match is indeed a match */
      if (memcmp(&window[match], &window[start], length) != 0) {
        fprintf(stderr, " start %u, match %u, length %u\n", start, match, length);
        assert(1);  // gzip_error("invalid match");
      }
    }
#else
#  define CheckMatch(start, match, length)
#endif

    /* With an initial offset of START, advance rsync's rolling checksum
       by NUM bytes.  */
    void RsyncRoll(uint32_t start, uint32_t num) noexcept {
      if (start < RSYNC_WIN) {
        /* before window fills. */
        for (uint32_t i = start; i < RSYNC_WIN; i++) {
          if (i == start + num) {
            return;
          }
          rsync_sum += uint32_t(window[i]);
        }
        num -= (RSYNC_WIN - start);
        start = RSYNC_WIN;
      }

      /* buffer after window full */
      for (uint32_t i = start; i < start + num; i++) {
        /* New character in */
        rsync_sum += uint32_t(window[i]);
        /* Old character out */
        rsync_sum -= uint32_t(window[i - RSYNC_WIN]);
        if (UINT32_C(~0) == rsync_chunk_end && RSYNC_SUM_MATCH(rsync_sum)) {
          rsync_chunk_end = i;
        }
      }
    }

/* ===========================================================================
 * Set rsync_chunk_end if window sum matches magic value.
 */
#define RSYNC_ROLL(s, n) \
  if (rsync) {           \
    RsyncRoll((s), (n)); \
  }

/* ===========================================================================
 * Flush the current block, with given end-of-file flag.
 * IN assertion: strstart is set to the end of the current match.
 */
#define FLUSH_BLOCK(eof) FlushBlock(block_start >= 0L ? reinterpret_cast<char*>(&window[uint32_t(block_start)]) : nullptr, int32_t(strstart) - block_start, flush - 1, (eof))

    /* ===========================================================================
     * Processes a new input file and return its compressed length. This
     * function does not perform lazy evaluationof matches and inserts
     * new strings in the dictionary only for unmatched strings or for short
     * matches. It is used only for the fast compression options.
     */
    auto DeflateFast() noexcept -> int32_t {
      uint32_t hash_head;        /* head of the hash chain */
      int32_t flush = 0;         /* set if current block must be flushed, 2=>and padded  */
      uint32_t match_length = 0; /* length of best match */

      prev_length = MIN_MATCH - 1;
      while (lookahead != 0) {
        /* Insert the string window[strstart .. strstart+2] in the
         * dictionary, and set hash_head to the head of the hash chain:
         */
        INSERT_STRING(strstart, hash_head);

        /* Find the longest match, discarding those <= prev_length.
         * At this point we have always match_length < MIN_MATCH
         */
        if (hash_head != 0 && strstart - hash_head <= MAX_DIST && strstart <= window_size - MIN_LOOKAHEAD) {
          /* To simplify the code, we prevent matches with the string
           * of window index 0 (in particular we have to avoid a match
           * of the string with itself at the start of the input file).
           */
          match_length = LongestMatch(hash_head);
          /* longest_match() sets match_start */
          if (match_length > lookahead) {
            match_length = lookahead;
          }
        }
        if (match_length >= MIN_MATCH) {
          CheckMatch(strstart, match_start, match_length);

          flush = CtTally(strstart - match_start, match_length - MIN_MATCH);

          lookahead -= match_length;

          RSYNC_ROLL(strstart, match_length)
          /* Insert new strings in the hash table only if the match length
           * is not too large. This saves time but degrades compression.
           */
          if (match_length <= max_lazy_match) {
            match_length--; /* string at strstart already in hash table */
            do {
              strstart++;
              INSERT_STRING(strstart, hash_head);
              /* strstart never exceeds WSIZE-MAX_MATCH, so there are
               * always MIN_MATCH bytes ahead. If lookahead < MIN_MATCH
               * these bytes are garbage, but it does not matter since
               * the next lookahead bytes will be emitted as literals.
               */
            } while (--match_length != 0);
            strstart++;
          } else {
            strstart += match_length;
            match_length = 0;
            ins_h = window[strstart];
            UPDATE_HASH(ins_h, window[strstart + 1]);
#if MIN_MATCH != 3
            Call UPDATE_HASH() MIN_MATCH - 3 more times
#endif
          }
        } else {
          /* No match, output a literal byte */
#if 0
        Tracevv((stderr, "%c", window[strstart]));
#endif
          flush = CtTally(0, window[strstart]);
          RSYNC_ROLL(strstart, 1)
          lookahead--;
          strstart++;
        }
        if (rsync && strstart > rsync_chunk_end) {
          rsync_chunk_end = UINT32_C(~0);
          flush = 2;
        }
        if (flush) {
          FLUSH_BLOCK(0);
          block_start = int32_t(strstart);
        }

        /* Make sure that we always have enough lookahead, except
         * at the end of the input file. We need MAX_MATCH bytes
         * for the next match, plus MIN_MATCH bytes to insert the
         * string following the next match.
         */
        while (lookahead < MIN_LOOKAHEAD && !eofile) {
          FillWindow();
        }
      }
      return FLUSH_BLOCK(1); /* eof */
    }
  };  // namespace

  /* ===========================================================================
   * Same as above, but achieves better compression. We use a lazy
   * evaluation for matches: a match is finally adopted only if there is
   * no better match at the next window position.
   */
  auto Deflate(const uint32_t pack_level) noexcept -> int32_t {
    uint32_t hash_head;                    /* head of hash chain */
    uint32_t prev_match;                   /* previous match */
    int32_t flush = 0;                     /* set if current block must be flushed */
    int32_t match_available = 0;           /* set if previous match exists */
    uint32_t match_length = MIN_MATCH - 1; /* length of best match */

    LongestMatchInit(pack_level);
    if (pack_level <= 3) {
      return DeflateFast();
    }

    /* Process the input block. */
    while (lookahead != 0) {
      /* Insert the string window[strstart .. strstart+2] in the
       * dictionary, and set hash_head to the head of the hash chain:
       */
      INSERT_STRING(strstart, hash_head);

      /* Find the longest match, discarding those <= prev_length.
       */
      prev_length = match_length;
      prev_match = match_start;
      match_length = MIN_MATCH - 1;

      if (hash_head != 0 && prev_length < max_lazy_match && strstart - hash_head <= MAX_DIST && strstart <= window_size - MIN_LOOKAHEAD) {
        /* To simplify the code, we prevent matches with the string
         * of window index 0 (in particular we have to avoid a match
         * of the string with itself at the start of the input file).
         */
        match_length = LongestMatch(hash_head);
        /* longest_match() sets match_start */
        if (match_length > lookahead) {
          match_length = lookahead;
        }

        /* Ignore a length 3 match if it is too distant: */
        if (match_length == MIN_MATCH && strstart - match_start > TOO_FAR) {
          /* If prev_match is also MIN_MATCH, match_start is garbage
           * but we will ignore the current match anyway.
           */
          match_length--;
        }
      }
      /* If there was a match at the previous step and the current
       * match is not better, output the previous match:
       */
      if (prev_length >= MIN_MATCH && match_length <= prev_length) {
        CheckMatch(strstart - 1, prev_match, prev_length);

        flush = CtTally(strstart - 1 - prev_match, prev_length - MIN_MATCH);

        /* Insert in hash table all strings up to the end of the match.
         * strstart-1 and strstart are already inserted.
         */
        lookahead -= prev_length - 1;
        prev_length -= 2;
        RSYNC_ROLL(strstart, prev_length + 1)
        do {
          strstart++;
          INSERT_STRING(strstart, hash_head);
          /* strstart never exceeds WSIZE-MAX_MATCH, so there are
           * always MIN_MATCH bytes ahead. If lookahead < MIN_MATCH
           * these bytes are garbage, but it does not matter since the
           * next lookahead bytes will always be emitted as literals.
           */
        } while (--prev_length != 0);
        match_available = 0;
        match_length = MIN_MATCH - 1;
        strstart++;

        if (rsync && strstart > rsync_chunk_end) {
          rsync_chunk_end = UINT32_C(~0);
          flush = 2;
        }
        if (flush) {
          FLUSH_BLOCK(0);
          block_start = strstart;
        }
      } else if (match_available) {
        /* If there was no match at the previous position, output a
         * single literal. If there was a match but the current match
         * is longer, truncate the previous match to a single literal.
         */
#if 0
        Tracevv((stderr, "%c", window[strstart - 1]));
#endif
        flush = CtTally(0, window[strstart - 1]);
        if (rsync && strstart > rsync_chunk_end) {
          rsync_chunk_end = UINT32_C(~0);
          flush = 2;
        }
        if (flush) {
          FLUSH_BLOCK(0);
          block_start = int32_t(strstart);
        }
        RSYNC_ROLL(strstart, 1)
        strstart++;
        lookahead--;
      } else {
        /* There is no previous match to compare with, wait for
         * the next step to decide.
         */
        if (rsync && strstart > rsync_chunk_end) {
          /* Reset huffman tree */
          rsync_chunk_end = UINT32_C(~0);
          flush = 2;
          FLUSH_BLOCK(0);
          block_start = int32_t(strstart);
        }

        match_available = 1;
        RSYNC_ROLL(strstart, 1)
        strstart++;
        lookahead--;
      }
      Assert(strstart <= bytes_in && lookahead <= bytes_in, "a bit too far");

      /* Make sure that we always have enough lookahead, except
       * at the end of the input file. We need MAX_MATCH bytes
       * for the next match, plus MIN_MATCH bytes to insert the
       * string following the next match.
       */
      while (lookahead < MIN_LOOKAHEAD && !eofile) {
        FillWindow();
      }
    }
    if (match_available) {
      CtTally(0, window[strstart - 1]);
    }

    return FLUSH_BLOCK(1); /* eof */
  }
};  // namespace gzip
