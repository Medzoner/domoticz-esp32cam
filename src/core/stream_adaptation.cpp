#include "core/stream_adaptation.h"

namespace app::core {
namespace {

constexpr uint8_t kRecoveryWindowsRequired = 3;

}  // namespace

StreamProfile AdaptiveStreamController::update(const StreamLoad& load) {
  const uint8_t requestedLevel = targetLevel(load);
  if (requestedLevel > level_) {
    level_ = requestedLevel;
    recoveryWindows_ = 0;
  } else if (requestedLevel < level_) {
    ++recoveryWindows_;
    if (recoveryWindows_ >= kRecoveryWindowsRequired) {
      --level_;
      recoveryWindows_ = 0;
    }
  } else {
    recoveryWindows_ = 0;
  }
  return current();
}

StreamProfile AdaptiveStreamController::reset() {
  level_ = 0;
  recoveryWindows_ = 0;
  return current();
}

StreamProfile AdaptiveStreamController::current() const {
  return profileForLevel(level_);
}

uint8_t AdaptiveStreamController::targetLevel(const StreamLoad& load) {
  uint8_t score = 0;
  if (load.clients >= 4) {
    score += 3;
  } else if (load.clients == 3) {
    score += 2;
  } else if (load.clients == 2) {
    ++score;
  }

  if (load.averageDeliveryMs >= 100) {
    score += 2;
  } else if (load.averageDeliveryMs >= 75) {
    ++score;
  }

  if (load.wifiRssi <= -78) {
    score += 2;
  } else if (load.wifiRssi <= -68) {
    ++score;
  }

  if (load.freeHeapBytes < 120000) {
    score += 2;
  } else if (load.freeHeapBytes < 160000) {
    ++score;
  }

  if (load.writeFailures > 0) {
    score += 3;
  }

  if (score >= 8) {
    return 4;
  }
  if (score >= 6) {
    return 3;
  }
  if (score >= 4) {
    return 2;
  }
  if (score >= 2) {
    return 1;
  }
  return 0;
}

StreamProfile AdaptiveStreamController::profileForLevel(uint8_t level) {
  constexpr StreamProfile kProfiles[] = {
      {10, 0, 0}, {9, 1, 1}, {8, 2, 2}, {6, 4, 3}, {4, 6, 4}};
  constexpr uint8_t kLastProfile =
      static_cast<uint8_t>(sizeof(kProfiles) / sizeof(kProfiles[0]) - 1);
  return kProfiles[level > kLastProfile ? kLastProfile : level];
}

}  // namespace app::core
