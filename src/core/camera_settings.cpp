#include "core/camera_settings.h"

#include <cstring>

namespace app::core {
namespace {

struct SettingName {
  const char* name;
  CameraSetting setting;
};

constexpr SettingName kSettingNames[] = {
    {"framesize", CameraSetting::kFrameSize},
    {"quality", CameraSetting::kQuality},
    {"brightness", CameraSetting::kBrightness},
    {"contrast", CameraSetting::kContrast},
    {"saturation", CameraSetting::kSaturation},
    {"vflip", CameraSetting::kVerticalFlip},
    {"hmirror", CameraSetting::kHorizontalMirror},
};

bool isBetween(int value, int minimum, int maximum) {
  return value >= minimum && value <= maximum;
}

}  // namespace

bool parseCameraSetting(const char* name, CameraSetting& setting) {
  if (name == nullptr) {
    return false;
  }
  for (const auto& candidate : kSettingNames) {
    if (std::strcmp(name, candidate.name) == 0) {
      setting = candidate.setting;
      return true;
    }
  }
  return false;
}

bool isCameraSettingValueValid(CameraSetting setting, int value) {
  switch (setting) {
    case CameraSetting::kFrameSize:
      return isBetween(value, 0, 13);
    case CameraSetting::kQuality:
      return isBetween(value, 4, 63);
    case CameraSetting::kBrightness:
    case CameraSetting::kContrast:
    case CameraSetting::kSaturation:
      return isBetween(value, -2, 2);
    case CameraSetting::kVerticalFlip:
    case CameraSetting::kHorizontalMirror:
      return value == 0 || value == 1;
  }
  return false;
}

}  // namespace app::core
