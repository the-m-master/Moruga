/* File, fast file handling
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
 */
#ifndef _FILE_HDR_
#define _FILE_HDR_

#include <sys/stat.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include "Utilities.h"

#if defined(_WIN32) || defined(_WIN64) || defined(__CYGWIN__)
#include <windows.h>

#if !defined(__CYGWIN__)
#define getc_unlocked(_stream) _fgetc_nolock(_stream)
#define putc_unlocked(_c, _stream) _fputc_nolock(_c, _stream)

/**
 * On Windows when using tmpfile() the temporary file may be created in the root
 * directory causing access denied error when User Account Control (UAC) is on.
 * To avoid this issue with tmpfile() we simply use fopen() instead.
 * We create a temporary path and file name.
 * The MS library provides two (MS specific) fopen() flags: Temporary and Delete.
 * NOTE: Solving this using <filesystem> will increase application size with 300% (??)
 * @return temporary file location
 */
static std::string getTempFileLocation() noexcept {
  char filename[MAX_PATH];
  memset(filename, 0, sizeof(filename));
  char temppath[MAX_PATH];
  const uint32_t i{GetTempPathA(sizeof(temppath), temppath)};
  if (i < sizeof(temppath)) {
    static constexpr char prefix[]{"Moruga"};
    GetTempFileNameA(temppath, prefix, 0, filename);
  }
  return filename;
}
#endif  // !defined(__CYGWIN__)
#endif  // defined(_WIN32) || defined(_WIN64) || defined(__CYGWIN__)

class File_t final {
public:
#if defined(__linux__) || defined(__CYGWIN__)
  explicit File_t() noexcept : File_t(nullptr, _mode) {}
#else
  explicit File_t() noexcept : File_t(getTempFileLocation().c_str(), _mode) {}
#endif

  explicit File_t(const char* const path, const std::string& mode) noexcept : _stream{path ? fopen(path, mode.c_str()) : std::tmpfile()} {
    if (!_stream) {
      fprintf(stderr, "Cannot open file '%s'\n", path);
      exit(EXIT_FAILURE);
    }
  }

  ~File_t() noexcept {
    Close();
  }

  File_t(const File_t& orig) = delete;
  File_t(const File_t&& orig) = delete;
  auto operator=(const File_t&& orig) -> File_t& = delete;

  constexpr operator FILE*() const noexcept {
    return _stream;
  }

  auto operator=(FILE* stream) noexcept -> File_t& {
    _stream = stream;
    return *this;
  }

  auto operator=(const File_t& source) noexcept -> File_t& {
    if (this != &source) {  // self-assignment check
      _stream = source._stream;
    }
    return *this;
  }

  [[nodiscard]] constexpr auto isOpen() const noexcept -> bool {
    return nullptr != _stream;
  }

  /**
   * Get size of file
   * @return The size of file
   */
  [[nodiscard]] auto Size() const noexcept -> int64_t {
    Flush();  // Mandatory to flush first!
#if defined(__CYGWIN__)
    struct stat fileInfo;
    fstat(fileno(_stream), &fileInfo);
#elif defined(_MSC_VER)
    struct _stat64 fileInfo;
    _fstat64(_fileno(_stream), &fileInfo);
#else
    struct stat64 fileInfo;
    fstat64(fileno(_stream), &fileInfo);
#endif
    return fileInfo.st_size;
  }

  /**
   * Get current position in file
   * @return The current position in file
   */
  [[nodiscard]] auto Position() const noexcept -> int64_t {
#if defined(__CYGWIN__)
    return ftell(_stream);
#elif defined(_MSC_VER)
    return _ftelli64(_stream);
#else
    if (fpos_t pos; !fgetpos(_stream, &pos)) {
#if defined(__linux__)
      return pos.__pos;
#else
      return pos;
#endif
    }
    return INT64_C(-1);
#endif
  }

  auto Seek(const int64_t offset) const noexcept -> int32_t {
#if defined(__linux__)
    return fseeko64(_stream, offset, SEEK_SET);
#else
    const fpos_t pos{offset};
    return fsetpos(_stream, &pos);
#endif
  }

  auto Rewind() const noexcept -> int32_t {
    return Seek(0);
  }

  auto Flush() const noexcept -> int32_t {
    return fflush(_stream);
  }

  void Close() noexcept {
    if (_stream) {
      fclose(_stream);
      _stream = nullptr;
    }
  }

  [[nodiscard]] ALWAYS_INLINE auto getc() const noexcept -> int32_t {
    return getc_unlocked(_stream);
  }

  ALWAYS_INLINE void putc(const int32_t ch) const noexcept {
    putc_unlocked(ch, _stream);
  }

  [[nodiscard]] auto get32() const noexcept -> uint32_t {
    return uint32_t(getc() << 24) |  //
           uint32_t(getc() << 16) |  //
           uint32_t(getc() << 8) |   //
           uint32_t(getc());
  }

  void put32(const uint32_t x) const noexcept {
    putc(int32_t(x >> 24));
    putc(int32_t(x >> 16));
    putc(int32_t(x >> 8));
    putc(int32_t(x));
  }

  [[nodiscard]] auto getVLI() const noexcept -> int64_t {
    int64_t i{0};
    int32_t k{0};
    int32_t b;
    do {
      b = getc();
      i |= int64_t(0x3F & b) << k;
      k += 6;
    } while (0x80 & b);
    return i;
  }

  void putVLI(int64_t i) const noexcept {
    while (i > 0x3F) {
      putc(int32_t(0x80 | (0x3F & i)));
      i >>= 6;
    }
    putc(int32_t(i));
  }

  auto Read(void* const data, const size_t size) const noexcept -> size_t {
    return fread(data, sizeof(char), size, _stream);
  }

  auto Write(const void* const data, const size_t size) const noexcept -> size_t {
    return fwrite(data, sizeof(char), size, _stream);
  }

private:
  static constexpr char _mode[]{"wb+TD"};

  FILE* _stream;
};

#endif /* _FILE_HDR_ */
