#pragma once

#include <cstdint>

using TickType_t = uint32_t;
inline constexpr int pdTRUE = 1;
inline constexpr int pdFALSE = 0;
inline constexpr TickType_t pdMS_TO_TICKS(uint32_t ms) { return ms; }
