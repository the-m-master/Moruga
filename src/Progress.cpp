/* Progress, simple progress bar handling
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
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
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "filters/filter.h"
#include "iMonitor.h"

#if defined(__linux__)
#  include <bits/types/struct_rusage.h>
#  include <pthread.h>
#  include <sys/ioctl.h>
#  include <sys/resource.h>
#  include <unistd.h>
#elif defined(_WIN32) || defined(_WIN64) || defined(__CYGWIN__) || defined(_MSC_VER)
#  include <windows.h>  // Must be done first! Otherwise #error: "No Target Architecture" in VS2019
//
#  include <psapi.h>
#endif

iMonitor_t::~iMonitor_t() noexcept = default;

// static_cast<int32_t>(ceil(log(double(layoutLength | 1)) / log(10.0)))
static auto digits(int64_t number) noexcept -> int32_t {
  number |= 1;
  int32_t ndigits{0};
  for (int64_t value{1}; value < number; value *= 10) {
    ++ndigits;
  }
  return ndigits;
}

#if defined(__linux__)

static auto consoleCol() noexcept -> int32_t {
  struct winsize csbi;
  memset(&csbi, 0, sizeof(csbi));
  ioctl(STDIN_FILENO, TIOCGWINSZ, &csbi);
  const int32_t columns{csbi.ws_col};
  return columns;
}

static auto memoryUseKiB() noexcept -> uint32_t {
  struct rusage rUsage;
  memset(&rUsage, 0, sizeof(rUsage));
  getrusage(RUSAGE_SELF, &rUsage);
  return static_cast<uint32_t>(rUsage.ru_maxrss);  // Maximum resident set size utilised in KiB
}

#else

static auto getConsoleScreenBufferInfo(void** handler = nullptr) noexcept -> const CONSOLE_SCREEN_BUFFER_INFO* {
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

static auto consoleRowCol() noexcept -> std::pair<bool, int32_t> {
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

static auto consoleCol() noexcept -> int32_t {
  if (const auto result{consoleRowCol()}; result.first) {
    return result.second;
  }
  try {
    static std::array<char, 4> result{{'8', '0', 0, 0}};
    if (std::unique_ptr<FILE, decltype(&pclose)> pipe(popen("tput cols 2>&1", "r"), pclose); nullptr != pipe) {
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

static auto memoryUseKiB() noexcept -> uint32_t {
  PROCESS_MEMORY_COUNTERS rUsage;
  GetProcessMemoryInfo(GetCurrentProcess(), &rUsage, sizeof(rUsage));
  return static_cast<uint32_t>(rUsage.PeakPagefileUsage / SIZE_T(1024));  // in KiB
}

#endif

static uint32_t peakMemoryUse_{0};  // in KiB

static volatile uint32_t nFilter_{0};

static volatile uint32_t nBMP_{0};
static volatile uint32_t nELF_{0};
static volatile uint32_t nEXE_{0};
static volatile uint32_t nGIF_{0};
static volatile uint32_t nGZP_{0};
static volatile uint32_t nLZX_{0};
static volatile uint32_t nPBM_{0};
static volatile uint32_t nPDF_{0};
static volatile uint32_t nPKZ_{0};
static volatile uint32_t nPNG_{0};
static volatile uint32_t nSGI_{0};
static volatile uint32_t nTGA_{0};
static volatile uint32_t nTIF_{0};
static volatile uint32_t nWAV_{0};

static void FiltersToString(std::string& filters, uint32_t count, const std::string& text) noexcept {
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

    const int64_t workPosition{tracer->encode ? inBytes : outBytes};

    auto speed{(workPosition * POW10_6) / deltaTime};
    std::string speed_dim{"B/s"};
    if (speed > 9999) {
      speed_dim = "KiB/s";
      speed = ((speed / 512) + 1) / 2;
      if (speed > 9999) {
        speed_dim = "MiB/s";
        speed = ((speed / 512) + 1) / 2;
        if (speed > 9999) {
          speed_dim = "GiB/s";
          speed = ((speed / 512) + 1) / 2;
        }
      }
    }

    auto mem_use{memoryUseKiB()};
    peakMemoryUse_ = (std::max)(peakMemoryUse_, mem_use);
    std::string mem_dim{"KiB"};
    if (mem_use > 9999) {
      mem_dim = "MiB";
      mem_use = ((mem_use / 512) + 1) / 2;
      if (mem_use > 9999) {
        mem_dim = "GiB";
        mem_use = ((mem_use / 512) + 1) / 2;
      }
    }

    const auto ms{(workPosition > min_time) ? std::clamp(((workLength * deltaTime) / workPosition) - deltaTime, min_time, max_time) : min_time};
    const auto mm{static_cast<int32_t>(ms / (INT64_C(60) * POW10_6))};
    const auto ss{static_cast<int32_t>((ms - (mm * INT64_C(60) * POW10_6)) / POW10_6)};

    const auto columns{consoleCol()};
    int32_t length{28 + 3 + tracer->digits + tracer->digits + 4 + 4 + 6 + 3};
    length = (std::max)(0, columns - length);

    const auto barLength{std::clamp(length, 2, 256)};
    const auto buzy{(workPosition * barLength) / workLength};

    std::string progress{};
    for (auto n{0}; n < barLength; ++n) {
      progress += (n < buzy) ? '#' : '.';
    }
    if ((buzy >= 0) && (buzy < barLength)) {
      static constexpr std::array<char, 4> animation{{'\\', '|', '/', '-'}};
      static uint32_t art{0};
      progress[static_cast<size_t>(buzy)] = animation[art++];
      if (art >= animation.size()) {
        art = 0;
      }
    }

    length = (std::max)(0, length - static_cast<int32_t>(progress.length()));
    const std::string filler(static_cast<size_t>(length), ' ');

    fprintf(stdout, "%s in/out %*" PRId64 "/%*" PRId64 "%s %4u %s %4" PRId64 " %-5s %02d:%02d [%s] %3" PRId64 "%%",
            tracer->workType,           //
            tracer->digits, inBytes,    //
            -tracer->digits, outBytes,  //
            filler.c_str(),             //
            mem_use, mem_dim.c_str(),   //
            speed, speed_dim.c_str(),   //
            mm, ss,                     //
            progress.c_str(),           //
            ((workPosition * POW10_2) / workLength));
    if (nFilter_) {
      std::string filters;
      FiltersToString(filters, nBMP_, "BMP");
      FiltersToString(filters, nELF_, "ELF");
      FiltersToString(filters, nEXE_, "EXE");
      FiltersToString(filters, nGIF_, "GIF");
      FiltersToString(filters, nGZP_, "GZ");  //  GNU zip
      FiltersToString(filters, nLZX_, "LZX");
      FiltersToString(filters, nPBM_, "PBM");
      FiltersToString(filters, nPDF_, "PDF");
      FiltersToString(filters, nPKZ_, "PKZ");  // PKZip
      FiltersToString(filters, nPNG_, "PNG");
      FiltersToString(filters, nSGI_, "SGI");
      FiltersToString(filters, nTGA_, "TGA");
      FiltersToString(filters, nTIF_, "TIF");
      FiltersToString(filters, nWAV_, "WAV");
      fprintf(stdout, "\r\n%*s[filter: %.*s]\r", tracer->digits + tracer->digits + length + 39, " ", barLength, filters.c_str());

      static constexpr std::array<char, 5> cursor_up_one_line{{"\033[1A"}};
#if defined(__linux__)
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

static void MonitorWorker(const volatile TraceProgress_t* const tracer) noexcept {
  assert(tracer);
#if !defined(_MSC_VER)
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

Progress_t::Progress_t(const std::string& workType, bool encode, const iMonitor_t& monitor) noexcept
    : _tracer{.isRunning = true,
              .workType{workType[0], workType[1], workType[2], '\0'},
              .encode = encode,
              .digits = digits(monitor.layoutLength()),
              .monitor = monitor,
              .start = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count()},  //
      _monitor_worker{MonitorWorker, &_tracer} {
  nFilter_ = 0;
  nBMP_ = 0;
  nELF_ = 0;
  nEXE_ = 0;
  nGIF_ = 0;
  nGZP_ = 0;
  nLZX_ = 0;
  nPBM_ = 0;
  nPDF_ = 0;
  nPKZ_ = 0;
  nPNG_ = 0;
  nSGI_ = 0;
  nTGA_ = 0;
  nTIF_ = 0;
  nWAV_ = 0;
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
  // clang-format off
  switch (type) {
    case Filter::BMP: nFilter_ = nFilter_ + 1; nBMP_ = nBMP_ + 1; break;
    case Filter::ELF: nFilter_ = nFilter_ + 1; nELF_ = nELF_ + 1; break;
    case Filter::EXE: nFilter_ = nFilter_ + 1; nEXE_ = nEXE_ + 1; break;
    case Filter::GIF: nFilter_ = nFilter_ + 1; nGIF_ = nGIF_ + 1; break;
    case Filter::GZP: nFilter_ = nFilter_ + 1; nGZP_ = nGZP_ + 1; break;
    case Filter::LZX: nFilter_ = nFilter_ + 1; nLZX_ = nLZX_ + 1; break;
    case Filter::PBM: nFilter_ = nFilter_ + 1; nPBM_ = nPBM_ + 1; break;
    case Filter::PDF: nFilter_ = nFilter_ + 1; nPDF_ = nPDF_ + 1; break;
    case Filter::PKZ: nFilter_ = nFilter_ + 1; nPKZ_ = nPKZ_ + 1; break;
    case Filter::PNG: nFilter_ = nFilter_ + 1; nPNG_ = nPNG_ + 1; break;
    case Filter::SGI: nFilter_ = nFilter_ + 1; nSGI_ = nSGI_ + 1; break;
    case Filter::TGA: nFilter_ = nFilter_ + 1; nTGA_ = nTGA_ + 1; break;
    case Filter::TIF: nFilter_ = nFilter_ + 1; nTIF_ = nTIF_ + 1; break;
    case Filter::WAV: nFilter_ = nFilter_ + 1; nWAV_ = nWAV_ + 1; break;

    case Filter::NOFILTER:
    default:
      break;
  }
  // clang-format on
}
