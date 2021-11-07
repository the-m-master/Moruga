/* File, fast file handling
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
#ifndef _FILE_HDR_
#define _FILE_HDR_

#include <sys/stat.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

#if defined(_MSC_VER)
#define ALWAYS_INLINE __forceinline
#define __restrict__ __restrict
#define strcasecmp stricmp
#elif defined(__GNUC__)
#define ALWAYS_INLINE inline __attribute__((always_inline))
#else
#define ALWAYS_INLINE inline
#endif

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
    GetTempFileNameA(temppath, "Moruga", 0, filename);
  }
  return filename;
}
#endif  // !defined(__CYGWIN__)
#endif  // defined(_WIN32) || defined(_WIN64) || defined(__CYGWIN__)

class File_t final {
public:
#if defined(__linux__) || defined(__CYGWIN__)
  explicit File_t() : File_t(nullptr, _mode) {}
#else
  explicit File_t() : File_t(getTempFileLocation().c_str(), _mode) {}
#endif

  explicit File_t(const char* path, const std::string& mode) : _stream{path ? fopen(path, mode.c_str()) : std::tmpfile()} {
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
    fflush(_stream);  // Mandatory to flush first!
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
    fpos_t pos;
    if (fgetpos(_stream, &pos)) {
      return INT64_C(-1);
    }
#if defined(__linux__)
    return pos.__pos;
#else
    return pos;
#endif
#endif
  }

  auto Seek(int64_t offset) const noexcept -> int32_t {
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

  void Close() noexcept {
    if (_stream) {
      fclose(_stream);
      _stream = nullptr;
    }
  }

  [[nodiscard]] ALWAYS_INLINE auto getc() const noexcept -> int32_t {
    return getc_unlocked(_stream);
  }

  ALWAYS_INLINE void putc(int32_t ch) const noexcept {
    putc_unlocked(ch, _stream);
  }

  [[nodiscard]] auto get32() const noexcept -> uint32_t {
    return uint32_t(getc() << 24) |  //
           uint32_t(getc() << 16) |  //
           uint32_t(getc() << 8) |   //
           uint32_t(getc());
  }

  void put32(uint32_t x) const noexcept {
    putc(int32_t(x >> 24));
    putc(int32_t(x >> 16));
    putc(int32_t(x >> 8));
    putc(int32_t(x));
  }

  [[nodiscard]] auto get64() const noexcept -> int64_t {
    return (int64_t(getc()) << 56) |  //
           (int64_t(getc()) << 48) |  //
           (int64_t(getc()) << 40) |  //
           (int64_t(getc()) << 32) |  //
           (int64_t(getc()) << 24) |  //
           (int64_t(getc()) << 16) |  //
           (int64_t(getc()) << 8) |   //
           int64_t(getc());
  }

  void put64(int64_t x) const noexcept {
    putc(int32_t(x >> 56));
    putc(int32_t(x >> 48));
    putc(int32_t(x >> 40));
    putc(int32_t(x >> 32));
    putc(int32_t(x >> 24));
    putc(int32_t(x >> 16));
    putc(int32_t(x >> 8));
    putc(int32_t(x));
  }

  auto Read(void* data, size_t size) const noexcept -> size_t {
    return fread(data, sizeof(char), size, _stream);
  }

  auto Write(void* data, size_t size) const noexcept -> size_t {
    return fwrite(data, sizeof(char), size, _stream);
  }

private:
  static constexpr char _mode[] = "wb+TD";

  FILE* _stream;
};

#endif /* _FILE_HDR_ */