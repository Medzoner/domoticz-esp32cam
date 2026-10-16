#pragma once

#include <cstddef>
#include <cstdint>

namespace app::config {

inline constexpr uint16_t kControlPort = 80;
inline constexpr std::size_t kMaxControlClients = 7;
inline constexpr std::size_t kMaxControlRoutes = 12;
inline constexpr uint32_t kCaptureSendBudgetMs = 3000;
inline constexpr uint32_t kControlMinIntervalMs = 250;
inline constexpr bool kLogHttpRequests = false;

}  // namespace app::config
