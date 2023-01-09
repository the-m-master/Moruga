#include "IntegerXXL.h"

#if defined(_MSC_VER) || defined(TEST_MSC_VER)

#  include <array>
#  include <cassert>
#  include <cstdio>
#  include <cstring>
#  include <string>
#  include <string_view>
#  include <utility>

static constexpr uint32_t legbits{64};
static constexpr uint32_t sublegs{4};
static constexpr uint32_t sublegbits{legbits / 2};
static constexpr uint64_t sublegmask{(UINT64_C(1) << sublegbits) - 1};

uint128_t::uint128_t() noexcept = default;

uint128_t::uint128_t(uint64_t lsb) noexcept : m_lo{lsb} {}

uint128_t::uint128_t(uint64_t msb, uint64_t lsb) noexcept : m_hi{msb}, m_lo{lsb} {}

uint128_t::uint128_t(std::string_view text) noexcept {
  size_t size{text.size()};
  if (size > 40) {
    return;
  }
  std::array<uint8_t, 50> str;
  memcpy(str.data(), text.data(), size);
  size_t start{0};
  const bool isNegative{'-' == str[0]};
  if (isNegative || ('+' == str[0])) {
    start = 1;
  }
  while (('0' == str[start]) && (start < size)) {
    ++start;
  }
  for (size_t i{start}; i < size; ++i) {
    if ((str[i] < '0') || (str[i] > '9')) {
      return;
    }
    str[i] -= '0';
  }
  for (size_t bit{0}; (start < size) && (bit < 128); ++bit) {
    uint32_t carry{0};
    for (size_t i{start}; i < size; i++) {
      const auto a{str[i] + carry * 10};
      carry = a % 2;
      str[i] = uint8_t(a / 2);
    }
    if (bit < 64) {
      m_lo |= uint64_t(carry) << bit;
    } else {
      m_hi |= uint64_t(carry) << (bit - 64);
    }
    while ((0 == str[start]) && (start < size)) {
      ++start;
    }
  }
  if (isNegative) {
    negate();
  }
}

std::string uint128_t::str() const noexcept {
  uint128_t tmp(*this);
#  if 0
  if (isNegative()) {
    tmp.negate();
  }
#  endif
  std::array<char, 50> str;
  str.fill(0);
  uint32_t size{1};
  for (int32_t i{127}; i >= 0; --i) {
    bool carry{false};
    for (uint32_t j{0}; j < size; ++j) {
      str[48 - j] = char(str[48 - j] * 2 + (carry ? 1 : 0));
      carry = str[48 - j] >= 10;
      if (carry) {
        str[48 - j] -= 10;
      }
    }
    if (carry) {
      str[48 - size++] = 1;
    }

    if (i >= 64) {
      carry = (tmp.m_hi & (UINT64_C(1) << (i % 64))) > 0;
    } else {
      carry = (tmp.m_lo & (UINT64_C(1) << i)) > 0;
    }

    for (uint32_t j{0}; j < size && carry; ++j) {
      str[48 - j] += 1;
      carry = str[48 - j] >= 10;
      if (carry) {
        str[48 - j] = 0;
      }
    }
    if (carry) {
      str[48 - size++] = 1;
    }
  }
  for (uint32_t i{0}; i < size; ++i) {
    str[48 - i] += '0';
  }
#  if 0
  if (isNegative()) {
    str[48 - size++] = '-';
  }
#  endif
  return std::string(&str[49 - size]);
}

bool uint128_t::isNegative() const noexcept {
  return (m_hi & (UINT64_C(1) << 63)) != 0;
}

uint128_t& uint128_t::zero() noexcept {
  m_lo = m_hi = UINT64_C(0);
  return *this;
}

uint128_t& uint128_t::negate() noexcept {
  m_hi = ~m_hi;
  m_lo = ~m_lo;
  return *this += 1;
}

// clang-format off
uint128_t::operator   int8_t() const noexcept { return static_cast<  int8_t>(m_lo); }
uint128_t::operator  uint8_t() const noexcept { return static_cast< uint8_t>(m_lo); }
uint128_t::operator  int16_t() const noexcept { return static_cast< int16_t>(m_lo); }
uint128_t::operator uint16_t() const noexcept { return static_cast<uint16_t>(m_lo); }
uint128_t::operator  int32_t() const noexcept { return static_cast< int32_t>(m_lo); }
uint128_t::operator uint32_t() const noexcept { return static_cast<uint32_t>(m_lo); }
uint128_t::operator  int64_t() const noexcept { return static_cast< int64_t>(m_lo); }
uint128_t::operator uint64_t() const noexcept { return                       m_lo;  }

#  if defined(__GNUC__) && defined(__SIZEOF_INT128__)
uint128_t::operator  int128_v() const noexcept { return static_cast<int128_v>((uint128_v(m_hi) << 64) | uint128_v(m_lo)); }
uint128_t::operator uint128_v() const noexcept { return (static_cast<uint128_v>(m_hi) << 64) | static_cast<uint128_v>(m_lo); }
#  endif
// clang-format on

int uint128_t::cmp(const uint128_t& b) const noexcept {
  const auto hi{m_hi};
  const auto bhi{b.m_hi};
  if (isZero() && b.isZero()) {
    return 0;
  }
  if (hi < bhi) {
    return -1;
  }
  if (hi > bhi) {
    return 1;
  }
  if (m_lo < b.m_lo) {
    return -1;
  }
  if (m_lo > b.m_lo) {
    return 1;
  }
  return 0;
}

bool uint128_t::isZero() const noexcept {
  return (m_hi == 0) && (m_lo == 0);
}

uint32_t uint128_t::bits() const noexcept {
  const auto hi{m_hi};
  uint32_t bits{hi == 0 ? 0U : 64U};
  uint64_t temp{hi == 0 ? m_lo : hi};
  for (; temp > 0; temp >>= 1) {
    ++bits;
  }
  return bits;
}

uint128_t uint128_t::operator-() const noexcept {
  auto retval{*this};
  retval.negate();
  return retval;
}

uint128_t& uint128_t::operator++() noexcept {
  return operator+=(UINT64_C(1));
}

uint128_t uint128_t::operator++(int) noexcept {
  return operator+=(UINT64_C(1));
}

uint128_t& uint128_t::operator--() noexcept {
  return operator-=(UINT64_C(1));
}

uint128_t uint128_t::operator--(int) noexcept {
  return operator-=(UINT64_C(1));
}

uint128_t& uint128_t::operator+=(const uint128_t& b) noexcept {
  uint64_t result{m_lo + b.m_lo};
  const uint64_t carry{(result < m_lo) ? UINT64_C(1) : UINT64_C(0)};  // Wrapping
  m_lo = result;
  const auto hi{m_hi};
  const auto bhi{b.m_hi};
  result = hi + bhi + carry;
  m_hi = result;
  return *this;
}

uint128_t& uint128_t::operator<<=(uint32_t i) noexcept {
  if (i == 0) {
    return *this;
  }
  if (i > 128) {
    m_hi = 0;
    m_lo = 0;
    return *this;
  }
  auto hi{m_hi};
  if (i < legbits) {
    const uint64_t carry{(m_lo & (((UINT64_C(1) << i) - 1) << (legbits - i))) >> (legbits - i)};
    m_lo <<= i;
    hi <<= i;
    hi += carry;
    m_hi = hi;
    return *this;
  }
  hi = m_lo << (i - legbits);
  m_hi = hi;
  m_lo = 0;
  return *this;
}

uint128_t& uint128_t::operator>>=(uint32_t i) noexcept {
  if (i > 128) {
    m_hi = 0;
    m_lo = 0;
    return *this;
  }
  auto hi{m_hi};
  if (i < legbits) {
    const uint64_t carry{hi & ((UINT64_C(1) << i) - 1)};
    m_lo >>= i;
    hi >>= i;
    m_lo += carry << (legbits - i);
    m_hi = hi;
    return *this;
  }
  m_lo = hi >> (i - legbits);
  m_hi = 0;
  return *this;
}

uint128_t& uint128_t::operator-=(const uint128_t& b) noexcept {
  const bool operand_bigger{cmp(b) < 0};
  auto hi{m_hi};
  auto far_hi{b.m_hi};
  if (operand_bigger) {
    if (m_lo > b.m_lo) {
      /* The + 1 on the end is because we really want to use 2^64, or
       * UINT64_MAX + 1, but that can't be represented in a uint64_t.
       */
      m_lo = UINT64_MAX - m_lo + b.m_lo + 1;
      --far_hi;  // borrow
    } else {
      m_lo = b.m_lo - m_lo;
    }
    hi = far_hi - hi;
    m_hi = hi;
    return *this;
  }
  if (m_lo < b.m_lo) {
    m_lo = UINT64_MAX - b.m_lo + m_lo + 1;  // See UINT64_MAX comment above
    --hi;                                   // borrow
  } else {
    m_lo -= b.m_lo;
  }

  hi -= far_hi;
  m_hi = hi;
  return *this;
}

uint128_t& uint128_t::operator*=(const uint128_t& b) noexcept {
  if (isZero() || b.isZero()) {
    m_lo = 0;
    m_hi = 0;
    return *this;
  }

  auto hi{m_hi};
  const auto bhi{b.m_hi};
  if (hi && bhi) {
    m_hi = hi;
    return *this;
  }

  const uint32_t abits{bits()};
  const uint32_t bbits{b.bits()};
  if ((abits + bbits - 1) > 128) {
    m_lo = 0;
    m_hi = 0;
    return *this;
  }

  if ((abits + bbits) <= legbits) {  // The trivial case
    m_lo *= b.m_lo;
    return *this;
  }

  const uint64_t av[sublegs]{(m_lo & sublegmask), (m_lo >> sublegbits), (hi & sublegmask), (hi >> sublegbits)};
  const uint64_t bv[sublegs]{(b.m_lo & sublegmask), (b.m_lo >> sublegbits), (bhi & sublegmask), (bhi >> sublegbits)};
  uint64_t rv[sublegs]{};

  rv[0] = av[0] * bv[0];

  rv[1] = av[1] * bv[0];
  uint64_t scratch{rv[1] + av[0] * bv[1]};
  uint64_t carry{(rv[1] > scratch) ? UINT64_C(1) : UINT64_C(0)};
  rv[1] = scratch;

  rv[2] = av[2] * bv[0] + carry;  // 0xffff^2 + 1 < 0xffffffff, can't overflow
  scratch = rv[2] + av[1] * bv[1];
  carry = rv[2] > scratch ? 1 : 0;
  rv[2] = scratch + av[0] * bv[2];
  carry += scratch > rv[2] ? 1 : 0;

  rv[3] = av[3] * bv[0] + carry;
  scratch = rv[3] + av[2] * bv[1];
  carry = rv[3] > scratch ? 1 : 0;
  rv[3] = scratch + av[1] * bv[2];
  carry += scratch > rv[3] ? 1 : 0;
  scratch = rv[3] + av[0] * bv[3];
  carry += rv[3] > scratch ? 1 : 0;
  rv[3] = scratch;

  if (carry) {  // Shouldn't happen because of the checks above
    return *this;
  }

  m_lo = rv[0] + (rv[1] << sublegbits);
  carry = rv[1] >> sublegbits;
  carry += ((rv[1] << sublegbits) > m_lo) || (rv[0] > m_lo) ? UINT64_C(1) : UINT64_C(0);
  hi = rv[2] + (rv[3] << sublegbits) + carry;
  if (((rv[3] << sublegbits) > hi) || (rv[2] > hi) || (rv[3] >> sublegbits) /*|| hi & flagmask*/) {
    m_hi = hi;
    return *this;
  }
  m_hi = hi;
  return *this;
}

static void div_multi_leg(uint64_t* u, size_t m, uint64_t* v, size_t n, uint128_t& q, uint128_t& r) noexcept {
  /* D1, Normalization */
  uint64_t qv[sublegs]{};
  uint64_t d{(UINT64_C(1) << sublegbits) / (v[n - 1] + UINT64_C(1))};
  uint64_t carry{UINT64_C(0)};
  for (size_t i = 0; i < m; ++i) {
    u[i] = u[i] * d + carry;
    if (u[i] > sublegmask) {
      carry = u[i] >> sublegbits;
      u[i] &= sublegmask;
    } else {
      carry = UINT64_C(0);
    }
    assert(u[i] <= sublegmask);
  }
  if (carry) {
    u[m++] = carry;
    carry = UINT64_C(0);
  }
  for (size_t i = 0; i < n; ++i) {
    v[i] = v[i] * d + carry;
    if (v[i] > sublegmask) {
      carry = v[i] >> sublegbits;
      v[i] &= sublegmask;
    } else {
      carry = UINT64_C(0);
    }
    assert(v[i] < sublegmask);
  }
  assert(carry == UINT64_C(0));
  for (size_t j{m - n}; int64_t(j) >= 0; j--) {  // D3
    uint64_t qhat{((u[j + n] << sublegbits) + u[j + n - 1]) / v[n - 1]};
    uint64_t rhat{((u[j + n] << sublegbits) + u[j + n - 1]) % v[n - 1]};

    while (qhat > sublegmask || (rhat <= sublegmask && ((qhat * v[n - 2]) > ((rhat << sublegbits) + u[j + n - 2])))) {
      --qhat;
      rhat += v[n - 1];
    }
    carry = UINT64_C(0);
    uint64_t borrow{};
    for (size_t k = 0; k < n; ++k)  // D4
    {
      auto subend = qhat * v[k] + carry;
      carry = subend >> sublegbits;
      subend &= sublegmask;
      if (u[j + k] >= subend) {
        u[j + k] = u[j + k] - subend;
        borrow = UINT64_C(0);
      } else {
        if (u[j + k + 1] > 0) {
          --u[j + k + 1];
        } else {
          ++borrow;
        }
        u[j + k] = u[j + k] + sublegmask + 1 - subend;
        u[j + k] &= sublegmask;
      }
    }
    u[j + n] -= carry;
    qv[j] = qhat;
    if (borrow) {  // D5,D6
      --qv[j];
      carry = UINT64_C(0);
      for (size_t k = 0; k < n; ++k) {
        u[j + k] += v[k] + carry;
        if (u[j + k] > sublegmask) {
          carry = u[j + k] >> sublegbits;
          u[j + k] &= sublegmask;
        }
      }
      u[j + n] += carry;
    }
  }  // D7
  q = uint128_t((qv[3] << sublegbits) + qv[2], (qv[1] << sublegbits) + qv[0]);
  r = uint128_t((u[3] << sublegbits) + u[2], (u[1] << sublegbits) + u[0]);
  r /= d;
}

static void div_single_leg(uint64_t* u, uint64_t m, uint64_t v, uint128_t& q, uint128_t& r) noexcept {
  uint64_t qv[sublegs]{};
  for (size_t i{m - 1}; int64_t(i) >= 0; --i) {
    qv[i] = u[i] / v;
    if (i > 0) {
      u[i - 1] += ((u[i] % v) << sublegbits);
      u[i] = UINT64_C(0);
    } else {
      u[i] %= v;
    }
  }

  q = uint128_t((qv[3] << sublegbits) + qv[2], (qv[1] << sublegbits) + qv[0]);
  r = uint128_t((u[3] << sublegbits) + u[2], (u[1] << sublegbits) + u[0]);
}

void uint128_t::div(const uint128_t& b, uint128_t& q, uint128_t& r) const noexcept {
  r = uint128_t();
  q = uint128_t();

  assert(&q != this);
  assert(&r != this);
  assert(&q != &b);
  assert(&r != &b);

  q.zero();
  r.zero();
  if (b.isZero()) {
    return;
  }

  if (*this < b) {
    r = *this;
    return;
  }

  auto hi{m_hi};
  auto bhi{b.m_hi};
  if (hi == 0 && bhi == 0) {  // let the hardware do it
    assert(b.m_lo != 0);      // b.m_hi is 0 but b isn't or we didn't get here.
    q.m_lo = m_lo / b.m_lo;
    r.m_lo = m_lo % b.m_lo;
    return;
  }

  uint64_t u[sublegs + 2]{(m_lo & sublegmask), (m_lo >> sublegbits), (hi & sublegmask), (hi >> sublegbits), 0, 0};
  uint64_t v[sublegs]{(b.m_lo & sublegmask), (b.m_lo >> sublegbits), (bhi & sublegmask), (bhi >> sublegbits)};
  auto m{u[3] ? UINT64_C(4) : u[2] ? UINT64_C(3) : u[1] ? UINT64_C(2) : u[0] ? UINT64_C(1) : UINT64_C(0)};
  auto n{v[3] ? UINT64_C(4) : v[2] ? UINT64_C(3) : v[1] ? UINT64_C(2) : v[0] ? UINT64_C(1) : UINT64_C(0)};
  if (m == 0 || n == 0) {  // Shouldn't happen
    return;
  }
  if (n == 1) {
    return div_single_leg(u, m, v[0], q, r);
  }

  return div_multi_leg(u, m, v, n, q, r);
}

uint128_t& uint128_t::operator/=(const uint128_t& b) noexcept {
  uint128_t q{};
  uint128_t r{};
  div(b, q, r);
  std::swap(*this, q);
  return *this;
}

uint128_t& uint128_t::operator%=(const uint128_t& b) noexcept {
  uint128_t q{};
  uint128_t r{};
  div(b, q, r);
  std::swap(*this, r);
  return *this;
}

uint128_t& uint128_t::operator&=(const uint128_t& b) noexcept {
  auto hi{m_hi};
  hi &= b.m_hi;
  m_lo &= b.m_lo;
  m_hi = hi;
  return *this;
}

uint128_t& uint128_t::operator|=(const uint128_t& b) noexcept {
  auto hi{m_hi};
  hi ^= b.m_hi;
  m_hi = hi;
  m_lo ^= b.m_lo;
  return *this;
}

uint128_t& uint128_t::operator^=(const uint128_t& b) noexcept {
  auto hi{m_hi};
  hi ^= b.m_hi;
  m_hi = hi;
  m_lo ^= b.m_lo;
  return *this;
}

static constexpr size_t dec_array_size{5};
static constexpr size_t char_buf_size{41};  // 39 digits plus sign and trailing null

static void decimal_from_binary(std::array<uint64_t, dec_array_size>& d, uint64_t hi, uint64_t lo) {
  /* Coefficients are the values of 2^96, 2^64, and 2^32 divided into 8-digit
   * segments:
   * 2^96 =               79228,16251426,43375935,43950336
   * 2^64 =                         1844,67440737,09551616
   * 2^32 =                                    42,94967296
   */
  constexpr size_t coeff_array_size{dec_array_size - 1};
  constexpr uint64_t coeff_3[coeff_array_size]{79228, 16251426, 43375935, 43950336};
  constexpr uint64_t coeff_2[coeff_array_size]{0, 1844, 67440737, 9551616};
  constexpr uint64_t coeff_1[coeff_array_size]{0, 0, 42, 94967296};
  constexpr auto bin_mask{UINT64_C(0xFFFFFFFF)};
  constexpr auto dec_div{UINT64_C(100000000)};

  d[0] = lo & bin_mask;
  d[1] = (lo >> 32) & bin_mask;
  d[2] = hi & bin_mask;
  d[3] = (hi >> 32) & bin_mask;

  d[0] += coeff_3[3] * d[3] + coeff_2[3] * d[2] + coeff_1[3] * d[1];
  uint64_t q{d[0] / dec_div};
  d[0] %= dec_div;

  for (size_t i{1}; i < coeff_array_size; ++i) {
    const size_t j{coeff_array_size - i - 1};
    d[i] = q + coeff_3[j] * d[3] + coeff_2[j] * d[2] + coeff_1[j] * d[1];
    q = d[i] / dec_div;
    d[i] %= dec_div;
  }
  d[dec_array_size - 1] = q;
  return;
}

char* uint128_t::asCharBufR(char* const buf) const noexcept {
  if (isZero()) {
    sprintf(buf, "%d", 0);
    return buf;
  }
  std::array<uint64_t, dec_array_size> d{};
  decimal_from_binary(d, m_hi, m_lo);
  char* next{buf};

  bool trailing{false};
  for (uint32_t i{dec_array_size}; i; --i) {
    if (d[i - 1] || trailing) {
      if (trailing) {
        next += sprintf(next, "%8.8" PRIu64, d[i - 1]);
      } else {
        next += sprintf(next, "%" PRIu64, d[i - 1]);
      }

      trailing = true;
    }
  }

  return buf;
}

std::ostream& operator<<(std::ostream& stream, const uint128_t& a) noexcept {
  std::array<char, char_buf_size> buf{};
  stream << a.asCharBufR(buf.data());
  return stream;
}

// clang-format off
bool operator==(const uint128_t& a, const uint128_t& b) noexcept { return a.cmp(b) == 0; }
bool operator!=(const uint128_t& a, const uint128_t& b) noexcept { return a.cmp(b) != 0; }
bool operator< (const uint128_t& a, const uint128_t& b) noexcept { return a.cmp(b)  < 0; }
bool operator> (const uint128_t& a, const uint128_t& b) noexcept { return a.cmp(b)  > 0; }
bool operator<=(const uint128_t& a, const uint128_t& b) noexcept { return a.cmp(b) <= 0; }
bool operator>=(const uint128_t& a, const uint128_t& b) noexcept { return a.cmp(b) >= 0; }

uint128_t operator+ (uint128_t a, const uint128_t& b) noexcept { a += b; return a; }
uint128_t operator- (uint128_t a, const uint128_t& b) noexcept { a -= b; return a; }
uint128_t operator* (uint128_t a, const uint128_t& b) noexcept { a *= b; return a; }
uint128_t operator/ (uint128_t a, const uint128_t& b) noexcept { a /= b; return a; }
uint128_t operator% (uint128_t a, const uint128_t& b) noexcept { a %= b; return a; }
uint128_t operator& (uint128_t a, const uint128_t& b) noexcept { a &= b; return a; }
uint128_t operator| (uint128_t a, const uint128_t& b) noexcept { a |= b; return a; }
uint128_t operator^ (uint128_t a, const uint128_t& b) noexcept { a ^= b; return a; }

uint128_t operator<<(uint128_t a, uint32_t b) noexcept { a <<= b; return a; }
uint128_t operator>>(uint128_t a, uint32_t b) noexcept { a >>= b; return a; }
// clang-format on

#endif  // _MSC_VER || TEST_MSC_VER
