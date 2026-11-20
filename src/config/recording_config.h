#pragma once

#include <cstddef>
#include <cstdint>

namespace app::config {

inline constexpr uint64_t kRecordingSegmentMaxBytes = 64ULL * 1024ULL * 1024ULL;
inline constexpr uint64_t kRecordingMinimumFreeBytes = 8ULL * 1024ULL * 1024ULL;
inline constexpr uint32_t kRecordingFlushIntervalMs = 2000;
inline constexpr uint32_t kRecordingLockTimeoutMs = 1000;
inline constexpr uint32_t kMaximumRecordingSegmentIndex = 99999;
inline constexpr std::size_t kMaxRecordingFiles = 32;
inline constexpr std::size_t kRecordingReadBlockSize = 4096;
// Per download block: tolerate Wi-Fi backpressure without retaining a stalled
// reader indefinitely. Progress refreshes only the idle deadline.
inline constexpr uint32_t kDownloadWriteTimeoutMs = 5000;
inline constexpr uint32_t kDownloadWriteIdleTimeoutMs = 2000;
inline constexpr uint32_t kDownloadTransferTimeoutMs = 10 * 60 * 1000;
inline constexpr std::size_t kMaximumRecordedFrameBytes = 2 * 1024 * 1024;
inline constexpr uint16_t kPlaybackPort = 82;
inline constexpr uint32_t kPlaybackFrameIntervalMs = 100;
inline constexpr uint32_t kPlaybackTaskStackSize = 4096;
inline constexpr uint32_t kPlaybackTaskPriority = 1;
inline constexpr uint32_t kPlaybackTaskCore = 0;
inline constexpr char kPlaybackTaskName[] = "recording-playback";

}  // namespace app::config
