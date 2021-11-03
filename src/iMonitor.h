/* iMonitor, monitor interface for progress bar handling
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
 * along with this program; see the file COPYING3.
 * If not, see <https://www.gnu.org/licenses/>
 */
#ifndef _IMONITOR_HDR_
#define _IMONITOR_HDR_

#include <cstdint>

class iMonitor_t {
public:
  virtual ~iMonitor_t() noexcept;

//  iMonitor(const iMonitor&) = delete;
//  iMonitor(iMonitor&&) = delete;
//  auto operator=(const iMonitor&) -> iMonitor& = delete;
//  auto operator=(iMonitor&&) -> iMonitor& = delete;

  [[nodiscard]] virtual int64_t inputLength() const noexcept = 0;
  [[nodiscard]] virtual int64_t outputLength() const noexcept = 0;
  [[nodiscard]] virtual int64_t workLength() const noexcept = 0;
  [[nodiscard]] virtual int64_t layoutLength() const noexcept = 0;
};

#endif  // _IMONITOR_HDR_
