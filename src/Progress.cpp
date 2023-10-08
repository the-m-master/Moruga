/* Progress, simple progress bar handling
 *
 * Copyright (c) 2019-2023 Marwijn Hessel
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
#include "Progress.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include "filters/filter.h"
#include "iMonitor.h"

#if defined(__linux__) || defined(__APPLE__)
#  include <pthread.h>
#  include <sys/ioctl.h>
#  include <sys/resource.h>
#  include <unistd.h>
#elif defined(_WIN32) || defined(_WIN64) || defined(__CYGWIN__) || defined(_MSC_VER)
#  include <windows.h>  // Must be done first! Otherwise #error: "No Target Architecture" in VS2019
//
#  include <psapi.h>
#endif

#if defined(_MSC_VER)
#  define pclose _pclose
#  define popen _popen
#endif

iMonitor_t::~iMonitor_t() noexcept = default;

namespace {
  using namespace std::literals;

  constexpr auto POW10_2{INT64_C(100)};
  constexpr auto POW10_6{INT64_C(1000000)};
  constexpr auto MIN_TIME{INT64_C(0)};
  constexpr auto MAX_TIME{INT64_C(5999) * POW10_6};
  constexpr auto MIN_BAR_LENGTH{2};
  constexpr auto MAX_BAR_LENGTH{256};

  constexpr std::array<const std::string_view, 4> SPEED_DIMS{{"B/s"sv, "KiB/s"sv, "MiB/s"sv, "GiB/s"sv}};
  constexpr std::array<const std::string_view, 3> MEM_DIMS{{"KiB"sv, "MiB"sv, "GiB"sv}};
  constexpr std::array<const char, 4> ANIMATION{{'\\', '|', '/', '-'}};

  // static_cast<int32_t>(ceil(log(double(layoutLength | 1)) / log(10.0)))
  auto GetDigits(int64_t number) noexcept -> int32_t {
    ++number;
    int32_t ndigits{0};
    for (int64_t value{1}; value < number; value *= 10) {
      ++ndigits;
    }
    return ndigits;
  }

#if defined(__linux__) || defined(__APPLE__)

  auto GetConsoleColumns() noexcept -> int32_t {
    struct winsize csbi;
    memset(&csbi, 0, sizeof(csbi));
    ioctl(STDIN_FILENO, TIOCGWINSZ, &csbi);
    const int32_t columns{csbi.ws_col};
    return columns;
  }

  auto GetMemoryUseInKiB() noexcept -> uint32_t {
    struct rusage rUsage;
    memset(&rUsage, 0, sizeof(rUsage));
    getrusage(RUSAGE_SELF, &rUsage);
    return static_cast<uint32_t>(rUsage.ru_maxrss);  // Maximum resident set size utilised in KiB
  }

#else

  auto getConsoleScreenBufferInfo(void** handler = nullptr) noexcept -> const CONSOLE_SCREEN_BUFFER_INFO* {
    if (auto* const handle{GetStdHandle(STD_OUTPUT_HANDLE)}; nullptr != handle) {
      static CONSOLE_SCREEN_BUFFER_INFO csbi{{0, 0}, {0, 0}, 0, {0, 0, 0, 0}, {0, 0}};
      if (0 != GetConsoleScreenBufferInfo(handle, &csbi)) {
        if (nullptr != handler) {
          *handler = handle;
        }
        return &csbi;
      }
    }
    return nullptr;  // Failure
  }

  auto GetConsoleRowsColumns() noexcept -> std::pair<bool, int32_t> {
    const char* const columns_str{getenv("COLUMNS")};
    if (columns_str) {
      return {true, std::stoi(columns_str, nullptr, 10)};
    }
    const auto* const csbi{getConsoleScreenBufferInfo()};
    if (nullptr == csbi) {
      return {false, 0};  // Failure
    }
    return {true, csbi->srWindow.Right - csbi->srWindow.Left + 1};
  }

  auto GetConsoleColumns() noexcept -> int32_t {
    if (const auto& result{GetConsoleRowsColumns()}; result.first) {
      return result.second;
    }
    try {
      std::array<char, 4> result{{'8', '0', 0, 0}};
      if (std::unique_ptr<FILE, decltype(&pclose)> const pipe(popen("tput cols 2>&1", "r"), pclose); nullptr != pipe) {
        if (const auto* const ptr{fgets(result.data(), int(result.size()), pipe.get())}; nullptr != ptr) {
          return std::stoi(result.data(), nullptr, 10);
        }
      }
    } catch (...) {
      // Ignore potential failures from 'popen'.
      // This might happen when the pipe is redirected some how...
      // or 'tput' does not exist...
    }
    return 0;  // Failure
  }

  auto GetMemoryUseInKiB() noexcept -> uint32_t {
    PROCESS_MEMORY_COUNTERS rUsage;
    GetProcessMemoryInfo(GetCurrentProcess(), &rUsage, sizeof(rUsage));
    return static_cast<uint32_t>(((rUsage.PeakPagefileUsage / SIZE_T(512)) + 1) / 2);  // in KiB
  }

#endif

  uint32_t peakMemoryUse_{0};  // in KiB

  struct Status_t {
    volatile uint32_t nFilters;

    volatile uint32_t nBMP;
    volatile uint32_t nBZ2;
    volatile uint32_t nCAB;
    volatile uint32_t nELF;
    volatile uint32_t nEXE;
    volatile uint32_t nGIF;
    volatile uint32_t nGZP;
    volatile uint32_t nPBM;
    volatile uint32_t nPDF;
    volatile uint32_t nPKZ;
    volatile uint32_t nPNG;
    volatile uint32_t nSGI;
    volatile uint32_t nTGA;
    volatile uint32_t nTIF;
    volatile uint32_t nWAV;
  };

  Status_t state_;

  void FiltersToString(std::string& filters, uint32_t count, const std::string_view text) noexcept {
    if (count > 0) {
      if (filters.length() > 0) {
        filters.append(", ");
      }
      filters += text;
      if (count > 1) {
        std::array<char, 16> tmp{};
        snprintf(tmp.data(), tmp.size(), ":%u", count);
        filters.append(tmp.data());
      }
    }
  }

  void ProgressBar(const volatile TraceProgress_t* const tracer) noexcept {
    const auto end_time{std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count()};
    const auto deltaTime{end_time - tracer->start};
    if (deltaTime > MIN_TIME) {
      const auto inBytes{tracer->monitor.InputLength()};
      const auto outBytes{tracer->monitor.OutputLength()};
      const auto workLength{tracer->monitor.WorkLength()};

      const int64_t workPosition{tracer->encode ? inBytes : outBytes};

      auto speed{(workPosition * POW10_6) / deltaTime};
      std::string_view speed_dim{SPEED_DIMS[0]};  // B/s
      if (speed > 9999999999) {                   //
        speed_dim = SPEED_DIMS[3];                // GiB/s
        speed = ((speed / 536870912) + 1) / 2;    // 1/1073741824
      } else if (speed > 9999999) {               //
        speed_dim = SPEED_DIMS[2];                // MiB/s
        speed = ((speed / 524288) + 1) / 2;       // 1/1048576
      } else if (speed > 9999) {                  //
        speed_dim = SPEED_DIMS[1];                // KiB/s
        speed = ((speed / 512) + 1) / 2;          // 1/1024
      }

      auto mem_use{GetMemoryUseInKiB()};
      peakMemoryUse_ = (std::max)(peakMemoryUse_, mem_use);
      std::string_view mem_dim{MEM_DIMS[0]};     // KiB
      if (mem_use > 9999999) {                   //
        mem_dim = MEM_DIMS[2];                   // GiB
        mem_use = ((mem_use / 524288) + 1) / 2;  // 1/1048576
      } else if (mem_use > 9999) {               //
        mem_dim = MEM_DIMS[1];                   // MiB
        mem_use = ((mem_use / 512) + 1) / 2;     // 1/1024
      }

      int64_t dT{MIN_TIME};
      if (tracer->isRunning) {
        if (workPosition > dT) {
          dT = int64_t((static_cast<double>(workLength) * static_cast<double>(deltaTime)) / static_cast<double>(workPosition)) - deltaTime;
        }
      } else {
        dT = deltaTime;
      }
      const auto ms{std::clamp(dT, MIN_TIME, MAX_TIME)};
      const auto mm{static_cast<int32_t>(ms / (INT64_C(60) * POW10_6))};
      const auto ss{static_cast<int32_t>((ms - (mm * INT64_C(60) * POW10_6)) / POW10_6)};

      const auto columns{GetConsoleColumns()};
      int32_t length{28 + 3 + tracer->digits + tracer->digits + 4 + 4 + 6 + 3};
      length = (std::max)(0, columns - length);

      const auto barLength{std::clamp(length, MIN_BAR_LENGTH, MAX_BAR_LENGTH)};
      const auto buzy{(workPosition * barLength) / workLength};

      std::array<char, MAX_BAR_LENGTH> progress;
      char* __restrict dst{progress.data()};
      for (int32_t n{0}; n < barLength; ++n) {
        *dst++ = (n < buzy) ? '#' : '.';
      }
      *dst = '\0';

      if ((buzy >= 0) && (buzy < barLength)) {
        static uint32_t art{0};
        progress[static_cast<size_t>(buzy)] = ANIMATION[art];
        if (++art >= ANIMATION.size()) {
          art = 0;
        }
      }

      length = (std::max)(0, length - barLength);
      const std::string filler(static_cast<size_t>(length), ' ');

      fprintf(stdout, "%s in/out %*" PRId64 "/%*" PRId64 "%s %4u %s %4" PRId64 " %-5s %02d:%02d [%s] %3" PRId64 "%%",
              tracer->workType,           //
              tracer->digits, inBytes,    //
              -tracer->digits, outBytes,  //
              filler.c_str(),             //
              mem_use, mem_dim.data(),    //
              speed, speed_dim.data(),    //
              mm, ss,                     //
              progress.data(),            //
              ((workPosition * POW10_2) / workLength));
      if (state_.nFilters) {
        std::string filters;
        FiltersToString(filters, state_.nBMP, "BMP"sv);
        FiltersToString(filters, state_.nBZ2, "BZ2"sv);
        FiltersToString(filters, state_.nCAB, "CAB"sv);
        FiltersToString(filters, state_.nELF, "ELF"sv);
        FiltersToString(filters, state_.nEXE, "EXE"sv);
        FiltersToString(filters, state_.nGIF, "GIF"sv);
        FiltersToString(filters, state_.nGZP, "GZ"sv);  // GNU zip
        FiltersToString(filters, state_.nPBM, "PBM"sv);
        FiltersToString(filters, state_.nPDF, "PDF"sv);
        FiltersToString(filters, state_.nPKZ, "PKZ"sv);  // PKZip
        FiltersToString(filters, state_.nPNG, "PNG"sv);
        FiltersToString(filters, state_.nSGI, "SGI"sv);
        FiltersToString(filters, state_.nTGA, "TGA"sv);
        FiltersToString(filters, state_.nTIF, "TIF"sv);
        FiltersToString(filters, state_.nWAV, "WAV"sv);
        fprintf(stdout, "\r\n%*s[Filter: %.*s]\r", tracer->digits + tracer->digits + length + 39, " ", barLength, filters.c_str());

        static constexpr std::string_view cursor_up_one_line = "\033[1A"sv;
#if defined(__linux__) || defined(__APPLE__)
        fputs(cursor_up_one_line.data(), stdout);
#else
        const char* const term{getenv("TERM")};
        if (nullptr != term) {
          fputs(cursor_up_one_line.data(), stdout);
        } else {
          void* handle{nullptr};
          if (const auto* const csbi{getConsoleScreenBufferInfo(&handle)}; nullptr != csbi) {
            COORD coord{.X = csbi->dwCursorPosition.X, .Y = SHORT(csbi->dwCursorPosition.Y - 1)};
            if (coord.Y < csbi->srWindow.Top) {
              coord.Y = csbi->srWindow.Top;
            }
            SetConsoleCursorPosition(handle, coord);
          }
        }
#endif
      } else {
        fputc('\r', stdout);
      }
      fflush(stdout);
    }
  }

  void MonitorWorker(const volatile TraceProgress_t* const tracer) noexcept {
    assert(tracer);
#if !defined(_MSC_VER) && !defined(__APPLE__)
    pthread_setname_np(pthread_self(), __FUNCTION__);
#endif

    for (auto count{1}; tracer->isRunning; ++count) {
      if (count >= 5) {  // Do every 5*50ms = 250ms something...
        count = 1;
        ProgressBar(tracer);
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
  }
};  // namespace

Progress_t::Progress_t(std::string_view workType, bool encode, const iMonitor_t& monitor) noexcept
    : _tracer{.isRunning = true,
              .workType{workType[0], workType[1], workType[2], '\0'},
              .encode = encode,
              .digits = GetDigits(monitor.LayoutLength()),
              .monitor = monitor,
              .start = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count()},  //
      _monitor_worker{MonitorWorker, &_tracer} {
  memset(&state_, 0, sizeof(state_));
  ProgressBar(&_tracer);
}

Progress_t::~Progress_t() noexcept {
  _tracer.isRunning = false;
  _monitor_worker.join();
  ProgressBar(&_tracer);  // Flush!
  fputc('\n', stdout);
  fflush(stdout);
}

auto Progress_t::PeakMemoryUse() noexcept -> uint32_t {
  return peakMemoryUse_;
}

void Progress_t::FoundType(const Filter& type) noexcept {
  state_.nFilters = state_.nFilters + 1;

  switch (type) {  // clang-format off
    case Filter::BMP: state_.nBMP = state_.nBMP + 1; break;
    case Filter::BZ2: state_.nBZ2 = state_.nBZ2 + 1; break;
    case Filter::CAB: state_.nCAB = state_.nCAB + 1; break;
    case Filter::ELF: state_.nELF = state_.nELF + 1; break;
    case Filter::EXE: state_.nEXE = state_.nEXE + 1; break;
    case Filter::GIF: state_.nGIF = state_.nGIF + 1; break;
    case Filter::GZP: state_.nGZP = state_.nGZP + 1; break;
    case Filter::PBM: state_.nPBM = state_.nPBM + 1; break;
    case Filter::PDF: state_.nPDF = state_.nPDF + 1; break;
    case Filter::PKZ: state_.nPKZ = state_.nPKZ + 1; break;
    case Filter::PNG: state_.nPNG = state_.nPNG + 1; break;
    case Filter::SGI: state_.nSGI = state_.nSGI + 1; break;
    case Filter::TGA: state_.nTGA = state_.nTGA + 1; break;
    case Filter::TIF: state_.nTIF = state_.nTIF + 1; break;
    case Filter::WAV: state_.nWAV = state_.nWAV + 1; break;

    case Filter::NOFILTER:
    default:
      break;
  }  // clang-format on
}

void Progress_t::Cancelled(const Filter& type) noexcept {
  if (state_.nFilters > 0) {
    state_.nFilters = state_.nFilters - 1;
  }

  switch (type) {  // clang-format off
    case Filter::BMP: if (state_.nBMP > 0) { state_.nBMP = state_.nBMP - 1; } break;
    case Filter::BZ2: if (state_.nBZ2 > 0) { state_.nBZ2 = state_.nBZ2 - 1; } break;
    case Filter::CAB: if (state_.nCAB > 0) { state_.nCAB = state_.nCAB - 1; } break;
    case Filter::ELF: if (state_.nELF > 0) { state_.nELF = state_.nELF - 1; } break;
    case Filter::EXE: if (state_.nEXE > 0) { state_.nEXE = state_.nEXE - 1; } break;
    case Filter::GIF: if (state_.nGIF > 0) { state_.nGIF = state_.nGIF - 1; } break;
    case Filter::GZP: if (state_.nGZP > 0) { state_.nGZP = state_.nGZP - 1; } break;
    case Filter::PBM: if (state_.nPBM > 0) { state_.nPBM = state_.nPBM - 1; } break;
    case Filter::PDF: if (state_.nPDF > 0) { state_.nPDF = state_.nPDF - 1; } break;
    case Filter::PKZ: if (state_.nPKZ > 0) { state_.nPKZ = state_.nPKZ - 1; } break;
    case Filter::PNG: if (state_.nPNG > 0) { state_.nPNG = state_.nPNG - 1; } break;
    case Filter::SGI: if (state_.nSGI > 0) { state_.nSGI = state_.nSGI - 1; } break;
    case Filter::TGA: if (state_.nTGA > 0) { state_.nTGA = state_.nTGA - 1; } break;
    case Filter::TIF: if (state_.nTIF > 0) { state_.nTIF = state_.nTIF - 1; } break;
    case Filter::WAV: if (state_.nWAV > 0) { state_.nWAV = state_.nWAV - 1; } break;

    case Filter::NOFILTER:
    default:
      break;
  }  // clang-format on
}
