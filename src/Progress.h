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
#pragma once

#include <cstdint>
#include <string_view>
#include <thread>
class iMonitor_t;

enum class Filter;

/**
 * @struct TraceProgress_t
 * @brief Container for track and trace information
 *
 * Container for track and trace information
 */
#pragma pack(push, 1)
struct TraceProgress_t {
  bool isRunning;

  const char workType[4];
  const bool encode;
  const int32_t digits;
  const iMonitor_t& monitor;
  const int64_t start;
};
#pragma pack(pop)

/**
 * @class Progress_t
 * @brief Progress monitoring, display as text
 *
 * Progress monitoring, display as text
 */
class Progress_t final {
public:
  explicit Progress_t(std::string_view workType, bool encode, const iMonitor_t& monitor) noexcept;
  virtual ~Progress_t() noexcept;

  [[nodiscard]] static auto PeakMemoryUse() noexcept -> uint32_t;  // in KiB

  static void FoundType(const Filter& type) noexcept;

  static void Cancelled(const Filter& type) noexcept;

private:
  Progress_t() = delete;
  Progress_t(const Progress_t& other) = delete;
  Progress_t(Progress_t&& other) = delete;
  Progress_t& operator=(const Progress_t& other) = delete;
  Progress_t& operator=(Progress_t&& other) = delete;

  TraceProgress_t _tracer;
  int32_t : 16;  // Padding
  int32_t : 32;  // Padding
  std::thread _monitor_worker;
};
