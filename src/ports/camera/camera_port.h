#pragma once

#include <cstddef>
#include <cstdint>

#include "core/camera/camera_settings.h"

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
  // Success transfers a lease to the caller, even if validation later rejects
  // the frame. Release exactly once before using camera controls or doing I/O.
  // Failure transfers no lease. Frame bytes are valid only until release.
  virtual bool acquire(CameraFrame& frame) = 0;
  virtual void release(CameraFrame& frame) = 0;
  virtual bool apply(core::CameraSetting setting, int value) = 0;
  virtual core::CameraStatus status() = 0;
};

}  // namespace app::ports
