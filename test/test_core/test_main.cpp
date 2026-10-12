#include <unity.h>

#include <cstring>

#include "core/camera_settings.h"
#include "core/mjpeg_protocol.h"

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

void testFormatsMjpegPartHeader() {
  char header[96]{};
  const std::size_t length =
      app::core::mjpeg::formatPartHeader(header, sizeof(header), 12345);
  TEST_ASSERT_GREATER_THAN(0, length);
  TEST_ASSERT_NOT_NULL(std::strstr(header, "Content-Type: image/jpeg"));
  TEST_ASSERT_NOT_NULL(std::strstr(header, "Content-Length: 12345"));
}

void testRejectsAnUndersizedHeaderBuffer() {
  char header[8] = "initial";
  TEST_ASSERT_EQUAL_UINT32(
      0, app::core::mjpeg::formatPartHeader(header, sizeof(header), 12345));
  TEST_ASSERT_EQUAL_CHAR('\0', header[0]);
}

}  // namespace

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(testParsesKnownCameraSettings);
  RUN_TEST(testRejectsUnknownCameraSetting);
  RUN_TEST(testValidatesCameraSettingRanges);
  RUN_TEST(testRecognizesOnlyTheStreamRoute);
  RUN_TEST(testFormatsMjpegPartHeader);
  RUN_TEST(testRejectsAnUndersizedHeaderBuffer);
  return UNITY_END();
}
