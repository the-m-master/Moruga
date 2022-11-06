/* Filter, is a binary preparation for encoding/decoding
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
#include "Buffer.h"
#include "filter.h"
class File_t;
class iEncoder_t;

class CAB_filter final : public iFilter_t {
public:
  explicit CAB_filter(File_t& stream, iEncoder_t* const coder, DataInfo_t& di) noexcept;
  virtual ~CAB_filter() noexcept override;

  CAB_filter() = delete;
  CAB_filter(const CAB_filter&) = delete;
  CAB_filter(CAB_filter&&) = delete;
  CAB_filter& operator=(const CAB_filter&) = delete;
  CAB_filter& operator=(CAB_filter&&) = delete;

  virtual auto Handle(int32_t ch) noexcept -> bool final;                // encoding
  virtual auto Handle(int32_t ch, int64_t& pos) noexcept -> bool final;  // decoding

private:
  File_t& _stream;
  iEncoder_t* const _coder;
  DataInfo_t& _di;
  uint32_t byte_counter_{0};
  uint32_t skip_counter_{0};

  struct ReserveHeader_t {
    uint16_t headerReserveSize;
    uint8_t folderReserveSize;
    uint8_t fileReserveSize;
  };
  ReserveHeader_t reserveHeader_{};

  struct FolderHeader_t {
    uint32_t offset;
    uint16_t nBlocks;
    uint16_t format;
  };
  uint16_t cfolder_{0};
  FolderHeader_t folderHeader_{};

  struct FileHeader_t {
    uint32_t length;
    uint32_t offset;
    uint16_t id;
    uint16_t date;
    uint16_t time;
    uint16_t attributes;
    std::string name;
  };
  uint16_t cname_{0};
  uint16_t cfile_{0};
  FileHeader_t fileHeader_{};

  struct Data_t {
    uint32_t crc;
    uint16_t compressedDataLength;
    uint16_t uncompressedDataLength;
  };
  uint16_t nBlocks_{0};
  Data_t data_{};

  uint16_t header_{};
};
