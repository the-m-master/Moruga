/* Progress, simple progress bar handling
 *
 * Copyright (c) 2019-2021 Marwijn Hessel
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
 * along with this program; see the file LICENSE.
 * If not, see <https://www.gnu.org/licenses/>
 */
#include "Progress.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include "filters/filter.h"
#include "iMonitor.h"

#if defined(__linux__)
#include <bits/types/struct_rusage.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <unistd.h>
#elif defined(_WIN32) || defined(_WIN64) || defined(__CYGWIN__) || defined(_MSC_VER)
#include <windows.h>  // Must be done first! Otherwise #error: "No Target Architecture" in VS2019
//
#include <psapi.h>
#endif

iMonitor_t::~iMonitor_t() noexcept = default;

// int32_t(ceil(log(double(layoutLength | 1)) / log(10.0)))
static auto digits(int64_t number) noexcept -> int32_t {
  number = (number < 2) ? 2 : number;
  int32_t ndigits{0};
  for (int64_t value{1}; value < number; value *= 10) {
    ++ndigits;
  }
  return ndigits;
}

#if defined(__linux__)

static void consoleRowCol(uint32_t& rows, uint32_t& columns) noexcept {
  struct winsize csbi;
  ioctl(STDIN_FILENO, TIOCGWINSZ, &csbi);
  columns = csbi.ws_col;
  rows = csbi.ws_row;
}

static auto consoleCol() noexcept -> uint32_t {
  uint32_t rows;
  uint32_t columns;
  consoleRowCol(rows, columns);
  return columns;
}
static auto memoryUseKiB() noexcept -> uint32_t {
  struct rusage rUsage;
  getrusage(RUSAGE_SELF, &rUsage);
#if defined(__APPLE__) && defined(__MACH__)
  return uint32_t(rUsage.ru_maxrss / UINT64_C(1024));  // in KiB;
#else
  return uint32_t(rUsage.ru_maxrss);  // in KiB
#endif
}

#else

static auto consoleRowCol(uint32_t& rows, uint32_t& columns) noexcept -> bool {
  const char* const columns_str{getenv("COLUMNS")};
  const char* const rows_str{getenv("LINES")};
  if (columns_str && rows_str) {
    columns = uint32_t(strtoul(columns_str, nullptr, 10));
    rows = uint32_t(strtoul(rows_str, nullptr, 10));
  } else {
    void* const handle{GetStdHandle(STD_OUTPUT_HANDLE)};
    if (nullptr == handle) {
      return false;  // Failure
    }
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (0 == GetConsoleScreenBufferInfo(handle, &csbi)) {
      return false;  // Failure
    }
    columns = uint32_t(csbi.srWindow.Right - csbi.srWindow.Left + 1);
    rows = uint32_t(csbi.srWindow.Bottom - csbi.srWindow.Top + 1);
  }
  return true;
}

static auto consoleCol() noexcept -> uint32_t {
  uint32_t rows;
  uint32_t columns{80};
  if (!consoleRowCol(rows, columns)) {
    static std::array<char, 4> result{{'8', '0', 0, 0}};
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen("tput cols", "r"), pclose);
    if (nullptr != pipe) {
      const char* ptr{fgets(&result[0], result.size(), pipe.get())};
      if (nullptr != ptr) {
        columns = strtoul(&result[0], nullptr, 10);
      }
    }
  }
  return columns;
}

static auto memoryUseKiB() noexcept -> uint32_t {
  PROCESS_MEMORY_COUNTERS rUsage;
  GetProcessMemoryInfo(GetCurrentProcess(), &rUsage, sizeof(rUsage));
  return uint32_t(rUsage.PeakPagefileUsage / UINT64_C(1024));  // in KiB
}

#endif

static uint32_t g_peakMemoryUse{0};  // in KiB

static volatile uint32_t g_nFilter{0};

static volatile uint32_t g_nBMP{0};
static volatile uint32_t g_nDCM{0};
static volatile uint32_t g_nELF{0};
static volatile uint32_t g_nEXE{0};
static volatile uint32_t g_nGIF{0};
static volatile uint32_t g_nPBM{0};
static volatile uint32_t g_nPDF{0};
static volatile uint32_t g_nPKZ{0};
static volatile uint32_t g_nPNG{0};
static volatile uint32_t g_nSGI{0};
static volatile uint32_t g_nTGA{0};
static volatile uint32_t g_nTIF{0};
static volatile uint32_t g_nWAV{0};

static void FiltersToString(std::string& filters, uint32_t count, const std::string& text) noexcept {
  if (count) {
    if (filters.length() > 0) {
      filters += ", ";
    }
    filters += text;
    if (count > 1) {
      std::array<char, 16> tmp;
      snprintf(&tmp[0], tmp.size(), ":%u", count);
      filters.append(&tmp[0]);
    }
  }
}

static void ProgressBar(const volatile TraceProgress_t* const tracer) noexcept {
  static constexpr auto POW10_2{INT64_C(100)};
  static constexpr auto POW10_6{INT64_C(1000000)};
  static constexpr auto min_time{INT64_C(0)};
  static constexpr auto max_time{INT64_C(5999) * POW10_6};

  const auto end_time{std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count()};
  const auto deltaTime{end_time - tracer->start};
  if (deltaTime > min_time) {
    const auto inBytes{tracer->monitor.inputLength()};
    const auto outBytes{tracer->monitor.outputLength()};
    const auto workLength{tracer->monitor.workLength()};

    uint32_t rows{0};
    uint32_t columns{tracer->columns};  // In case of failure
    consoleRowCol(rows, columns);
    const int64_t workPosition{tracer->encode ? inBytes : outBytes};

    auto speed{uint32_t((workPosition * POW10_6) / (deltaTime * INT64_C(1024)))};
    char speedC{'K'};                     // KiB
    if (speed > 9999) {                   //
      speedC = 'M';                       // MiB
      speed = ((speed / 512) + 1) / 2;    //
      if (speed > 9999) {                 //
        speedC = 'G';                     // GiB
        speed = ((speed / 512) + 1) / 2;  //
      }
    }

    auto memUse{memoryUseKiB()};
    g_peakMemoryUse = (std::max)(g_peakMemoryUse, memUse);
    char memC{'K'};                         // KiB
    if (memUse > 9999) {                    //
      memC = 'M';                           // MiB
      memUse = ((memUse / 512) + 1) / 2;    //
      if (memUse > 9999) {                  //
        memC = 'G';                         // GiB
        memUse = ((memUse / 512) + 1) / 2;  //
      }
    }

    const auto ms{(workPosition > min_time) ? std::clamp(((workLength * deltaTime) / workPosition) - deltaTime, min_time, max_time) : min_time};
    const auto mm{int32_t(ms / (INT64_C(60) * POW10_6))};
    const auto ss{int32_t((ms - (mm * INT64_C(60) * POW10_6)) / POW10_6)};

    int32_t length{28 + 3 + tracer->digits + tracer->digits + 4 + 4 + 6 + 3};
    length = (std::max)(0, int32_t(columns) - length);

    const auto barLength{std::clamp(uint32_t(length), UINT32_C(2), UINT32_C(256))};
    const auto buzy{uint32_t((workPosition * barLength) / workLength)};

    std::string progress;
    for (uint32_t n{0}; n < barLength; ++n) {
      progress += (n < buzy) ? '#' : '.';
    }
    if (buzy < barLength) {
      static constexpr std::array<char, 4> animation{{'\\', '|', '/', '-'}};
      static uint8_t art{0};
      progress[buzy] = animation[art++];
      if (art >= sizeof(animation)) {
        art = 0;
      }
    }

    length = (std::max)(0, length - int32_t(progress.length()));

    std::string filler;
    for (auto n{0}; n < length; ++n) {
      filler += ' ';
    }

    fprintf(stdout, "%s in/out %*" PRId64 "/%*" PRId64 "%s %4u %ciB %4u %ciB/s %02d:%02d [%s] %3" PRId64 "%%",
            tracer->workType,           //
            tracer->digits, inBytes,    //
            -tracer->digits, outBytes,  //
            filler.c_str(),             //
            memUse, memC,               //
            speed, speedC,              //
            mm, ss,                     //
            progress.c_str(),           //
            ((workPosition * POW10_2) / workLength));
    if (g_nFilter) {
      std::string filters;
      FiltersToString(filters, g_nBMP, "BMP");
      FiltersToString(filters, g_nDCM, "DCM");
      FiltersToString(filters, g_nELF, "ELF");
      FiltersToString(filters, g_nEXE, "EXE");
      FiltersToString(filters, g_nGIF, "GIF");
      FiltersToString(filters, g_nPBM, "PBM");
      FiltersToString(filters, g_nPDF, "PDF");
      FiltersToString(filters, g_nPKZ, "PKZ");
      FiltersToString(filters, g_nPNG, "PNG");
      FiltersToString(filters, g_nSGI, "SGI");
      FiltersToString(filters, g_nTGA, "TGA");
      FiltersToString(filters, g_nTIF, "TIF");
      FiltersToString(filters, g_nWAV, "WAV");
      fprintf(stdout, "\r\n%*s[filter: %.*s]\r", tracer->digits + tracer->digits + length + 39, " ", barLength, filters.c_str());

      static constexpr std::array<char, 5> cursor_up_one_line{{"\033[1A"}};
#if defined(__linux__)
      fputs(&cursor_up_one_line[0], stdout);
#else
      const char* const term{getenv("TERM")};
      if (nullptr != term) {
        fputs(&cursor_up_one_line[0], stdout);
      } else {
        void* const handle{GetStdHandle(STD_OUTPUT_HANDLE)};
        if (nullptr != handle) {
          CONSOLE_SCREEN_BUFFER_INFO csbi;
          if (0 != GetConsoleScreenBufferInfo(handle, &csbi)) {
            COORD coord{.X = csbi.dwCursorPosition.X, .Y = SHORT(csbi.dwCursorPosition.Y - 1)};
            if (coord.Y < csbi.srWindow.Top) {
              coord.Y = csbi.srWindow.Top;
            }
            SetConsoleCursorPosition(handle, coord);
          }
        }
      }
#endif
    } else {
      fputc('\r', stdout);
    }
    fflush(stdout);
  }
}

static void MonitorWorker(const volatile TraceProgress_t* tracer) noexcept {
  assert(tracer);
#if defined(__linux__)
  pthread_setname_np(pthread_self(), __FUNCTION__);
#endif

  for (auto count{1}; tracer->isRunning; ++count) {
    if (count >= 5) {  // Do every 5*50ms = 250ms do every something
      count = 1;
      ProgressBar(tracer);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
}

Progress_t::Progress_t(const std::string& workType, bool encode, const iMonitor_t& monitor)
    : _tracer{.isRunning = true,
              .workType{workType[0], workType[1], workType[2], '\0'},
              .encode = encode,
              .digits = digits(monitor.layoutLength()),
              .columns = consoleCol(),
              .monitor = monitor,
              .start = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count()},  //
      _monitorWorker{MonitorWorker, &_tracer} {
  g_nFilter = 0;
  g_nBMP = 0;
  g_nDCM = 0;
  g_nELF = 0;
  g_nEXE = 0;
  g_nGIF = 0;
  g_nPBM = 0;
  g_nPDF = 0;
  g_nPKZ = 0;
  g_nPNG = 0;
  g_nSGI = 0;
  g_nTGA = 0;
  g_nTIF = 0;
  g_nWAV = 0;
  ProgressBar(&_tracer);
}

Progress_t::~Progress_t() noexcept {
  _tracer.isRunning = false;
  _monitorWorker.join();
  ProgressBar(&_tracer);  // Flush!
  fputc('\n', stdout);
  fflush(stdout);
}

auto Progress_t::PeakMemoryUse() noexcept -> uint32_t {
  return g_peakMemoryUse;
}

void Progress_t::FoundType(const Filter& type) noexcept {
  // clang-format off
  switch (type) {
    case Filter::BMP: g_nFilter = g_nFilter + 1; g_nBMP = g_nBMP + 1; break;
    case Filter::DCM: g_nFilter = g_nFilter + 1; g_nDCM = g_nDCM + 1; break;
    case Filter::ELF: g_nFilter = g_nFilter + 1; g_nELF = g_nELF + 1; break;
    case Filter::EXE: g_nFilter = g_nFilter + 1; g_nEXE = g_nEXE + 1; break;
    case Filter::GIF: g_nFilter = g_nFilter + 1; g_nGIF = g_nGIF + 1; break;
    case Filter::PBM: g_nFilter = g_nFilter + 1; g_nPBM = g_nPBM + 1; break;
    case Filter::PDF: g_nFilter = g_nFilter + 1; g_nPDF = g_nPDF + 1; break;
    case Filter::PKZ: g_nFilter = g_nFilter + 1; g_nPKZ = g_nPKZ + 1; break;
    case Filter::PNG: g_nFilter = g_nFilter + 1; g_nPNG = g_nPNG + 1; break;
    case Filter::SGI: g_nFilter = g_nFilter + 1; g_nSGI = g_nSGI + 1; break;
    case Filter::TGA: g_nFilter = g_nFilter + 1; g_nTGA = g_nTGA + 1; break;
    case Filter::TIF: g_nFilter = g_nFilter + 1; g_nTIF = g_nTIF + 1; break;
    case Filter::WAV: g_nFilter = g_nFilter + 1; g_nWAV = g_nWAV + 1; break;

    case Filter::NOFILTER:
    default:
      break;
  }
  // clang-format on
}
