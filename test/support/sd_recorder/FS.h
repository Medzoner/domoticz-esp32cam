#pragma once

#include <algorithm>
#include <cstring>

#include "fake_storage.h"

inline constexpr const char* FILE_READ = "r";
inline constexpr const char* FILE_WRITE = "w";

namespace fs {
class File {
 public:
  File() = default;
  explicit File(const std::string& path, bool directory = false)
      : handle_(std::make_shared<Handle>()) {
    handle_->path = path;
    handle_->directory = directory;
    if (!directory) handle_->bytes = fake::files.at(path);
  }
  explicit operator bool() const {
    fake::accessFilesystem();
    return handle_ && handle_->open;
  }
  bool isDirectory() const {
    fake::accessFilesystem();
    return handle_ && handle_->directory;
  }
  const char* name() const {
    fake::accessFilesystem();
    return handle_->path.c_str();
  }
  std::size_t size() const {
    fake::accessFilesystem();
    return handle_ && handle_->bytes ? handle_->bytes->size() : 0;
  }
  std::size_t read(uint8_t* data, std::size_t capacity) {
    fake::accessFilesystem();
    if (!handle_ || !handle_->open || !handle_->bytes) return 0;
    const auto size = std::min(capacity, handle_->bytes->size() - handle_->position);
    if (size != 0) std::memcpy(data, handle_->bytes->data() + handle_->position, size);
    handle_->position += size;
    return size;
  }
  std::size_t write(const uint8_t* data, std::size_t size) {
    fake::accessFilesystem();
    if (!handle_ || !handle_->open || !handle_->bytes) return 0;
    if (fake::shortWrite && size != 0) --size;
    handle_->bytes->insert(handle_->bytes->end(), data, data + size);
    return size;
  }
  void flush() { fake::accessFilesystem(); }
  void close() {
    fake::accessFilesystem();
    if (handle_) handle_->open = false;
  }
  File openNextFile() {
    fake::accessFilesystem();
    if (!handle_ || !handle_->directory) return {};
    auto iterator = fake::files.upper_bound(handle_->lastEntry);
    if (iterator == fake::files.end()) return {};
    handle_->lastEntry = iterator->first;
    return File(iterator->first);
  }

 private:
  struct Handle {
    std::string path;
    std::string lastEntry;
    bool directory = false;
    bool open = true;
    std::size_t position = 0;
    std::shared_ptr<std::vector<uint8_t>> bytes;
  };
  std::shared_ptr<Handle> handle_;
};
}  // namespace fs
