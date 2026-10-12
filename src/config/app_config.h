#pragma once

#include <cstddef>
#include <cstdint>

namespace app::config {

inline constexpr uint16_t kControlPort = 80;
inline constexpr uint16_t kStreamPort = 81;
inline constexpr std::size_t kMaxControlClients = 7;
inline constexpr std::size_t kMaxStreamClients = 4;
inline constexpr std::size_t kStreamWriteBlockSize = 4096;
inline constexpr uint32_t kStreamTaskStackSize = 3584;
inline constexpr uint32_t kStreamTaskPriority = 2;
inline constexpr uint32_t kStreamTaskCore = 0;
inline constexpr uint32_t kIdleDelayMs = 20;
inline constexpr uint32_t kTargetStreamFps = 10;
inline constexpr uint32_t kStreamFrameIntervalMs = 1000 / kTargetStreamFps;
inline constexpr uint32_t kStreamStatsIntervalMs = 1000;
inline constexpr uint32_t kClientReadTimeoutMs = 1000;
inline constexpr uint32_t kClientWriteTimeoutMs = 300;
inline constexpr std::size_t kRequestLineCapacity = 128;
inline constexpr std::size_t kPartHeaderCapacity = 96;

inline constexpr uint32_t kWifiConnectTimeoutMs = 30000;
inline constexpr uint32_t kWifiRetryIntervalMs = 10000;
inline constexpr uint32_t kWifiPollIntervalMs = 500;

inline constexpr uint32_t kCameraLockTimeoutMs = 2000;
inline constexpr int kCameraClockHz = 20000000;
inline constexpr int kDefaultJpegQuality = 12;

inline constexpr char kMdnsService[] = "http";
inline constexpr char kMdnsProtocol[] = "tcp";
inline constexpr char kStreamTaskName[] = "mjpeg-stream";

}  // namespace app::config
