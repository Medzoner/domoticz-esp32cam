#pragma once

#include <cstdint>

namespace app::core {

// Numeric sensor IDs are checked against the pinned SDK in esp32_camera.cpp.
inline constexpr uint8_t kVgaFrameSize = 8;
inline constexpr uint8_t kUxgaFrameSize = 13;

struct FrameBufferPolicy {
  uint8_t allocationFrameSize;
  uint8_t startupFrameSize;
  uint8_t allocationQuality;
  uint8_t startupQuality;
};

constexpr FrameBufferPolicy frameBufferPolicy(bool psram, uint8_t quality) {
  // Espressif's CameraWebServer initializes at maximum resolution/high quality
  // to reserve large buffers, then reduces the active resolution after init.
  return {psram ? kUxgaFrameSize : kVgaFrameSize, kVgaFrameSize,
          static_cast<uint8_t>(psram && quality > 10 ? 10 : quality), quality};
}

constexpr bool frameSizeFitsAllocation(int requested, uint8_t allocated) {
  return requested >= 0 && requested <= allocated && requested <= kUxgaFrameSize;
}

}  // namespace app::core
