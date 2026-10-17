#include "adapters/camera/esp32_camera.h"

#include <Arduino.h>
#include <esp_camera.h>

#include "config/camera_config.h"

#include "adapters/camera/camera_pins.h"

#ifndef CAMERA_DEFAULT_VFLIP
#define CAMERA_DEFAULT_VFLIP 0
#endif

#ifndef CAMERA_DEFAULT_HMIRROR
#define CAMERA_DEFAULT_HMIRROR 0
#endif

namespace app::adapters {
namespace {

camera_config_t makeCameraConfig() {
  camera_config_t config{};
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = config::kCameraClockHz;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_VGA;
  config.jpeg_quality = config::kDefaultJpegQuality;
  config.fb_count = 1;
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_DRAM;

  if (psramFound()) {
    config.fb_count = 2;
    config.grab_mode = CAMERA_GRAB_LATEST;
    config.fb_location = CAMERA_FB_IN_PSRAM;
  }
  return config;
}

}  // namespace

bool Esp32Camera::begin() {
  mutex_ = xSemaphoreCreateMutex();
  if (mutex_ == nullptr) {
    return false;
  }
#if defined(CAMERA_MODEL_ESP_EYE)
  pinMode(13, INPUT_PULLUP);
  pinMode(14, INPUT_PULLUP);
#endif
  camera_config_t config = makeCameraConfig();
  if (esp_camera_init(&config) != ESP_OK) {
    return false;
  }
#if CAMERA_FLASH_GPIO >= 0
  pinMode(CAMERA_FLASH_GPIO, OUTPUT);
  digitalWrite(CAMERA_FLASH_GPIO, LOW);
#endif
  sensor_t* sensor = esp_camera_sensor_get();
  return sensor->set_vflip(sensor, CAMERA_DEFAULT_VFLIP) == 0 &&
         sensor->set_hmirror(sensor, CAMERA_DEFAULT_HMIRROR) == 0;
}

bool Esp32Camera::acquire(ports::CameraFrame& frame) {
  if (mutex_ == nullptr ||
      xSemaphoreTake(mutex_, pdMS_TO_TICKS(config::kCameraLockTimeoutMs)) !=
          pdTRUE) {
    return false;
  }
  camera_fb_t* nativeFrame = esp_camera_fb_get();
  if (nativeFrame == nullptr) {
    xSemaphoreGive(mutex_);
    return false;
  }
  frame.data = nativeFrame->buf;
  frame.size = nativeFrame->len;
  frame.nativeHandle = nativeFrame;
  return true;
}

void Esp32Camera::release(ports::CameraFrame& frame) {
  if (frame.nativeHandle != nullptr) {
    esp_camera_fb_return(static_cast<camera_fb_t*>(frame.nativeHandle));
  }
  frame = {};
  if (mutex_ != nullptr) {
    xSemaphoreGive(mutex_);
  }
}

bool Esp32Camera::apply(core::CameraSetting setting, int value) {
  if (!core::isCameraSettingValueValid(setting, value) || mutex_ == nullptr ||
      xSemaphoreTake(mutex_, pdMS_TO_TICKS(config::kCameraLockTimeoutMs)) !=
          pdTRUE) {
    return false;
  }
  sensor_t* sensor = esp_camera_sensor_get();
  int result = -1;
  switch (setting) {
    case core::CameraSetting::kFrameSize:
      result = sensor->set_framesize(sensor, static_cast<framesize_t>(value));
      break;
    case core::CameraSetting::kQuality:
      result = sensor->set_quality(sensor, value);
      break;
    case core::CameraSetting::kBrightness:
      result = sensor->set_brightness(sensor, value);
      break;
    case core::CameraSetting::kContrast:
      result = sensor->set_contrast(sensor, value);
      break;
    case core::CameraSetting::kSaturation:
      result = sensor->set_saturation(sensor, value);
      break;
    case core::CameraSetting::kVerticalFlip:
      result = sensor->set_vflip(sensor, value);
      break;
    case core::CameraSetting::kHorizontalMirror:
      result = sensor->set_hmirror(sensor, value);
      break;
    case core::CameraSetting::kFlash:
#if CAMERA_FLASH_GPIO >= 0
      flashEnabled_ = value == 1;
      digitalWrite(CAMERA_FLASH_GPIO, flashEnabled_ ? HIGH : LOW);
      result = 0;
#endif
      break;
  }
  xSemaphoreGive(mutex_);
  return result == 0;
}

core::CameraStatus Esp32Camera::status() {
  core::CameraStatus result{};
  if (mutex_ == nullptr ||
      xSemaphoreTake(mutex_, pdMS_TO_TICKS(config::kCameraLockTimeoutMs)) !=
          pdTRUE) {
    return result;
  }
  const sensor_t* sensor = esp_camera_sensor_get();
  result.frameSize = sensor->status.framesize;
  result.quality = sensor->status.quality;
  result.brightness = sensor->status.brightness;
  result.contrast = sensor->status.contrast;
  result.saturation = sensor->status.saturation;
  result.verticalFlip = sensor->status.vflip;
  result.horizontalMirror = sensor->status.hmirror;
  result.flashEnabled = flashEnabled_;
  xSemaphoreGive(mutex_);
  return result;
}

}  // namespace app::adapters
