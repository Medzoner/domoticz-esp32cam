#pragma once

#include "FS.h"

class FakeSD {
 public:
  bool begin(const char*, bool, bool) {
    fake::accessFilesystem();
    return true;
  }
  uint64_t totalBytes() const {
    fake::accessFilesystem();
    return fake::totalBytes;
  }
  uint64_t usedBytes() const {
    fake::accessFilesystem();
    uint64_t bytes = 0;
    for (const auto& file : fake::files) bytes += file.second->size();
    return bytes;
  }
  bool exists(const char* path) const {
    fake::accessFilesystem();
    return fake::files.count(path) != 0;
  }
  fs::File open(const char* path, const char* mode = FILE_READ) {
    fake::accessFilesystem();
    if (std::strcmp(path, "/") == 0) return fs::File(path, true);
    if (std::strcmp(mode, FILE_WRITE) == 0) {
      fake::files[path] = std::make_shared<std::vector<uint8_t>>();
    }
    return exists(path) ? fs::File(path) : fs::File{};
  }
  bool remove(const char* path) {
    fake::accessFilesystem();
    return fake::files.erase(path) != 0;
  }
};
inline FakeSD SD_MMC;
