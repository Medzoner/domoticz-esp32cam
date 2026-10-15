#pragma once

#include <cstddef>
#include <cstdint>

namespace app::ports {

struct RecordingStats {
  bool supported;
  bool available;
  bool active;
  bool startPending;
  uint32_t segment;
  uint32_t segments;
  uint32_t frames;
  uint32_t failures;
  uint64_t bytes;
  uint64_t totalBytes;
  uint64_t freeBytes;
};

struct RecordingFileInfo {
  uint32_t index;
  uint64_t bytes;
};

class RecordingPort {
 public:
  virtual ~RecordingPort() = default;
  virtual bool begin() = 0;
  virtual bool requestStart() = 0;
  virtual void requestStop() = 0;
  virtual bool wantsFrames() const = 0;
  virtual void service() = 0;
  virtual bool appendFrame(const uint8_t* data, std::size_t size) = 0;
  virtual RecordingStats stats() const = 0;
  virtual std::size_t list(RecordingFileInfo* files,
                           std::size_t capacity) = 0;
  virtual bool remove(uint32_t index) = 0;
  virtual bool openReader(uint32_t index) = 0;
  virtual std::size_t read(uint8_t* destination, std::size_t capacity) = 0;
  virtual uint64_t readerSize() const = 0;
  virtual void closeReader() = 0;
};

}  // namespace app::ports
