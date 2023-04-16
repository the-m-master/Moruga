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
#include <algorithm>
#include <array>
#include <cstdint>
#include "gzip.h"

namespace gzip {
  namespace {
#define MAX_BITS 15
    /* All codes must not exceed MAX_BITS bits */

#define MAX_BL_BITS 7
    /* Bit length codes must not exceed MAX_BL_BITS bits */

#define LENGTH_CODES 29
    /* number of length codes, not counting the special END_BLOCK code */

#define LITERALS 256
    /* number of literal bytes 0..255 */

#define END_BLOCK 256
    /* end of block literal code */

#define L_CODES (LITERALS + 1 + LENGTH_CODES)
    /* number of Literal or Length codes, including the END_BLOCK code */

#define D_CODES 30
    /* number of distance codes */

#define BL_CODES 19
    /* number of codes used to transfer the bit lengths */

    /* extra bits for each length code */
    constexpr std::array<uint32_t, LENGTH_CODES> extra_lbits{{0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0}};

    /* extra bits for each distance code */
    constexpr std::array<uint32_t, D_CODES> extra_dbits{{0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13}};

    /* extra bits for each bit length code */
    constexpr std::array<uint32_t, BL_CODES> extra_blbits{{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 3, 7}};

#define STORED_BLOCK 0
#define STATIC_TREES 1
#define DYN_TREES 2
    /* The three kinds of block type */

#define LIT_BUFSIZE 0x8000
#ifndef DIST_BUFSIZE
#  define DIST_BUFSIZE LIT_BUFSIZE
#endif
/* Sizes of match buffers for literals/lengths and distances.  There are
 * 4 reasons for limiting LIT_BUFSIZE to 64K:
 *   - frequencies can be kept in 16 bit counters
 *   - if compression is not successful for the first block, all input data is
 *     still in the window so we can still emit a stored block even when input
 *     comes from standard input.  (This can also be done for all blocks if
 *     LIT_BUFSIZE is not greater than 32K.)
 *   - if compression is not successful for a file smaller than 64K, we can
 *     even emit a stored file instead of a stored block (saving 5 bytes).
 *   - creating new Huffman trees less frequently may not provide fast
 *     adaptation to changes in the input data statistics. (Take for
 *     example a binary file with poorly compressible code followed by
 *     a highly compressible string table.) Smaller buffer sizes give
 *     fast adaptation but have of course the overhead of transmitting trees
 *     more frequently.
 *   - I can't count above 4
 * The current code is general and allows DIST_BUFSIZE < LIT_BUFSIZE (to save
 * memory at the expense of compression). Some optimisations would be possible
 * if we rely on DIST_BUFSIZE == LIT_BUFSIZE.
 */
#if LIT_BUFSIZE > INBUFSIZ
#  error cannot overlay l_buf and inbuf
#endif

#define REP_3_6 16
    /* repeat previous bit length 3-6 times (2 bits of repeat count) */

#define REPZ_3_10 17
    /* repeat a zero length 3-10 times  (3 bits of repeat count) */

#define REPZ_11_138 18
    /* repeat a zero length 11-138 times  (7 bits of repeat count) */

    /* ===========================================================================
     * Local data
     */

    /* Data structure describing a single value and its code string. */
    struct ct_data {
      union {
        uint16_t freq; /* frequency count */
        uint16_t code; /* bit string */
      } fc;
      union {
        uint16_t dad; /* father node in Huffman tree */
        uint16_t len; /* length of bit string */
      } dl;
    };

#define HEAP_SIZE (2 * L_CODES + 1)
    /* maximum heap size */

    std::array<ct_data, HEAP_SIZE> dyn_ltree;       /* literal and length tree */
    std::array<ct_data, 2 * D_CODES + 1> dyn_dtree; /* distance tree */

    std::array<ct_data, L_CODES + 2> static_ltree;
    /* The static literal tree. Since the bit lengths are imposed, there is no
     * need for the L_CODES extra codes used during heap construction. However
     * The codes 286 and 287 are needed to build a canonical tree (see ct_init
     * below).
     */

    std::array<ct_data, D_CODES> static_dtree;
    /* The static distance tree. (Actually a trivial tree since all codes use
     * 5 bits.)
     */

    std::array<ct_data, 2 * BL_CODES + 1> bl_tree;
    /* Huffman tree for the bit lengths */

    struct tree_desc {
      ct_data* dyn_tree;           // the dynamic tree
      ct_data* static_tree;        // corresponding static tree or nullptr
      const uint32_t* extra_bits;  // extra bits for each code or nullptr
      int32_t extra_base;          // base index for extra_bits
      int32_t elems;               // max number of elements in the tree
      int32_t max_length;          // max bit length for the codes
      int32_t max_code;            // largest code with non zero frequency
    };

    tree_desc l_desc = {.dyn_tree = dyn_ltree.data(),  //
                        .static_tree = static_ltree.data(),
                        .extra_bits = extra_lbits.data(),
                        .extra_base = LITERALS + 1,
                        .elems = L_CODES,
                        .max_length = MAX_BITS,
                        .max_code = 0};

    tree_desc d_desc = {.dyn_tree = dyn_dtree.data(),  //
                        .static_tree = static_dtree.data(),
                        .extra_bits = extra_dbits.data(),
                        .extra_base = 0,
                        .elems = D_CODES,
                        .max_length = MAX_BITS,
                        .max_code = 0};

    tree_desc bl_desc = {.dyn_tree = bl_tree.data(),  //
                         .static_tree = nullptr,
                         .extra_bits = extra_blbits.data(),
                         .extra_base = 0,
                         .elems = BL_CODES,
                         .max_length = MAX_BL_BITS,
                         .max_code = 0};

    std::array<uint16_t, MAX_BITS + 1> bl_count;
    /* number of codes at each bit length for an optimal tree */

    constexpr std::array<uint8_t, BL_CODES> bl_order{{16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15}};
    /* The lengths of the bit length codes are sent in order of decreasing
     * probability, to avoid transmitting the lengths for unused bit length codes.
     */

    std::array<int32_t, 2 * L_CODES + 1> heap; /* heap used to build the Huffman trees */
    int32_t heap_len;                          /* number of elements in the heap */
    int32_t heap_max;                          /* element of largest frequency */
    /* The sons of heap[n] are heap[2*n] and heap[2*n+1]. heap[0] is not used.
     * The same heap array is used to build all trees.
     */

    std::array<uint8_t, 2 * L_CODES + 1> depth;
    /* Depth of each subtree used as tie breaker for trees of equal frequency */

    std::array<uint8_t, MAX_MATCH - MIN_MATCH + 1> length_code;
    /* length code for each normalised match length (0 == MIN_MATCH) */

    std::array<uint8_t, 512> dist_code;
    /* distance codes. The first 256 values correspond to the distances
     * 3 .. 258, the last 256 values correspond to the top 8 bits of
     * the 15 bit distances.
     */

    std::array<int32_t, LENGTH_CODES> base_length;
    /* First normalised length for each code (0 = MIN_MATCH) */

    std::array<int32_t, D_CODES> base_dist;
    /* First normalised distance for each code (0 = distance of 1) */

#define l_buf inbuf
    /* DECLARE(uint8_t, l_buf, LIT_BUFSIZE);  buffer for literals or lengths */

    std::array<uint16_t, DIST_BUFSIZE> d_buf; /* buffer for distances */

    std::array<uint8_t, LIT_BUFSIZE / 8> flag_buf;
    /* flag_buf is a bit array distinguishing literals from lengths in
     * l_buf, thus indicating the presence or absence of a distance.
     */

    uint32_t last_lit;   /* running index in l_buf */
    uint32_t last_dist;  /* running index in d_buf */
    uint32_t last_flags; /* running index in flag_buf */
    uint8_t flags;       /* current flags not yet saved in flag_buf */
    uint8_t flag_bit;    /* current bit used in flags */
    /* bits are filled in flags starting at bit 0 (least significant).
     * Note: these flags are overkill in the current code since we don't
     * take advantage of DIST_BUFSIZE == LIT_BUFSIZE.
     */

    uint32_t opt_len;    /* bit length of current block with optimal trees */
    uint32_t static_len; /* bit length of current block with static trees */

    int32_t compressed_len; /* total bit length of compressed file */

    uint16_t* file_type; /* pointer to UNKNOWN, BINARY or ASCII */

#define send_code(c, tree) SendBits(tree[c].fc.code, tree[c].dl.len)
    /* Send a code of the given tree. c and tree must not have side effects */

#define d_code(dist) ((dist) < 256 ? dist_code[dist] : dist_code[256 + ((dist) >> 7)])
    /* Mapping from a distance to a distance code. dist is the distance - 1 and
     * must not have side effects. dist_code[256] and dist_code[257] are never
     * used.
     */

    /* ===========================================================================
     * Initialise a new block.
     */
    void InitBlock() noexcept {
      /* Initialise the trees. */
      for (uint32_t n{0}; n < L_CODES; n++) {
        dyn_ltree[n].fc.freq = 0;
      }
      for (uint32_t n{0}; n < D_CODES; n++) {
        dyn_dtree[n].fc.freq = 0;
      }
      for (uint32_t n{0}; n < BL_CODES; n++) {
        bl_tree[n].fc.freq = 0;
      }

      dyn_ltree[END_BLOCK].fc.freq = 1;
      opt_len = static_len = 0L;
      last_lit = last_dist = last_flags = 0;
      flags = 0;
      flag_bit = 1;
    }

    /* ===========================================================================
     * Generate the codes for a given tree and bit counts (which need not be
     * optimal).
     * IN assertion: the array bl_count contains the bit length statistics for
     * the given tree and the field len is set for all tree elements.
     * OUT assertion: the field code is set for all tree elements of non
     *     zero code length.
     */
    void GenerateCodes(ct_data* tree, int32_t max_code) noexcept {
      std::array<uint16_t, MAX_BITS + 1> next_code; /* next code value for each bit length */
      uint16_t code{0};                             /* running code value */

      /* The distribution counts are first used to generate the code values
       * without bit reversal.
       */
      for (uint32_t bits{1}; bits <= MAX_BITS; bits++) {
        next_code[bits] = code = uint16_t((code + bl_count[bits - 1]) << 1);
      }
      /* Check that the bit counts in bl_count are consistent. The last code
       * must be all ones.
       */
      Assert(code + bl_count[MAX_BITS] - 1 == (1 << MAX_BITS) - 1, "inconsistent bit counts");
#if 0
      Tracev((stderr, "\ngen_codes: max_code %d ", max_code));
#endif

      for (uint32_t n{0}; n <= uint32_t(max_code); ++n) {
        const auto len{tree[n].dl.len};
        if (len == 0) {
          continue;
        }
        /* Now reverse the bits */
        tree[n].fc.code = BitsReverse(next_code[len]++, len);

#if 0
        Tracec(tree != static_ltree, (stderr, "\nn %3d %c l %2d c %4x (%x) ", n, (isgraph(n) ? n : ' '), len, tree[n].fc.code, next_code[len] - 1));
#endif
      }
    }
  };  // namespace

  /* ===========================================================================
   * Allocate the match buffer, initialise the various tables and save the
   * location of the internal file attribute (ASCII/binary) and method
   * (DEFLATE/STORE).
   */
  void CtInit(uint16_t* attr) noexcept {
    file_type = attr;
    compressed_len = 0L;

    if (static_dtree[0].dl.len != 0) {
      return; /* ct_init already called */
    }

    /* Initialise the mapping length (0..255) -> length code (0..28) */
    uint32_t length{0}; /* length value */
    uint32_t code;      /* code value */
    for (code = 0; code < LENGTH_CODES - 1; code++) {
      base_length[code] = length;
      for (uint32_t n{0}; n < (1u << extra_lbits[code]); n++) {
        length_code[length++] = uint8_t(code);
      }
    }
    Assert(length == 256, "ct_init: length != 256");
    /* Note that the length 255 (match length 258) can be represented
     * in two different ways: code 284 + 5 bits or code 285, so we
     * overwrite length_code[255] to use the best encoding:
     */
    length_code[length - 1] = uint8_t(code);

    /* Initialise the mapping dist (0..32K) -> dist code (0..29) */
    uint32_t dist{0}; /* distance index */
    for (code = 0; code < 16; code++) {
      base_dist[code] = dist;
      for (uint32_t n{0}; n < (1u << extra_dbits[code]); n++) {
        dist_code[dist++] = uint8_t(code);
      }
    }
    Assert(dist == 256, "ct_init: dist != 256");
    dist >>= 7; /* from now on, all distances are divided by 128 */
    for (; code < D_CODES; code++) {
      base_dist[code] = dist << 7;
      for (uint32_t n{0}; n < (1u << (extra_dbits[code] - 7)); n++) {
        dist_code[256 + dist++] = uint8_t(code);
      }
    }
    Assert(dist == 256, "ct_init: 256+dist != 512");

    /* Construct the codes of the static literal tree */
    for (uint32_t bits = 0; bits <= MAX_BITS; bits++) {
      bl_count[bits] = 0;
    }
    {
      int32_t n{0};
      while (n <= 143) {
        static_ltree[n++].dl.len = 8;
        bl_count[8]++;
      }
      while (n <= 255) {
        static_ltree[n++].dl.len = 9;
        bl_count[9]++;
      }
      while (n <= 279) {
        static_ltree[n++].dl.len = 7;
        bl_count[7]++;
      }
      while (n <= 287) {
        static_ltree[n++].dl.len = 8;
        bl_count[8]++;
      }
    }
    /* Codes 286 and 287 do not exist, but we must include them in the
     * tree construction to get a canonical Huffman tree (longest code
     * all ones)
     */
    GenerateCodes(static_ltree.data(), L_CODES + 1);

    /* The static distance tree is trivial: */
    for (uint32_t n{0}; n < D_CODES; n++) {
      static_dtree[n].dl.len = 5;
      static_dtree[n].fc.code = BitsReverse(n, 5);
    }

    /* Initialise the first block of the first file: */
    InitBlock();
  }

  namespace {
#define SMALLEST 1
/* Index within the heap array of least frequent node in the Huffman tree */

/* ===========================================================================
 * Remove the smallest element from the heap and recreate the heap with
 * one less element. Updates heap and heap_len.
 */
#define pqremove(tree, top)          \
  top = heap[SMALLEST];              \
  heap[SMALLEST] = heap[heap_len--]; \
  PqDownHeap(tree, SMALLEST)

/* ===========================================================================
 * Compares to subtrees, using the tree depth as tie breaker when
 * the subtrees have equal frequency. This minimises the worst case length.
 */
#define smaller(tree, n, m) (tree[n].fc.freq < tree[m].fc.freq || (tree[n].fc.freq == tree[m].fc.freq && depth[n] <= depth[m]))

    /* ===========================================================================
     * Restore the heap property by moving down the tree starting at node k,
     * exchanging a node with the smallest of its two sons if necessary, stopping
     * when the heap property is re-established (each father smaller than its
     * two sons).
     */
    void PqDownHeap(ct_data* tree, int32_t k) noexcept {
      const int32_t v = heap[k];
      int32_t j = k << 1; /* left son of k */
      while (j <= heap_len) {
        /* Set j to the smallest of the two sons: */
        if (j < heap_len && smaller(tree, heap[j + 1], heap[j])) {
          j++;
        }

        /* Exit if v is smaller than both sons */
        if (smaller(tree, v, heap[j])) {
          break;
        }

        /* Exchange v with the smallest son */
        heap[k] = heap[j];
        k = j;

        /* And continue down the tree, setting j to the left son of k */
        j <<= 1;
      }
      heap[k] = v;
    }

    /* ===========================================================================
     * Compute the optimal bit lengths for a tree and update the total bit length
     * for the current block.
     * IN assertion: the fields freq and dad are set, heap[heap_max] and
     *    above are the tree nodes sorted by increasing frequency.
     * OUT assertions: the field len is set to the optimal bit length, the
     *     array bl_count contains the frequencies for each bit length.
     *     The length opt_len is updated; static_len is also updated if stree is
     *     not null.
     */
    void GenerateBitLengths(tree_desc* desc) noexcept {
      ct_data* tree = desc->dyn_tree;
      const uint32_t* extra = desc->extra_bits;
      const int32_t base = desc->extra_base;
      const int32_t max_code = desc->max_code;
      const int32_t max_length = desc->max_length;
      ct_data* stree = desc->static_tree;
      int32_t h;            // heap index
      int32_t n, m;         // iterate over the tree elements
      int32_t xbits;        // extra bits
      uint16_t f;           // frequency
      int32_t overflow{0};  // number of elements with bit length too large

      for (int32_t bits{0}; bits <= MAX_BITS; bits++) {
        bl_count[bits] = 0;
      }

      /* In a first pass, compute the optimal bit lengths (which may
       * overflow in the case of the bit length tree).
       */
      tree[heap[heap_max]].dl.len = 0; /* root of the heap */

      for (h = heap_max + 1; h < HEAP_SIZE; h++) {
        n = heap[h];
        int32_t bits = tree[tree[n].dl.dad].dl.len + 1;
        if (bits > max_length) {
          bits = max_length;
          overflow++;
        }
        tree[n].dl.len = uint16_t(bits);
        /* We overwrite tree[n].Dad which is no longer needed */

        if (n > max_code) {
          continue; /* not a leaf node */
        }
        bl_count[bits]++;
        xbits = 0;
        if (n >= base) {
          xbits = extra[n - base];
        }
        f = tree[n].fc.freq;
        opt_len += uint32_t(f) * uint32_t(bits + xbits);
        if (stree) {
          static_len += uint32_t(f) * uint32_t(stree[n].dl.len + xbits);
        }
      }
      if (overflow == 0) {
        return;
      }

#if 0
      Trace((stderr, "\nbit length overflow\n"));
#endif
      /* This happens for example on obj2 and pic of the Calgary corpus */

      /* Find the first bit length which could increase: */
      do {
        int32_t bits = max_length - 1;
        while (bl_count[bits] == 0) {
          bits--;
        }
        bl_count[bits]--;        /* move one leaf down the tree */
        bl_count[bits + 1] += 2; /* move one overflow item as its brother */
        bl_count[max_length]--;
        /* The brother of the overflow item also moves one step up,
         * but this does not affect bl_count[max_length]
         */
        overflow -= 2;
      } while (overflow > 0);

      /* Now recompute all bit lengths, scanning in increasing frequency.
       * h is still equal to HEAP_SIZE. (It is simpler to reconstruct all
       * lengths instead of fixing only the wrong ones. This idea is taken
       * from 'ar' written by Haruhiko Okumura.)
       */
      for (int32_t bits = max_length; bits != 0; bits--) {
        n = bl_count[bits];
        while (n != 0) {
          m = heap[--h];
          if (m > max_code) {
            continue;
          }
          if (tree[m].dl.len != uint32_t(bits)) {
#if 0
            Trace((stderr, "code %d bits %d->%d\n", m, tree[m].dl.len, bits));
#endif
            opt_len += (bits - int32_t(tree[m].dl.len)) * int32_t(tree[m].fc.freq);
            tree[m].dl.len = uint16_t(bits);
          }
          n--;
        }
      }
    }

    /* ===========================================================================
     * Construct one Huffman tree and assigns the code bit strings and lengths.
     * Update the total bit length for the current block.
     * IN assertion: the field freq is set for all tree elements.
     * OUT assertions: the fields len and code are set to the optimal bit length
     *     and corresponding code. The length opt_len is updated; static_len is
     *     also updated if stree is not null. The field max_code is set.
     */
    void BuildTree(tree_desc* desc) noexcept {
      ct_data* tree = desc->dyn_tree;
      ct_data* stree = desc->static_tree;
      const int32_t elems = desc->elems;
      int32_t n, m;          /* iterate over heap elements */
      int32_t max_code = -1; /* largest code with non zero frequency */
      int32_t node = elems;  /* next internal node of the tree */

      /* Construct the initial heap, with least frequent element in
       * heap[SMALLEST]. The sons of heap[n] are heap[2*n] and heap[2*n+1].
       * heap[0] is not used.
       */
      heap_len = 0;
      heap_max = HEAP_SIZE;

      for (n = 0; n < elems; n++) {
        if (tree[n].fc.freq != 0) {
          heap[++heap_len] = max_code = n;
          depth[n] = 0;
        } else {
          tree[n].dl.len = 0;
        }
      }

      /* The pkzip format requires that at least one distance code exists,
       * and that at least one bit should be sent even if there is only one
       * possible code. So to avoid special checks later on we force at least
       * two codes of non zero frequency.
       */
      while (heap_len < 2) {
        const int32_t new_ = heap[++heap_len] = (max_code < 2 ? ++max_code : 0);
        tree[new_].fc.freq = 1;
        depth[new_] = 0;
        opt_len--;
        if (stree) {
          static_len -= stree[new_].dl.len;
        }
        /* new is 0 or 1 so it does not have extra bits */
      }
      desc->max_code = max_code;

      /* The elements heap[heap_len/2+1 .. heap_len] are leaves of the tree,
       * establish sub-heaps of increasing lengths:
       */
      for (n = heap_len / 2; n >= 1; n--) {
        PqDownHeap(tree, n);
      }

      /* Construct the Huffman tree by repeatedly combining the least two
       * frequent nodes.
       */
      do {
        pqremove(tree, n);  /* n = node of least frequency */
        m = heap[SMALLEST]; /* m = node of next least frequency */

        heap[--heap_max] = n; /* keep the nodes sorted by frequency */
        heap[--heap_max] = m;

        /* Create a new node father of n and m */
        tree[node].fc.freq = tree[n].fc.freq + tree[m].fc.freq;
        depth[node] = uint8_t(std::max(depth[n], depth[m]) + 1);
        tree[n].dl.dad = tree[m].dl.dad = uint16_t(node);
#ifdef DUMP_BL_TREE
        if (tree == bl_tree) {
          fprintf(stderr, "\nnode %d(%d), sons %d(%d) %d(%d)", node, tree[node].fc.freq, n, tree[n].fc.freq, m, tree[m].fc.freq);
        }
#endif
        /* and insert the new node in the heap */
        heap[SMALLEST] = node++;
        PqDownHeap(tree, SMALLEST);
      } while (heap_len >= 2);

      heap[--heap_max] = heap[SMALLEST];

      /* At this point, the fields freq and dad are set. We can now
       * generate the bit lengths.
       */
      GenerateBitLengths(desc);

      /* The field len is now set, we can generate the bit codes */
      GenerateCodes(tree, max_code);
    }

    /* ===========================================================================
     * Scan a literal or distance tree to determine the frequencies of the codes
     * in the bit length tree. Updates opt_len to take into account the repeat
     * counts. (The contribution of the bit length codes will be added later
     * during the construction of bl_tree.)
     */
    void ScanTree(ct_data* tree, int32_t max_code) noexcept {
      int32_t prevlen = -1;             /* last emitted length */
      int32_t nextlen = tree[0].dl.len; /* length of next code */
      int32_t max_count = 7;            /* max repeat count */
      int32_t min_count = 4;            /* min repeat count */

      if (nextlen == 0) {
        max_count = 138;
        min_count = 3;
      }
      tree[max_code + 1].dl.len = 0xFFFF; /* guard */

      int32_t count = 0; /* repeat count of the current code */
      for (int32_t n = 0; n <= max_code; n++) {
        const int32_t curlen = nextlen; /* length of current code */
        nextlen = tree[n + 1].dl.len;
        if (++count < max_count && curlen == nextlen) {
          continue;
        }
        if (count < min_count) {
          bl_tree[curlen].fc.freq += count;
        } else if (curlen != 0) {
          if (curlen != prevlen) {
            bl_tree[curlen].fc.freq++;
          }
          bl_tree[REP_3_6].fc.freq++;
        } else if (count <= 10) {
          bl_tree[REPZ_3_10].fc.freq++;
        } else {
          bl_tree[REPZ_11_138].fc.freq++;
        }
        count = 0;
        prevlen = curlen;
        if (nextlen == 0) {
          max_count = 138;
          min_count = 3;
        } else if (curlen == nextlen) {
          max_count = 6;
          min_count = 3;
        } else {
          max_count = 7;
          min_count = 4;
        }
      }
    }

    /* ===========================================================================
     * Send a literal or distance tree in compressed form, using the codes in
     * bl_tree.
     */
    void SendTree(ct_data* tree, int32_t max_code) noexcept {
      int32_t prevlen = -1;             /* last emitted length */
      int32_t nextlen = tree[0].dl.len; /* length of next code */
      int32_t max_count = 7;            /* max repeat count */
      int32_t min_count = 4;            /* min repeat count */

      /* tree[max_code+1].dl.len = -1; */ /* guard already set */
      if (nextlen == 0) {
        max_count = 138;
        min_count = 3;
      }

      int32_t count = 0; /* repeat count of the current code */
      for (int32_t n = 0; n <= max_code; n++) {
        const int32_t curlen = nextlen; /* length of current code */
        nextlen = tree[n + 1].dl.len;
        if (++count < max_count && curlen == nextlen) {
          continue;
        }
        if (count < min_count) {
          do {
            send_code(curlen, bl_tree);
          } while (--count != 0);
        } else if (curlen != 0) {
          if (curlen != prevlen) {
            send_code(curlen, bl_tree);
            count--;
          }
          Assert(count >= 3 && count <= 6, " 3_6?");
          send_code(REP_3_6, bl_tree);
          SendBits(count - 3, 2);
        } else if (count <= 10) {
          send_code(REPZ_3_10, bl_tree);
          SendBits(count - 3, 3);
        } else {
          send_code(REPZ_11_138, bl_tree);
          SendBits(count - 11, 7);
        }
        count = 0;
        prevlen = curlen;
        if (nextlen == 0) {
          max_count = 138;
          min_count = 3;
        } else if (curlen == nextlen) {
          max_count = 6;
          min_count = 3;
        } else {
          max_count = 7;
          min_count = 4;
        }
      }
    }

    /* ===========================================================================
     * Construct the Huffman tree for the bit lengths and return the index in
     * bl_order of the last bit length code to send.
     */
    auto BuildBitLengthTree() noexcept -> int32_t {
      int32_t max_blindex; /* index of last bit length code of non zero freq */

      /* Determine the bit length frequencies for literal and distance trees */
      ScanTree(dyn_ltree.data(), l_desc.max_code);
      ScanTree(dyn_dtree.data(), d_desc.max_code);

      /* Build the bit length tree: */
      BuildTree(&bl_desc);
      /* opt_len now includes the length of the tree representations, except
       * the lengths of the bit lengths codes and the 5+5+4 bits for the counts.
       */

      /* Determine the number of bit length codes to send. The pkzip format
       * requires that at least 4 bit length codes be sent. (appnote.txt says
       * 3 but the actual value used is 4.)
       */
      for (max_blindex = BL_CODES - 1; max_blindex >= 3; max_blindex--) {
        if (bl_tree[bl_order[max_blindex]].dl.len != 0) {
          break;
        }
      }
      /* Update opt_len to include the bit length tree and counts */
      opt_len += 3 * (max_blindex + 1) + 5 + 5 + 4;
#if 0
    Tracev((stderr, "\ndyn trees: dyn %lu, stat %lu", opt_len, static_len));
#endif

      return max_blindex;
    }

    /* ===========================================================================
     * Send the header for a block using dynamic Huffman trees: the counts, the
     * lengths of the bit length codes, the literal tree and the distance tree.
     * IN assertion: lcodes >= 257, dcodes >= 1, blcodes >= 4.
     */
    void SendAllTrees(int32_t lcodes, int32_t dcodes, int32_t blcodes) noexcept {
      Assert(lcodes >= 257 && dcodes >= 1 && blcodes >= 4, "not enough codes");
      Assert(lcodes <= L_CODES && dcodes <= D_CODES && blcodes <= BL_CODES, "too many codes");
#if 0
    Tracev((stderr, "\nbl counts: "));
#endif
      SendBits(lcodes - 257, 5); /* not +255 as stated in appnote.txt */
      SendBits(dcodes - 1, 5);
      SendBits(blcodes - 4, 4); /* not -3 as stated in appnote.txt */
      for (int32_t rank = 0; rank < blcodes; rank++) {
#if 0
      Tracev((stderr, "\nbl code %2d ", bl_order[rank]));
#endif
        SendBits(bl_tree[bl_order[rank]].dl.len, 3);
      }
      SendTree(dyn_ltree.data(), lcodes - 1); /* send the literal tree */
      SendTree(dyn_dtree.data(), dcodes - 1); /* send the distance tree */
    }

    /* ===========================================================================
     * Set the file type to ASCII or BINARY, using a crude approximation:
     * binary if more than 20% of the bytes are <= 6 or >= 128, ascii otherwise.
     * IN assertion: the fields freq of dyn_ltree are set and the total of all
     * frequencies does not exceed 64K (to fit in an int on 16 bit machines).
     */
    void SetFileType() noexcept {
      uint32_t n{0};
      uint32_t ascii_freq{0};
      uint32_t bin_freq{0};
      while (n < 7) {
        bin_freq += dyn_ltree[n++].fc.freq;
      }
      while (n < 128) {
        ascii_freq += dyn_ltree[n++].fc.freq;
      }
      while (n < LITERALS) {
        bin_freq += dyn_ltree[n++].fc.freq;
      }
      *file_type = bin_freq > (ascii_freq >> 2) ? BINARY : ASCII;
    }

    /* ===========================================================================
     * Send the block data compressed using the given Huffman trees
     */
    void CompressBlock(ct_data* ltree, ct_data* dtree) noexcept {
      uint32_t dist;    /* distance of matched string */
      int32_t lc;       /* match length or unmatched char (if dist == 0) */
      uint32_t lx{0};   /* running index in l_buf */
      uint32_t dx = 0;  /* running index in d_buf */
      uint32_t fx = 0;  /* running index in flag_buf */
      uint8_t flag = 0; /* current flags */
      uint32_t code;    /* the code to send */
      int32_t extra;    /* number of extra bits to send */

      if (last_lit != 0) {
        do {
          if ((lx & 7) == 0) {
            flag = flag_buf[fx++];
          }
          lc = l_buf[lx++];
          if ((flag & 1) == 0) {
            send_code(lc, ltree); /* send a literal byte */
#if 0
            Tracecv(isgraph(lc), (stderr, " '%c' ", lc));
#endif
          } else {
            /* Here, lc is the match length - MIN_MATCH */
            code = length_code[lc];
            send_code(code + LITERALS + 1, ltree); /* send the length code */
            extra = extra_lbits[code];
            if (extra != 0) {
              lc -= base_length[code];
              SendBits(lc, extra); /* send the extra length bits */
            }
            dist = d_buf[dx++];
            /* Here, dist is the match distance - 1 */
            code = d_code(dist);
            Assert(code < D_CODES, "bad d_code");

            send_code(code, dtree); /* send the distance code */
            extra = extra_dbits[code];
            if (extra != 0) {
              dist -= base_dist[code];
              SendBits(dist, extra); /* send the extra distance bits */
            }
          } /* literal or match pair ? */
          flag >>= 1;
        } while (lx < last_lit);
      }

      send_code(END_BLOCK, ltree);
    }
  };  // namespace

  /* ===========================================================================
   * Determine the best encoding for the current block: dynamic trees, static
   * trees or store, and output the encoded block to the zip file. This function
   * returns the total compressed length for the file so far.
   */
  auto FlushBlock(char* buf, uint32_t stored_len, int32_t pad, int32_t eof) noexcept -> int32_t {
    uint32_t opt_lenb, static_lenb; /* opt_len and static_len in bytes */
    int32_t max_blindex;            /* index of last bit length code of non zero freq */

    flag_buf[last_flags] = flags; /* Save the flags for the last 8 items */

    /* Check if the file is ascii or binary */
    if (*file_type == uint16_t(UNKNOWN)) {
      SetFileType();
    }

    /* Construct the literal and distance trees */
    BuildTree(&l_desc);
#if 0
    Tracev((stderr, "\nlit data: dyn %lu, stat %lu", opt_len, static_len));
#endif

    BuildTree(&d_desc);
#if 0
    Tracev((stderr, "\ndist data: dyn %lu, stat %lu", opt_len, static_len));
#endif
    /* At this point, opt_len and static_len are the total bit lengths of
     * the compressed block data, excluding the tree representations.
     */

    /* Build the bit length tree for the above two trees, and get the index
     * in bl_order of the last bit length code to send.
     */
    max_blindex = BuildBitLengthTree();

    /* Determine the best encoding. Compute first the block length in bytes */
    opt_lenb = (opt_len + 3 + 7) >> 3;
    static_lenb = (static_len + 3 + 7) >> 3;

#if 0
    Trace((stderr, "\nopt %lu(%lu) stat %lu(%lu) stored %lu lit %u dist %u ", opt_lenb, opt_len, static_lenb, static_len, stored_len, last_lit, last_dist));
#endif

    if (static_lenb <= opt_lenb) {
      opt_lenb = static_lenb;
    }

    /* If compression failed and this is the first and last block,
     * and if we can seek through the zip file (to rewrite the static header),
     * the whole file is transformed into a stored file:
     */
    if (stored_len + 4 <= opt_lenb && buf != nullptr) {
      /* 4: two words for the lengths */
      /* The test buf != nullptr is only necessary if LIT_BUFSIZE > WSIZE.
       * Otherwise we can't have processed more than WSIZE input bytes since
       * the last block flush, because compression would have been
       * successful. If LIT_BUFSIZE <= WSIZE, it is never too late to
       * transform a block into a stored block.
       */
      SendBits((STORED_BLOCK << 1) + eof, 3); /* send block type */
      compressed_len = (compressed_len + 3 + 7) & ~7L;
      compressed_len += (stored_len + 4) << 3;
      CopyBlock(buf, stored_len, 1); /* with header */
    } else if (static_lenb == opt_lenb) {
      SendBits((STATIC_TREES << 1) + eof, 3);
      CompressBlock(static_ltree.data(), static_dtree.data());
      compressed_len += 3 + static_len;
    } else {
      SendBits((DYN_TREES << 1) + eof, 3);
      SendAllTrees(l_desc.max_code + 1, d_desc.max_code + 1, max_blindex + 1);
      CompressBlock(dyn_ltree.data(), dyn_dtree.data());
      compressed_len += 3 + opt_len;
    }
    InitBlock();

    if (eof) {
      BitsWindup();
      compressed_len += 7; /* align on byte boundary */
    } else if (pad && (compressed_len % 8) != 0) {
      SendBits((STORED_BLOCK << 1) + eof, 3); /* send block type */
      compressed_len = (compressed_len + 3 + 7) & ~7L;
      CopyBlock(buf, 0, 1); /* with header */
    }

    return compressed_len >> 3;
  }

  /* ===========================================================================
   * Save the match info and tally the frequency counts. Return true if
   * the current block must be flushed.
   */
  auto CtTally(int32_t dist, int32_t lc) noexcept -> int32_t {
    l_buf[last_lit++] = uint8_t(lc);
    if (dist == 0) {
      /* lc is the unmatched char */
      dyn_ltree[lc].fc.freq++;
    } else {
      /* Here, lc is the match length - MIN_MATCH */
      dist--; /* dist = match distance - 1 */
      Assert(uint16_t(dist) < uint16_t(MAX_DIST) && uint16_t(lc) <= uint16_t(MAX_MATCH - MIN_MATCH) && uint16_t(d_code(dist)) < uint16_t(D_CODES), "ct_tally: bad match");

      dyn_ltree[length_code[lc] + LITERALS + 1].fc.freq++;
      dyn_dtree[d_code(dist)].fc.freq++;

      d_buf[last_dist++] = uint16_t(dist);
      flags |= flag_bit;
    }
    flag_bit <<= 1;

    /* Output the flags if they fill a byte: */
    if ((last_lit & 7) == 0) {
      flag_buf[last_flags++] = flags;
      flags = 0;
      flag_bit = 1;
    }
    /* Try to guess if it is profitable to stop the current block here */
    if (level > 2 && (last_lit & 0xfff) == 0) {
      /* Compute an upper bound for the compressed length */
      uint32_t out_length = last_lit * 8u;
      const uint32_t in_length = strstart - block_start;
      for (uint32_t dcode = 0; dcode < D_CODES; dcode++) {
        out_length += uint32_t(dyn_dtree[dcode].fc.freq) * (5u + extra_dbits[dcode]);
      }
      out_length >>= 3;
#if 0
      Trace((stderr, "\nlast_lit %u, last_dist %u, in %ld, out ~%ld(%ld%%) ", last_lit, last_dist, in_length, out_length, 100L - out_length * 100L / in_length));
#endif
      if (last_dist < last_lit / 2 && out_length < in_length / 2) {
        return 1;
      }
    }
    return (last_lit == LIT_BUFSIZE - 1 || last_dist == DIST_BUFSIZE);
    /* We avoid equality with LIT_BUFSIZE because of wraparound at 64K
     * on 16 bit machines and because stored blocks are restricted to
     * 64K-1 bytes.
     */
  }
};  // namespace gzip
