#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "ports/camera/camera_port.h"

namespace app::adapters {

class Esp32Camera final : public ports::CameraPort {
 public:
  bool begin() override;
  bool acquire(ports::CameraFrame& frame) override;
  void release(ports::CameraFrame& frame) override;
  bool apply(core::CameraSetting setting, int value) override;
  core::CameraStatus status() override;

 private:
  SemaphoreHandle_t mutex_ = nullptr;
  bool flashEnabled_ = false;
};

}  // namespace app::adapters
