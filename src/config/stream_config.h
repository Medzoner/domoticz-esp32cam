#pragma once

#include <cstddef>
#include <cstdint>

namespace app::config {

inline constexpr uint16_t kStreamPort = 81;
inline constexpr std::size_t kMaxStreamClients = 4;
inline constexpr std::size_t kStreamWriteBlockSize = 16384;
inline constexpr uint32_t kStreamTaskStackSize = 3584;
inline constexpr uint32_t kStreamTaskPriority = 2;
inline constexpr uint32_t kStreamTaskCore = 0;
inline constexpr uint32_t kIdleDelayMs = 20;
inline constexpr uint32_t kTargetStreamFps = 10;
inline constexpr uint32_t kStreamFrameIntervalMs = 1000 / kTargetStreamFps;
inline constexpr uint32_t kStreamStatsIntervalMs = 1000;
inline constexpr uint32_t kStreamAdaptationIntervalMs = 2000;
inline constexpr uint32_t kClientReadTimeoutMs = 250;
inline constexpr uint32_t kClientWriteTimeoutMs = 300;
inline constexpr std::size_t kRequestLineCapacity = 128;
inline constexpr std::size_t kPartHeaderCapacity = 96;
inline constexpr std::size_t kMaximumRequestHeaderBytes = 1024;
inline constexpr char kStreamTaskName[] = "mjpeg-stream";

}  // namespace app::config
