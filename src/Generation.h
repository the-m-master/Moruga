#define M_E 2.7182818284590452354

enum {
  NBITS = 12,        // Construct 12 bit tables
  TOP = 1 << NBITS,  // Top value
  HTOP = TOP / 2     // Half way value
};

[[nodiscard]] static auto inverf(const double x) noexcept -> double {
  double p;
  double t{log(fma(x, 0.0 - x, 1.0))};
  if (fabs(t) > 6.125) {
    // clang-format off
    p =            3.03697567e-10;
    p = fma(p, t,  2.93243101e-8);
    p = fma(p, t,  1.22150334e-6);
    p = fma(p, t,  2.84108955e-5);
    p = fma(p, t,  3.93552968e-4);
    p = fma(p, t,  3.02698812e-3);
    p = fma(p, t,  4.83185798e-3);
    p = fma(p, t, -2.64646143e-1);
    p = fma(p, t,  8.40016484e-1);
  } else {
    p =            5.43877832e-9;
    p = fma(p, t,  1.43286059e-7);
    p = fma(p, t,  1.22775396e-6);
    p = fma(p, t,  1.12962631e-7);
    p = fma(p, t, -5.61531961e-5);
    p = fma(p, t, -1.47697705e-4);
    p = fma(p, t,  2.31468701e-3);
    p = fma(p, t,  1.15392562e-2);
    p = fma(p, t, -2.32015476e-1);
    p = fma(p, t,  8.86226892e-1);
    // clang-format on
  }
  return p * x;
}

class Squash_t final {
public:
  explicit Squash_t(const double a) noexcept {
#if 1
    //                              x  -x
    //                            e - e
    // Sigmoid: y(x) = tanh(x) = ---------
    //                             x   -x
    //                            e + e
    //
    for (uint32_t n{0}; n < TOP; ++n) {
      const double x{(double(n) - (HTOP - 1)) / a};  // Best when a=598.0 on enwik9
      const auto epx{pow(M_E, x)};
      const auto enx{pow(M_E, -x)};
      const auto tmp{(epx - enx) / (epx + enx)};
      _table[n] = tmp + 1.0;
    }
    const double offset{_table[0]};
    const double scale{double(TOP - 1) / (_table[TOP - 1] - offset)};
    for (uint32_t n{0}; n < TOP; ++n) {
      double tmp{(_table[n] - offset) * scale};
      _squash[n] = static_cast<uint16_t>(std::lround(tmp));
    }
#elif 0
    //
    // Sigmoid: y(x) = erf(0.5 * sqrt(pi) * x)
    //          y(x) = erf(x/a)
    //
    for (uint32_t n{0}; n < TOP; ++n) {
      const double in{(double(n) - (HTOP - 1)) / a};  // Best when a=756.1 on enwik9
      double tmp{std::erf(in)};
      _table[n] = (1.0 + tmp) / 2.0;

#  if !defined(NDEBUG)  // Small validation: 'inverf(erf(x)) = x'
      tmp = inverf(tmp);
      assert(std::fabs(tmp - in) < 1e-6);
#  endif
    }
    const double offset{_table[0]};
    const double scale{double(TOP - 1) / (_table[TOP - 1] - offset)};
    for (uint32_t n{0}; n < TOP; ++n) {
      double tmp{(_table[n] - offset) * scale};
      _squash[n] = static_cast<uint16_t>(std::lround(tmp));
    }
#elif 0
    //                      1
    // Sigmoid:  y(x) = ----------
    //                       -ax
    //                   1 + e
    //
    for (uint32_t n{0}; n < TOP; ++n) {
      _table[n] = 1.0 / (1.0 + exp(((HTOP - 1) - double(n)) / a));  // Best when a=315 on enwik9
    }
    const double offset{_table[0]};
    const double scale{double(TOP - 1) / (_table[TOP - 1] - offset)};
    for (uint32_t n{0}; n < TOP; ++n) {
      double tmp{(_table[n] - offset) * scale};
      _squash[n] = static_cast<uint16_t>(std::lround(tmp));
    }
#else
    (void)a;  // Not adjustable

    static constexpr std::array<uint16_t, 33> ts{{1,    2,    3,    6,    10,   16,   27,   45,    //
                                                  73,   120,  194,  310,  488,  747,  1101, 1546,  //
                                                  2047, 2549, 2994, 3348, 3607, 3785, 3901, 3975,  //
                                                  4022, 4050, 4068, 4079, 4085, 4089, 4092, 4093, 4094}};
    for (int32_t n{-(HTOP - 1)}; n < HTOP; ++n) {
      const int32_t w{n & 127};
      const int32_t d{(n >> 7) + 16};
      _squash[n + 2047] = static_cast<uint16_t>((ts[d] * (127 - w) + ts[d + 1] * w + 64) / 127);
    }
#endif

#if 0
    {
      File_t out("Squash.txt", "wb");
      for (uint32_t n{0}; n < HTOP; ++n) {
        fprintf(out, "%4d,", _squash[n]);
        if (!((n + 1) % 16)) {
          fprintf(out, "\n");
        }
      }
    }
#endif
#if 0
    {
      File_t out("Squash.csv", "wb");
      for (uint32_t n{0}; n < TOP; ++n) {
        fprintf(out, "%d,\n", _squash[n]);
      }
    }
#endif
  }

  constexpr auto operator()(const int32_t pr) const noexcept -> uint32_t {  // Conversion from -2048..2047 (clamped) into 0..4095
    if (pr <= ~0x7FF) {
      return 0x000;
    }
    if (pr >= 0x7FF) {
      return 0xFFF;
    }
    if (pr >= 0x000) {
      return 0xFFF - _squash[static_cast<size_t>(pr ^ 0x7FF)];  // 0x7FF - pr
    }
    return _squash[static_cast<size_t>(pr + 0x800)];
  }

  Squash_t(const Squash_t&) = delete;
  Squash_t(Squash_t&&) = delete;
  auto operator=(const Squash_t&) -> Squash_t& = delete;
  auto operator=(Squash_t&&) -> Squash_t& = delete;

private:
  std::array<double, TOP> _table{};
  std::array<uint16_t, TOP> _squash{};
};

static Squash_t* _squash{nullptr};

class Stretch_t final {
public:
  explicit Stretch_t(const double a) noexcept {
#if 0
    //
    //             -1    1       1 + x
    // Sigmoid: tanh  = --- ln( ------- )
    //                   2       1 - x
    //
    for (uint32_t n{0}; n < HTOP; ++n) {
      const auto x{double(n) / double(HTOP)};
      _table[n] = log((1.0 + x) / (1.0 - x)) * a;
    }
    _table[HTOP] = HTOP - 1;
    for (uint32_t n{0}; n < _stretch.size(); ++n) {
      const int32_t tmp{std::lround(_table[n])};
      _stretch[n] = static_cast<int16_t>(std::clamp(tmp, 0, HTOP - 1));
    }
#elif 1
    // Inverse of sigmoid: y(x) = inverf(x)
    //
    for (uint32_t n{0}; n < HTOP; ++n) {
      _table[n] = inverf(double(n) / double(HTOP)) * a;  // Best when a=738.2 on enwik9
    }
    _table[HTOP - 1] = HTOP - 1;
    for (uint32_t n{0}; n < _stretch.size(); ++n) {
      const int32_t tmp{std::lround(_table[n])};
      _stretch[n] = static_cast<int16_t>(std::clamp(tmp, 0, HTOP - 1));
    }
#else
    (void)a;  // Not adjustable

    // Inverse of sigmoid
    //
    uint32_t pi{0};
    for (int32_t n{0}; n < TOP; ++n) {
      const uint32_t i{(*_squash)(n - HTOP)};  // Conversion from -2048..2047 into 0..4095
      for (uint32_t j{pi}; j <= i; ++j) {
        _stretch[j] = static_cast<int16_t>(n - HTOP);
      }
      pi = i + 1;
    }
    _stretch[TOP - 1] = HTOP - 1;
#endif

#if 0
     File_t out("Stretch.txt", "wb");
     for (uint32_t n{0}; n < _stretch.size(); ++n) {
       fprintf(out, "%4d,", _stretch[n]);
       if (!((n + 1) % 16)) {
         fprintf(out, "\n");
       }
     }
#endif
#if 0
    {
      File_t out("Stretch.csv", "wb");
      for (uint32_t n{0}; n < _stretch.size(); ++n) {
        fprintf(out, "%d,\n", _stretch[n]);
      }
    }
#endif
  }

  constexpr auto operator()(const uint32_t pr) const noexcept -> int32_t {  // Conversion from 0..4095 into -2048..2047
    assert(pr < TOP);
    if (pr <= 0x7FF) {
      return -_stretch[static_cast<size_t>(pr ^ 0x7FF)];  // 0x7FF - pr
    }
    return _stretch[static_cast<size_t>(pr & 0x7FF)];  // pr - 0x800
  }

  Stretch_t(const Stretch_t&) = delete;
  Stretch_t(Stretch_t&&) = delete;
  auto operator=(const Stretch_t&) -> Stretch_t& = delete;
  auto operator=(Stretch_t&&) -> Stretch_t& = delete;

private:
  std::array<double, TOP> _table{};
  std::array<int16_t, HTOP> _stretch{};
};

static Stretch_t* _stretch{nullptr};

static auto Squash(const int32_t pr) noexcept -> uint32_t {  // Conversion from -2048..2047 (clamped) into 0..4095
  return (*_squash)(pr);
}

static auto Stretch(const uint32_t pr) noexcept -> int32_t {  // Conversion from 0..4095 into -2048..2047
  return (*_stretch)(pr);
}

static auto Stretch256(const int32_t pr) noexcept -> int32_t {  // Conversion from 0..1048575 into -2048..2047
  return Stretch(static_cast<uint32_t>(pr) / 256);
}
