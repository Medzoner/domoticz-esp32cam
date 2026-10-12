#include <unity.h>

#include <cstring>

#include "core/camera_settings.h"
#include "core/mjpeg_protocol.h"
#include "core/stream_adaptation.h"

namespace {

void testParsesKnownCameraSettings() {
  app::core::CameraSetting setting{};
  TEST_ASSERT_TRUE(app::core::parseCameraSetting("framesize", setting));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(app::core::CameraSetting::kFrameSize),
                        static_cast<int>(setting));
  TEST_ASSERT_TRUE(app::core::parseCameraSetting("hmirror", setting));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(app::core::CameraSetting::kHorizontalMirror),
      static_cast<int>(setting));
}

void testRejectsUnknownCameraSetting() {
  app::core::CameraSetting setting{};
  TEST_ASSERT_FALSE(app::core::parseCameraSetting(nullptr, setting));
  TEST_ASSERT_FALSE(app::core::parseCameraSetting("unsupported", setting));
}

void testValidatesCameraSettingRanges() {
  using app::core::CameraSetting;
  TEST_ASSERT_TRUE(
      app::core::isCameraSettingValueValid(CameraSetting::kQuality, 12));
  TEST_ASSERT_FALSE(
      app::core::isCameraSettingValueValid(CameraSetting::kQuality, 64));
  TEST_ASSERT_TRUE(
      app::core::isCameraSettingValueValid(CameraSetting::kBrightness, -2));
  TEST_ASSERT_FALSE(
      app::core::isCameraSettingValueValid(CameraSetting::kBrightness, -3));
  TEST_ASSERT_TRUE(
      app::core::isCameraSettingValueValid(CameraSetting::kVerticalFlip, 1));
  TEST_ASSERT_FALSE(
      app::core::isCameraSettingValueValid(CameraSetting::kVerticalFlip, 2));
}

void testRecognizesOnlyTheStreamRoute() {
  TEST_ASSERT_TRUE(
      app::core::mjpeg::isStreamRequestLine("GET /stream HTTP/1.1\r"));
  TEST_ASSERT_TRUE(
      app::core::mjpeg::isStreamRequestLine("GET /stream?token=x HTTP/1.1\r"));
  TEST_ASSERT_FALSE(
      app::core::mjpeg::isStreamRequestLine("GET /streaming HTTP/1.1\r"));
  TEST_ASSERT_FALSE(
      app::core::mjpeg::isStreamRequestLine("POST /stream HTTP/1.1\r"));
}

void testFormatsMjpegPartPreamble() {
  char preamble[96]{};
  const std::size_t length =
      app::core::mjpeg::formatPartPreamble(preamble, sizeof(preamble), 12345);
  TEST_ASSERT_GREATER_THAN(0, length);
  TEST_ASSERT_NOT_NULL(std::strstr(preamble, "--frame-boundary"));
  TEST_ASSERT_NOT_NULL(std::strstr(preamble, "Content-Type: image/jpeg"));
  TEST_ASSERT_NOT_NULL(std::strstr(preamble, "Content-Length: 12345"));
}

void testRejectsAnUndersizedPreambleBuffer() {
  char preamble[8] = "initial";
  TEST_ASSERT_EQUAL_UINT32(
      0, app::core::mjpeg::formatPartPreamble(preamble, sizeof(preamble), 12345));
  TEST_ASSERT_EQUAL_CHAR('\0', preamble[0]);
}

void testKeepsFullPerformanceUnderHealthyLoad() {
  app::core::AdaptiveStreamController controller;
  const app::core::StreamProfile profile =
      controller.update({1, 45, -62, 180000, 0});
  TEST_ASSERT_EQUAL_UINT8(10, profile.targetFps);
  TEST_ASSERT_EQUAL_UINT8(0, profile.jpegQualityOffset);
  TEST_ASSERT_EQUAL_UINT8(0, profile.pressureLevel);
}

void testReducesLoadImmediatelyUnderPressure() {
  app::core::AdaptiveStreamController controller;
  const app::core::StreamProfile profile =
      controller.update({3, 120, -70, 175000, 0});
  TEST_ASSERT_EQUAL_UINT8(8, profile.targetFps);
  TEST_ASSERT_EQUAL_UINT8(2, profile.jpegQualityOffset);
  TEST_ASSERT_EQUAL_UINT8(2, profile.pressureLevel);
}

void testUsesEmergencyProfileForResourceFailures() {
  app::core::AdaptiveStreamController controller;
  const app::core::StreamProfile profile =
      controller.update({4, 180, -82, 100000, 1});
  TEST_ASSERT_EQUAL_UINT8(4, profile.targetFps);
  TEST_ASSERT_EQUAL_UINT8(6, profile.jpegQualityOffset);
  TEST_ASSERT_EQUAL_UINT8(4, profile.pressureLevel);
}

void testRecoversGraduallyAfterPressureDrops() {
  app::core::AdaptiveStreamController controller;
  controller.update({3, 120, -70, 175000, 0});
  const app::core::StreamLoad healthy{1, 45, -62, 180000, 0};
  controller.update(healthy);
  TEST_ASSERT_EQUAL_UINT8(2, controller.update(healthy).pressureLevel);
  TEST_ASSERT_EQUAL_UINT8(1, controller.update(healthy).pressureLevel);
  controller.update(healthy);
  controller.update(healthy);
  TEST_ASSERT_EQUAL_UINT8(0, controller.update(healthy).pressureLevel);
}

}  // namespace

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(testParsesKnownCameraSettings);
  RUN_TEST(testRejectsUnknownCameraSetting);
  RUN_TEST(testValidatesCameraSettingRanges);
  RUN_TEST(testRecognizesOnlyTheStreamRoute);
  RUN_TEST(testFormatsMjpegPartPreamble);
  RUN_TEST(testRejectsAnUndersizedPreambleBuffer);
  RUN_TEST(testKeepsFullPerformanceUnderHealthyLoad);
  RUN_TEST(testReducesLoadImmediatelyUnderPressure);
  RUN_TEST(testUsesEmergencyProfileForResourceFailures);
  RUN_TEST(testRecoversGraduallyAfterPressureDrops);
  return UNITY_END();
}
