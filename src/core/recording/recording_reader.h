#pragma once

#include <cstddef>
#include <cstdint>

namespace app::core::recording {

// Reader must return 0 on EOF/error, otherwise at most the requested count.
// A positive short read is not EOF; keep reading the remainder.
template <typename Read>
bool readExact(uint8_t* destination, std::size_t length, Read read) {
  if (destination == nullptr && length != 0) {
    return false;
  }
  std::size_t offset = 0;
  while (offset < length) {
    const std::size_t count = read(destination + offset, length - offset);
    if (count == 0 || count > length - offset) {
      return false;
    }
    offset += count;
  }
  return true;
}

enum class FrameReadStatus { kFrame, kEnd, kTruncated, kInvalid, kReadFailed };

struct FrameReadResult {
  FrameReadStatus status;
  uint32_t size = 0;
};

// remaining starts at file size minus the already-consumed MJR1 magic.
// A successful result reserves header+payload from remaining. The caller must
// consume that payload before asking for another header, and stop on errors.
// Preflight prevents announcing an incomplete on-disk frame; it cannot prevent
// later media read errors or validate JPEG contents (MJR1 has no checksum).
template <typename Read>
FrameReadResult nextFrame(uint64_t& remaining, uint32_t maximumSize, Read read) {
  if (remaining == 0) {
    return {FrameReadStatus::kEnd};
  }
  uint8_t header[sizeof(uint32_t)]{};
  if (remaining < sizeof(header)) {
    return {FrameReadStatus::kTruncated};
  }
  if (!readExact(header, sizeof(header), read)) {
    return {FrameReadStatus::kReadFailed};
  }
  remaining -= sizeof(header);
  const uint32_t size = static_cast<uint32_t>(header[0]) |
                        (static_cast<uint32_t>(header[1]) << 8) |
                        (static_cast<uint32_t>(header[2]) << 16) |
                        (static_cast<uint32_t>(header[3]) << 24);
  if (size == 0 || size > maximumSize) {
    return {FrameReadStatus::kInvalid};
  }
  if (size > remaining) {
    return {FrameReadStatus::kTruncated};
  }
  remaining -= size;
  return {FrameReadStatus::kFrame, size};
}

}  // namespace app::core::recording
