#pragma once

#include <cstddef>
#include <cstdint>

#include "core/camera_settings.h"

namespace app::ports {

struct CameraFrame {
  const uint8_t* data = nullptr;
  std::size_t size = 0;
  void* nativeHandle = nullptr;
};

class CameraPort {
 public:
  virtual ~CameraPort() = default;
  virtual bool begin() = 0;
  virtual bool acquire(CameraFrame& frame) = 0;
  virtual void release(CameraFrame& frame) = 0;
  virtual bool apply(core::CameraSetting setting, int value) = 0;
  virtual core::CameraStatus status() = 0;
};

}  // namespace app::ports
