#pragma once

#include <cstdint>

namespace app::config {

inline constexpr uint32_t kWifiConnectTimeoutMs = 30000;
inline constexpr uint32_t kWifiRetryIntervalMs = 10000;
inline constexpr uint32_t kWifiPollIntervalMs = 500;
inline constexpr uint8_t kWifiConnectAttempts = 2;

}  // namespace app::config
