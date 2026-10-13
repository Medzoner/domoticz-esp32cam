#pragma once

#include <cstddef>
#include <cstdint>

namespace app::config {

inline constexpr uint16_t kControlPort = 80;
inline constexpr uint16_t kStreamPort = 81;
inline constexpr std::size_t kMaxControlClients = 7;
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
// Upper bound for the request headers drained before a stream is accepted.
// Matches CONFIG_HTTPD_MAX_REQ_HDR_LEN and stops a header flood from blocking
// the single stream task.
inline constexpr std::size_t kMaximumRequestHeaderBytes = 1024;
// Global budget for sending a still capture, so one slow client cannot hold
// the camera mutex or the HTTP task indefinitely.
inline constexpr uint32_t kCaptureSendBudgetMs = 3000;
// Minimum delay between two accepted camera mutations.
inline constexpr uint32_t kControlMinIntervalMs = 250;

inline constexpr uint32_t kWifiConnectTimeoutMs = 30000;
inline constexpr uint32_t kWifiRetryIntervalMs = 10000;
inline constexpr uint32_t kWifiPollIntervalMs = 500;
inline constexpr uint8_t kWifiConnectAttempts = 2;

// Onboarding portal, started only when no usable network configuration exists.
inline constexpr uint16_t kProvisioningHttpPort = 80;
inline constexpr uint16_t kProvisioningDnsPort = 53;
inline constexpr std::size_t kMaxProvisioningClients = 4;
inline constexpr uint32_t kProvisioningTimeoutMs = 600000;
inline constexpr uint32_t kProvisioningRestartDelayMs = 2500;
inline constexpr char kProvisioningAccessPointSuffix[] = "-setup";
inline constexpr char kProvisioningAccessPointName[] = "esp32cam-setup";

inline constexpr uint32_t kCameraLockTimeoutMs = 2000;
inline constexpr int kCameraClockHz = 20000000;
inline constexpr int kDefaultJpegQuality = 12;
inline constexpr uint8_t kMaximumJpegQuality = 63;

inline constexpr char kMdnsService[] = "http";
inline constexpr char kMdnsProtocol[] = "tcp";
inline constexpr char kStreamTaskName[] = "mjpeg-stream";

}  // namespace app::config
