#pragma once

#include <cstdint>

namespace app::core {

enum class CameraSetting : uint8_t {
  kFrameSize,
  kQuality,
  kBrightness,
  kContrast,
  kSaturation,
  kVerticalFlip,
  kHorizontalMirror,
  kFlash,
};

struct CameraStatus {
  uint8_t frameSize;
  uint8_t quality;
  int8_t brightness;
  int8_t contrast;
  int8_t saturation;
  bool verticalFlip;
  bool horizontalMirror;
  bool flashEnabled;
};

bool parseCameraSetting(const char* name, CameraSetting& setting);
bool isCameraSettingValueValid(CameraSetting setting, int value);

}  // namespace app::core
