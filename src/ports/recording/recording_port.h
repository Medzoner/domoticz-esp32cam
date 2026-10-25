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
  // False means unavailable telemetry, not an idle/empty recording state.
  bool snapshotValid = false;
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
  // True means the stop request was accepted, not that flushing has finished.
  virtual bool requestStop() = 0;
  virtual bool wantsFrames() const = 0;
  virtual void service() = 0;
  // Synchronous: consume/copy the bytes before returning; never retain data.
  // The caller may reuse its delivery buffer as soon as this call returns.
  virtual bool appendFrame(const uint8_t* data, std::size_t size) = 0;
  virtual RecordingStats stats() const = 0;
  // Failure is distinct from a successful empty listing; count is zero on failure.
  virtual bool list(RecordingFileInfo* files, std::size_t capacity,
                    std::size_t& count) = 0;
  virtual bool remove(uint32_t index) = 0;
  virtual bool openReader(uint32_t index) = 0;
  virtual std::size_t read(uint8_t* destination, std::size_t capacity) = 0;
  virtual bool readerSize(uint64_t& size) const = 0;
  // False retains reader ownership. The reader task must retry before reusing it.
  virtual bool closeReader() = 0;
};

}  // namespace app::ports
