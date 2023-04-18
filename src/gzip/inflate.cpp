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
#include <cstdlib>
#include <cstring>
#include "gzip.h"

namespace gzip {

  /**
   * @struct huft
   * @brief Huffman code lookup table entry
   *
   *  Huffman code lookup table entry--this entry is four bytes for machines
     that have 16-bit pointers (e.g. PC's in the small or medium model).
     Valid extra bits are 0..13.  e == 15 is EOB (end of block), e == 16
     means that v is a literal, 16 < e < 32 means that v is a pointer to
     the next table, which codes e - 16 bits, and lastly e == 99 indicates
     an unused code.  If a code with e == 99 is looked up, this implies an
     error in the data. */
  struct huft {
    uint8_t e; /**< number of extra bits or operation */
    uint8_t b; /**< number of bits in this code or subcode */
    union {
      uint16_t n; /**< literal, length base, or distance base */
      huft* t;    /**< pointer to next level of table */
    } v;
  };

/* The inflate algorithm uses a sliding 32K byte window on the uncompressed
   stream to find repeated byte strings.  This is implemented here as a
   circular buffer.  The index is updated simply by incrementing and then
   and'ing with 0x7fff (32K-1). */
/* It is left to other modules to supply the 32K area.  It is assumed
   to be usable as if it were declared "uint8_t window[32768];" or as just
   "uint8_t *window;" and then malloc'ed in the latter case.  The definition
   must be in unzip.h, included above. */
/* uint32_t wp;             current position in window */
#define flush_output(w) \
  outcnt = (w);         \
  flush_window()

  namespace {
    /* Tables for deflate from PKZIP's appnote.txt. */
    /* Order of the bit length code lengths */
    constexpr std::array<uint32_t, 19> bitlen_order{{16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15}};
    /* Copy lengths for literal codes 257..285 */
    constexpr std::array<uint16_t, 31> lit_lengths{{3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258, 0, 0}};
    /* note: see note #13 above about the 258 in this list. */
    /* Extra bits for literal codes 257..285 */
    constexpr std::array<uint16_t, 31> lit_extrabits{{0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0, 99, 99}}; /* 99==invalid */
    /* Copy offsets for distance codes 0..29 */
    constexpr std::array<uint16_t, 30> dist_offsets{{1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577}};
    /* Extra bits for distance codes */
    constexpr std::array<uint16_t, 30> dist_extrabits{{0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13}};

    /* Macros for inflate() bit peeking and grabbing.
         The usage is:

              NEEDBITS(j)
              x = b & mask_bits[j];
              DUMPBITS(j)

         where NEEDBITS makes sure that b has at least j bits in it, and
         DUMPBITS removes the bits from b.  The macros use the variable k
         for the number of bits in b.  Normally, b and k are register
         variables for speed, and are initialised at the beginning of a
         routine that uses these macros from a global bit buffer and count.
         The macros also use the variable w, which is a cached copy of wp.

         If we assume that EOB will be the longest code, then we will never
         ask for bits with NEEDBITS that are beyond the end of the stream.
         So, NEEDBITS should not read any more bytes than are needed to
         meet the request.  Then no bytes need to be "returned" to the buffer
         at the end of the last block.

         However, this assumption is not true for fixed blocks--the EOB code
         is 7 bits, but the other literal/length codes can be 8 or 9 bits.
         (The EOB code is shorter than other codes because fixed blocks are
         generally short.  So, while a block always has an EOB, many other
         literal/length codes have a significantly lower probability of
         showing up at all.)  However, by making the first table have a
         lookup of seven bits, the EOB code will be found in that first
         lookup, and so will not require that too many bits be pulled from
         the stream.
       */

    uint32_t bb; /* bit buffer */
    uint32_t bk; /* bits in bit buffer */

    constexpr std::array<uint16_t, 17> mask_bits{{0x0000, 0x0001, 0x0003, 0x0007,  //
                                                  0x000F, 0x001F, 0x003F, 0x007F,  //
                                                  0x00FF, 0x01FF, 0x03FF, 0x07FF,  //
                                                  0x0FFF, 0x1FFF, 0x3FFF, 0x7FFF,  //
                                                  0xFFFF}};

#define GETBYTE() ((inptr < insize) ? inbuf[inptr++] : (outcnt = w, fill_inbuf(0)))

#define NEEDBITS(n)                \
  while (k < (n)) {                \
    b |= uint32_t(GETBYTE()) << k; \
    k += 8;                        \
  }

#define DUMPBITS(n) \
  b >>= (n);        \
  k -= (n)

    /*
       Huffman code decoding is performed using a multi-level table lookup.
       The fastest way to decode is to simply build a lookup table whose
       size is determined by the longest code.  However, the time it takes
       to build this table can also be a factor if the data being decoded
       is not very long.  The most common codes are necessarily the
       shortest codes, so those codes dominate the decoding time, and hence
       the speed.  The idea is you can have a shorter table that decodes the
       shorter, more probable codes, and then point to subsidiary tables for
       the longer codes.  The time it costs to decode the longer codes is
       then traded against the time it takes to make longer tables.

       This results of this trade are in the variables lbits and dbits
       below.  lbits is the number of bits the first level table for literal/
       length codes can decode in one step, and dbits is the same thing for
       the distance codes.  Subsequent tables are also less than or equal to
       those sizes.  These values may be adjusted either when all of the
       codes are shorter than that, in which case the longest code length in
       bits is used, or when the shortest code is *longer* than the requested
       table size, in which case the length of the shortest code in bits is
       used.

       There are two different values for the two tables, since they code a
       different number of possibilities each.  The literal/length table
       codes 286 possible values, or in a flat code, a little over eight
       bits.  The distance table codes 30 possible values, or a little less
       than five bits, flat.  The optimum values for speed end up being
       about one bit more than those, so lbits is 8+1 and dbits is 5+1.
       The optimum values may differ though from machine to machine, and
       possibly even between compilers.  Your mileage may vary.
     */

    const uint32_t lbits = 9; /* bits in base literal/length lookup table */
    const uint32_t dbits = 6; /* bits in base distance lookup table */

/* If BMAX needs to be larger than 16, then h and x[] should be uint32_t. */
#define BMAX 16   /* maximum bit length of any code (16 for explode) */
#define N_MAX 288 /* maximum number of codes in any set */

    uint32_t hufts; /* track memory usage */

    /* Free the malloc'ed tables T built by huft_build(), which makes a linked
       list of the tables it made, with the links in a dummy first entry of
       each table. */
    auto HuftTree(huft* t) noexcept -> int32_t {
      /* Go through linked list, freeing from the malloced (t[-1]) address. */
      huft* p = t;
      while (p != nullptr) {
        huft* q = (--p)->v.t;
        free(p);
        p = q;
      }
      return 0;
    }

    auto HuftBuild(uint32_t* b,       /* code lengths in bits (all assumed <= BMAX) */
                   uint32_t n,        /* number of codes (assumed <= N_MAX) */
                   uint32_t s,        /* number of simple-valued codes (0..s-1) */
                   const uint16_t* d, /* list of base values for non-simple codes */
                   const uint16_t* e, /* list of extra bits for non-simple codes */
                   huft** t,          /* result: starting table */
                   uint32_t* m        /* maximum lookup bits, returns actual */
                   ) noexcept -> uint32_t
    /* Given a list of code lengths and a maximum table size, make a set of
       tables to decode that set of codes.  Return zero on success, one if
       the given code set is incomplete (the tables are still built in this
       case), two if the input is invalid (all zero length codes or an
       oversubscribed set of lengths), and three if not enough memory. */
    {
      uint32_t a;                       /* counter for codes of length k */
      std::array<uint32_t, BMAX + 1> c; /* bit length count table */
      uint32_t f;                       /* i repeats in table every f entries */
      uint32_t g;                       /* maximum code length */
      uint32_t i;                       /* counter, current code */
      uint32_t j;                       /* counter */
      uint32_t k;                       /* number of bits in current code */
      uint32_t l;                       /* bits per table (returned in m) */
      uint32_t* p;                      /* pointer into c[], b[], or v[] */
      huft* q;                          /* points to current table */
      huft r;                           /* table entry for structure assignment */
      std::array<huft*, BMAX> u;        /* table stack */
      std::array<uint32_t, N_MAX> v;    /* values in order of bit length */
      int32_t w;                        /* bits before this table == (l * h) */
      std::array<uint32_t, BMAX + 1> x; /* bit offsets, then code stack */
      uint32_t* xp;                     /* pointer into x */
      int32_t y;                        /* number of dummy codes added */
      uint32_t z;                       /* number of entries in current table */

      /* Generate counts for each bit length */
      c.fill(0);
      p = b;
      i = n;
      do {
#if 0
      Tracecv(*p, (stderr, (n - i >= ' ' && n - i <= '~' ? "%c %d\n" : "0x%x %d\n"), n - i, *p));
#endif
        c[*p]++; /* assume all entries <= BMAX */
        p++;     /* Can't combine with above line (Solaris bug) */
      } while (--i);
      if (c[0] == n) { /* null input--all zero length codes */
        q = static_cast<huft*>(malloc(3 * sizeof *q));
        if (!q) {
          return 3;
        }
        hufts += 3;
        q[0].v.t = nullptr;
        q[1].e = 99; /* invalid code marker */
        q[1].b = 1;
        q[2].e = 99; /* invalid code marker */
        q[2].b = 1;
        *t = q + 1;
        *m = 1;
        return 0;
      }

      /* Find minimum and maximum length, bound *m by those */
      l = *m;
      for (j = 1; j <= BMAX; j++) {
        if (c[j]) {
          break;
        }
      }
      k = j; /* minimum code length */
      if (l < j) {
        l = j;
      }
      for (i = BMAX; i; i--) {
        if (c[i]) {
          break;
        }
      }
      g = i; /* maximum code length */
      if (l > i) {
        l = i;
      }
      *m = l;

      /* Adjust last length count to fill out codes, if needed */
      for (y = 1 << j; j < i; j++, y <<= 1) {
        if ((y -= int32_t(c[j])) < 0) {
          return 2; /* bad input: more codes than bits */
        }
      }
      if ((y -= int32_t(c[i])) < 0) {
        return 2;
      }
      c[i] += uint32_t(y);

      /* Generate starting offsets into the value table for each length */
      x[1] = j = 0;
      p = &c[0] + 1;
      xp = &x[0] + 2;
      while (--i) { /* note that i == g from above */
        *xp++ = (j += *p++);
      }

      /* Make a table of values in order of bit lengths */
      p = b;
      i = 0;
      do {
        if ((j = *p++) != 0) {
          v[x[j]++] = i;
        }
      } while (++i < n);
      n = x[g]; /* set n to length of v */

      /* Generate the Huffman codes and for each, make the table entries */
      x[0] = i = 0;    /* first Huffman code is zero */
      p = &v[0];       /* grab values in bit order */
      int32_t h = -1;  /* no tables yet--level -1 */
      w = int32_t(-l); /* bits decoded == (l * h) */
      u[0] = nullptr;  /* just to keep compilers happy */
      q = nullptr;     /* ditto */
      z = 0;           /* ditto */

      /* go through the bit lengths (k already is bits in shortest code) */
      for (; k <= g; k++) {
        a = c[k];
        while (a--) {
          /* here i is the Huffman code of length k bits for value *p */
          /* make tables up to required level */
          while (k > (w + l)) {
            h++;
            w += l; /* previous table always l bits */

            /* compute minimum size table less than or equal to l bits */
            z = (z = g - w) > l ? l : z;          /* upper limit on table size */
            if ((f = 1 << (j = k - w)) > a + 1) { /*  try a k-w bit table too, few codes for k-w bit table */
              f -= a + 1;                         /* deduct codes from patterns left */
              xp = &c[0] + k;
              if (j < z) {
                while (++j < z) { /* try smaller tables up to z bits */
                  if ((f <<= 1) <= *++xp) {
                    break; /* enough codes to use up j bits */
                  }
                  f -= *xp; /* else deduct codes from patterns */
                }
              }
            }
            z = 1 << j; /* table entries for j-bit table */

            /* allocate and link in new table */
            if ((q = static_cast<huft*>(malloc((z + 1) * sizeof(huft)))) == nullptr) {
              if (h) {
                HuftTree(u[0]);
              }
              return 3; /* not enough memory */
            }
            hufts += z + 1; /* track memory usage */
            *t = q + 1;     /* link to list for huft_free() */
            *(t = &(q->v.t)) = nullptr;
            u[h] = ++q; /* table starts after link */

            /* connect to last table, if there is one */
            if (h) {
              x[h] = i;              /* save pattern for backing up */
              r.b = uint8_t(l);      /* bits to dump before this table */
              r.e = uint8_t(16 + j); /* bits in this table */
              r.v.t = q;             /* pointer to this table */
              j = i >> (w - l);      /* (get around Turbo C bug) */
              u[h - 1][j] = r;       /* connect to last table */
            }
          }

          /* set up table entry in r */
          r.b = uint8_t(k - w);
          if (p >= (&v[0] + n)) {
            r.e = 99; /* out of values--invalid code */
          } else if (*p < s) {
            r.e = uint8_t(*p < 256 ? 16 : 15); /* 256 is end-of-block code */
            r.v.n = uint16_t(*p);              /* simple code is just the value */
            p++;                               /* one compiler does not like *p++ */
          } else {
            r.e = uint8_t(e[*p - s]); /* non-simple--look up in lists */
            r.v.n = d[*p++ - s];
          }

          /* fill code-like entries with r */
          f = 1 << (k - w);
          for (j = i >> w; j < z; j += f) {
            q[j] = r;
          }

          /* backwards increment the k-bit code i */
          for (j = 1 << (k - 1); i & j; j >>= 1) {
            i ^= j;
          }
          i ^= j;

          /* backup over finished tables */
          while ((i & ((1 << w) - 1)) != x[h]) {
            h--; /* don't need to update q */
            w -= l;
          }
        }
      }

      /* Return true (1) if we were given an incomplete table */
      return y != 0 && g != 1;
    }

    /* tl, td:   literal/length and distance decoder tables */
    /* bl, bd:   number of bits decoded by tl[] and td[] */
    /* inflate (decompress) the codes in a deflated (compressed) block.
       Return an error code or zero if it all goes ok. */
    auto InflateCodes(huft* tl, huft* td, const uint32_t bl, const uint32_t bd) noexcept -> int32_t {
      uint32_t e;      /* table entry flag/number of extra bits */
      uint32_t n, d;   /* length and index for copy */
      huft* t;         /* pointer to table entry */
      uint32_t ml, md; /* masks for bl and bd bits */
      uint32_t b;      /* bit buffer */
      uint32_t k;      /* number of bits in bit buffer */

      /* make local copies of globals */
      b = bb; /* initialize bit buffer */
      k = bk;
      uint32_t w = outcnt; /* Initialise window position */

      /* inflate the coded data */
      ml = mask_bits[bl]; /* precompute masks for speed */
      md = mask_bits[bd];
      for (;;) { /* do until end of block */
        NEEDBITS(uint32_t(bl))
        if ((e = (t = tl + (b & ml))->e) > 16) {
          do {
            if (e == 99) {
              return 1;
            }
            DUMPBITS(t->b);
            e -= 16;
            NEEDBITS(e)
          } while ((e = (t = t->v.t + (b & mask_bits[e]))->e) > 16);
        }
        DUMPBITS(t->b);
        if (e == 16) { /* then it's a literal */
          window[w++] = uint8_t(t->v.n);
#if 0
        Tracevv((stderr, "%c", window[w - 1]));
#endif
          if (w == WSIZE) {
            flush_output(w);
            w = 0;
          }
        } else { /* it's an EOB or a length */
          /* exit if end of block */
          if (e == 15) {
            break;
          }

          /* get length of block to copy */
          NEEDBITS(e)
          n = t->v.n + (b & mask_bits[e]);
          DUMPBITS(e);

          /* decode distance of block to copy */
          NEEDBITS(uint32_t(bd))
          if ((e = (t = td + (b & md))->e) > 16) {
            do {
              if (e == 99) {
                return 1;
              }
              DUMPBITS(t->b);
              e -= 16;
              NEEDBITS(e)
            } while ((e = (t = t->v.t + (b & mask_bits[e]))->e) > 16);
          }
          DUMPBITS(t->b);
          NEEDBITS(e)
          d = w - t->v.n - (b & mask_bits[e]);
          DUMPBITS(e);
#if 0
        Tracevv((stderr, "\\[%d,%d]", w - d, n));
#endif

          /* do the copy */
          do {
            n -= (e = (e = WSIZE - ((d &= WSIZE - 1) > w ? d : w)) > n ? n : e);
#ifndef DEBUG
            if (e <= (d < w ? w - d : d - w)) {
              memcpy(&window[w], &window[d], e);
              w += e;
              d += e;
            } else /* do it slow to avoid memcpy() overlap */
#endif
              do {
                window[w++] = window[d++];
#if 0
              Tracevv((stderr, "%c", window[w - 1]));
#endif
              } while (--e);
            if (w == WSIZE) {
              flush_output(w);
              w = 0;
            }
          } while (n);
        }
      }

      /* restore the globals from the locals */
      outcnt = w; /* restore global window pointer */
      bb = b;     /* restore global bit buffer */
      bk = k;

      /* done */
      return 0;
    }

    /* "decompress" an inflated type 0 (stored) block. */
    auto InflateStored() noexcept -> uint32_t {
      uint32_t n; /* number of bytes in block */
      uint32_t b; /* bit buffer */
      uint32_t k; /* number of bits in bit buffer */

      /* make local copies of globals */
      b = bb; /* Initialise bit buffer */
      k = bk;
      uint32_t w = outcnt; /* Initialise window position */

      /* go to byte boundary */
      n = k & 7;
      DUMPBITS(n);

      /* get the length and its complement */
      NEEDBITS(16)
      n = b & 0xFFFF;
      DUMPBITS(16);
      NEEDBITS(16)
      if (n != (~b & 0xFFFF)) {
        return 1; /* error in compressed data */
      }
      DUMPBITS(16);

      /* read and output the compressed data */
      while (n--) {
        NEEDBITS(8)
        window[w++] = uint8_t(b);
        if (w == WSIZE) {
          flush_output(w);
          w = 0;
        }
        DUMPBITS(8);
      }

      /* restore the globals from the locals */
      outcnt = w; /* restore global window pointer */
      bb = b;     /* restore global bit buffer */
      bk = k;
      return 0;
    }

    /* decompress an inflated type 1 (fixed Huffman codes) block.  We should
       either replace this with a custom decoder, or at least precompute the
       Huffman tables. */
    auto InflateFixed() noexcept -> uint32_t {
      uint32_t i;                  /* temporary variable */
      huft* tl;                    /* literal/length code table */
      huft* td;                    /* distance code table */
      uint32_t bl;                 /* lookup bits for tl */
      uint32_t bd;                 /* lookup bits for td */
      std::array<uint32_t, 288> l; /* length list for huft_build */

      /* set up literal table */
      for (i = 0; i < 144; i++) {
        l[i] = 8;
      }
      for (; i < 256; i++) {
        l[i] = 9;
      }
      for (; i < 280; i++) {
        l[i] = 7;
      }
      for (; i < 288; i++) { /* make a complete, but wrong code set */
        l[i] = 8;
      }
      bl = 7;
      {
        const auto state{HuftBuild(l.data(), l.size(), 257, lit_lengths.data(), lit_extrabits.data(), &tl, &bl)};
        if (0 != state) {
          return state;
        }
      }

      /* set up distance table */
      for (i = 0; i < 30; i++) { /* make an incomplete code set */
        l[i] = 5;
      }
      bd = 5;
      {
        const auto state{HuftBuild(l.data(), 30, 0, dist_offsets.data(), dist_extrabits.data(), &td, &bd)};
        if (state > 1) {
          HuftTree(tl);
          return state;
        }
      }

      /* decompress until an end-of-block code */
      if (InflateCodes(tl, td, bl, bd)) {
        return 1;
      }

      /* free the decoding tables, return */
      HuftTree(tl);
      HuftTree(td);
      return 0;
    }

    /* decompress an inflated type 2 (dynamic Huffman codes) block. */
    auto InflateDynamic() noexcept -> uint32_t {
      uint32_t i; /* temporary variables */
      uint32_t j;
      uint32_t l;  /* last length */
      uint32_t m;  /* mask for bit lengths table */
      uint32_t n;  /* number of lengths to get */
      huft* tl;    /* literal/length code table */
      huft* td;    /* distance code table */
      uint32_t bl; /* lookup bits for tl */
      uint32_t bd; /* lookup bits for td */
      uint32_t nb; /* number of bit length codes */
      uint32_t nl; /* number of literal/length codes */
      uint32_t nd; /* number of distance codes */
#ifdef PKZIP_BUG_WORKAROUND
      std::array<uint32_t, 288 + 32> ll; /* literal/length and distance code lengths */
#else
      std::array<uint32_t, 286 + 30> ll; /* literal/length and distance code lengths */
#endif
      uint32_t b; /* bit buffer */
      uint32_t k; /* number of bits in bit buffer */

      /* make local bit buffer */
      b = bb;
      k = bk;
      const uint32_t w{outcnt}; /* current window position */

      /* read in table lengths */
      NEEDBITS(5)
      nl = 257 + (b & 0x1f); /* number of literal/length codes */
      DUMPBITS(5);
      NEEDBITS(5)
      nd = 1 + (b & 0x1f); /* number of distance codes */
      DUMPBITS(5);
      NEEDBITS(4)
      nb = 4 + (b & 0xf); /* number of bit length codes */
      DUMPBITS(4);
#ifdef PKZIP_BUG_WORKAROUND
      if (nl > 288 || nd > 32)
#else
      if (nl > 286 || nd > 30)
#endif
        return 1; /* bad lengths */

      /* read in bit-length-code lengths */
      for (j = 0; j < nb; j++) {
        NEEDBITS(3)
        ll[bitlen_order[j]] = b & 7;
        DUMPBITS(3);
      }
      for (; j < 19; j++) {
        ll[bitlen_order[j]] = 0;
      }

      /* build decoding table for trees--single level, 7 bit lookup */
      bl = 7;
      if ((i = HuftBuild(ll.data(), 19, 19, nullptr, nullptr, &tl, &bl)) != 0) {
        if (i == 1) {
          HuftTree(tl);
        }
        return i; /* incomplete code set */
      }

      if (tl == nullptr) { /* Grrrhhh */
        return 2;
      }

      /* read in literal and distance code lengths */
      n = nl + nd;
      m = mask_bits[bl];
      i = l = 0;
      while (i < n) {
        NEEDBITS(bl)
        j = (td = tl + (b & m))->b;
        DUMPBITS(j);
        if (td->e == 99) { /* Invalid code.  */
          HuftTree(tl);
          return 2;
        }
        j = td->v.n;
        if (j < 16) {         /* length of code in bits (0..15) */
          ll[i++] = l = j;    /* save last length in l */
        } else if (j == 16) { /* repeat last length 3 to 6 times */
          NEEDBITS(2)
          j = 3 + (b & 3);
          DUMPBITS(2);
          if (i + j > n) {
            return 1;
          }
          while (j--) {
            ll[i++] = l;
          }
        } else if (j == 17) { /* 3 to 10 zero length codes */
          NEEDBITS(3)
          j = 3 + (b & 7);
          DUMPBITS(3);
          if (i + j > n) {
            return 1;
          }
          while (j--) {
            ll[i++] = 0;
          }
          l = 0;
        } else { /* j == 18: 11 to 138 zero length codes */
          NEEDBITS(7)
          j = 11 + (b & 0x7f);
          DUMPBITS(7);
          if (i + j > n) {
            return 1;
          }
          while (j--) {
            ll[i++] = 0;
          }
          l = 0;
        }
      }

      /* free decoding table for trees */
      HuftTree(tl);

      /* restore the global bit buffer */
      bb = b;
      bk = k;

      /* build the decoding tables for literal/length and distance codes */
      bl = lbits;
      if ((i = HuftBuild(ll.data(), nl, 257, lit_lengths.data(), lit_extrabits.data(), &tl, &bl)) != 0) {
        if (i == 1) {
#if 0
        Trace((stderr, " incomplete literal tree\n"));
#endif
          HuftTree(tl);
        }
        return i; /* incomplete code set */
      }
      bd = dbits;
      if ((i = HuftBuild(ll.data() + nl, nd, 0, dist_offsets.data(), dist_extrabits.data(), &td, &bd)) != 0) {
        if (i == 1) {
#if 0
        Trace((stderr, " incomplete distance tree\n"));
#endif
#ifdef PKZIP_BUG_WORKAROUND
          i = 0;
        }
#else
          HuftTree(td);
        }
        HuftTree(tl);
        return i; /* incomplete code set */
#endif
      }

      {
        /* decompress until an end-of-block code */
        const uint32_t err = InflateCodes(tl, td, bl, bd) ? 1 : 0;

        /* free the decoding tables */
        HuftTree(tl);
        HuftTree(td);

        return err;
      }
    }

    /* decompress an inflated block */
    /* E is the last block flag */
    auto InflateBlock(int32_t* e) noexcept -> uint32_t {
      // make local bit buffer
      uint32_t b = bb;           // bit buffer
      uint32_t k = bk;           // number of bits in bit buffer
      const uint32_t w{outcnt};  // current window position

      /* read in last block bit */
      NEEDBITS(1)
      *e = b & 1;
      DUMPBITS(1);

      /* read in block type */
      NEEDBITS(2)
      const uint32_t t{b & 3u};  // block type
      DUMPBITS(2);

      /* restore the global bit buffer */
      bb = b;
      bk = k;

      /* inflate that block type */
      if (t == 2) {
        return InflateDynamic();
      }
      if (t == 0) {
        return InflateStored();
      }
      if (t == 1) {
        return InflateFixed();
      }

      /* bad block type */
      return 2;
    }
  };  // namespace

  /* decompress an inflated entry */
  auto Inflate() noexcept -> uint32_t {
    int32_t e; /* last block flag */

    inptr = 0;
    insize = 0;
    bytes_in = 0;

    /* Initialise window, bit buffer */
    outcnt = 0;
    bk = 0;
    bb = 0;

    /* decompress until the last block */
    uint32_t h = 0; /* maximum huft's malloc'ed */
    do {
      hufts = 0;
      const uint32_t r = InflateBlock(&e); /* result code */
      if (GZip_OK != r) {
        return r;
      }
      if (hufts > h) {
        h = hufts;
      }
    } while (!e);

    /* Undo too much lookahead. The next read will be byte aligned so we
     * can discard unused bits in the last meaningful byte.
     */
    while (bk >= 8) {
      bk -= 8;
      inptr--;
    }

    /* flush out window */
    flush_output(outcnt);

#if 0
    Trace((stderr, "<%u> ", h));
#endif
    /* return success */
    return GZip_OK;
  }
};  // namespace gzip
