#pragma once

#include <cstdint>

namespace app::core {

struct StreamLoad {
  uint8_t clients;
  uint16_t averageDeliveryMs;
  int16_t wifiRssi;
  uint32_t freeHeapBytes;
  uint16_t writeFailures;
};

struct StreamProfile {
  uint8_t targetFps;
  uint8_t jpegQualityOffset;
  uint8_t pressureLevel;
};

class AdaptiveStreamController {
 public:
  StreamProfile update(const StreamLoad& load);
  StreamProfile reset();
  StreamProfile current() const;

 private:
  static uint8_t targetLevel(const StreamLoad& load);
  static StreamProfile profileForLevel(uint8_t level);

  uint8_t level_ = 0;
  uint8_t recoveryWindows_ = 0;
};

}  // namespace app::core
