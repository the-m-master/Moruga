/* iMonitor, monitor interface for progress bar handling
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

/**
 * @class iMonitor_t
 * @brief Interface class for progress monitoring
 *
 * Interface class for progress monitoring
 */
class iMonitor_t {
public:
  virtual ~iMonitor_t() noexcept;

  [[nodiscard]] virtual auto InputLength() const noexcept -> int64_t = 0;
  [[nodiscard]] virtual auto OutputLength() const noexcept -> int64_t = 0;
  [[nodiscard]] virtual auto WorkLength() const noexcept -> int64_t = 0;
  [[nodiscard]] virtual auto LayoutLength() const noexcept -> int64_t = 0;
};
