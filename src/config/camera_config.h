#pragma once

#include <cstdint>

namespace app::config {

inline constexpr uint32_t kCameraLockTimeoutMs = 2000;
inline constexpr int kCameraClockHz = 20000000;
inline constexpr int kDefaultJpegQuality = 12;
inline constexpr uint8_t kMaximumJpegQuality = 63;

}  // namespace app::config
