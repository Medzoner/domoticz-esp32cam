#pragma once

#include <cstddef>
#include <cstdint>

namespace app::config {

inline constexpr uint16_t kProvisioningHttpPort = 80;
inline constexpr uint16_t kProvisioningDnsPort = 53;
inline constexpr std::size_t kMaxProvisioningClients = 4;
inline constexpr uint32_t kProvisioningTimeoutMs = 600000;
inline constexpr uint32_t kProvisioningRestartDelayMs = 2500;
inline constexpr char kProvisioningAccessPointSuffix[] = "-setup";
inline constexpr char kProvisioningAccessPointName[] = "esp32cam-setup";

}  // namespace app::config
