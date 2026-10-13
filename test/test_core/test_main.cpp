#include <unity.h>

#include <cstring>

#include "core/camera_settings.h"
#include "core/http_security.h"
#include "core/mjpeg_protocol.h"
#include "core/provisioning_rules.h"
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

namespace provisioning = app::core::provisioning;

void testDecodesFormComponents() {
  char output[16]{};
  TEST_ASSERT_EQUAL_UINT(3, provisioning::urlDecode("a+b", 3, output, sizeof(output)));
  TEST_ASSERT_EQUAL_STRING("a b", output);
  TEST_ASSERT_EQUAL_UINT(3, provisioning::urlDecode("a%20b", 5, output, sizeof(output)));
  TEST_ASSERT_EQUAL_STRING("a b", output);
  TEST_ASSERT_EQUAL_UINT(0, provisioning::urlDecode("a%2", 3, output, sizeof(output)));
  TEST_ASSERT_EQUAL_UINT(0, provisioning::urlDecode("abcdef", 6, output, 4));
}

void testParsesProvisioningForm() {
  provisioning::Request request;
  const char* body = "ssid=My+Net&pass=secret123&dhcp=1";
  TEST_ASSERT_EQUAL(static_cast<int>(provisioning::ParseStatus::kOk),
                    static_cast<int>(
                        provisioning::parseFormBody(body, std::strlen(body), request)));
  TEST_ASSERT_EQUAL_STRING("My Net", request.ssid);
  TEST_ASSERT_EQUAL_STRING("secret123", request.password);
  TEST_ASSERT_TRUE(request.useDhcp);
}

void testAcceptsOpenNetworkWithDhcpDefaults() {
  provisioning::Request request;
  const char* body = "ssid=Cafe+Wifi";
  TEST_ASSERT_EQUAL(static_cast<int>(provisioning::ParseStatus::kOk),
                    static_cast<int>(
                        provisioning::parseFormBody(body, std::strlen(body), request)));
  TEST_ASSERT_EQUAL_STRING("Cafe Wifi", request.ssid);
  TEST_ASSERT_EQUAL_STRING("", request.password);
  TEST_ASSERT_TRUE(request.useDhcp);

  const char* unchecked = "ssid=Cafe&dhcp=0";
  TEST_ASSERT_EQUAL(static_cast<int>(provisioning::ParseStatus::kOk),
                    static_cast<int>(provisioning::parseFormBody(
                        unchecked, std::strlen(unchecked), request)));
  TEST_ASSERT_FALSE(request.useDhcp);

  // The onboarding form posts a hidden "0" before the checkbox "1".
  const char* checked = "ssid=Cafe&dhcp=0&dhcp=1";
  TEST_ASSERT_EQUAL(static_cast<int>(provisioning::ParseStatus::kOk),
                    static_cast<int>(provisioning::parseFormBody(
                        checked, std::strlen(checked), request)));
  TEST_ASSERT_TRUE(request.useDhcp);
}

void testRejectsInvalidProvisioningForms() {
  provisioning::Request request;
  const char* noSsid = "pass=secret123";
  TEST_ASSERT_EQUAL(
      static_cast<int>(provisioning::ParseStatus::kMissingSsid),
      static_cast<int>(provisioning::parseFormBody(noSsid, std::strlen(noSsid),
                                                   request)));

  const char* shortPassword = "ssid=Net&pass=1234";
  TEST_ASSERT_EQUAL(
      static_cast<int>(provisioning::ParseStatus::kInvalidPassword),
      static_cast<int>(provisioning::parseFormBody(
          shortPassword, std::strlen(shortPassword), request)));

  const char* longSsid = "ssid=012345678901234567890123456789012";
  TEST_ASSERT_EQUAL(
      static_cast<int>(provisioning::ParseStatus::kInvalidSsid),
      static_cast<int>(provisioning::parseFormBody(longSsid,
                                                   std::strlen(longSsid), request)));
}

void testValidatesProvisioningSecrets() {
  TEST_ASSERT_TRUE(provisioning::isValidPassword("", 0));
  TEST_ASSERT_TRUE(provisioning::isValidPassword("12345678", 8));
  TEST_ASSERT_FALSE(provisioning::isValidPassword("12345", 5));
  TEST_ASSERT_TRUE(provisioning::isValidSsid("camera", 6));
  TEST_ASSERT_FALSE(provisioning::isValidSsid("", 0));
  TEST_ASSERT_TRUE(provisioning::isUnconfigured("your-wifi"));
  TEST_ASSERT_FALSE(provisioning::isUnconfigured("home-network"));
}

void testRecognizesCaptivePortalProbes() {
  TEST_ASSERT_TRUE(provisioning::isCaptivePortalProbe("/generate_204"));
  TEST_ASSERT_TRUE(provisioning::isCaptivePortalProbe("/hotspot-detect.html?x=1"));
  TEST_ASSERT_TRUE(provisioning::isCaptivePortalProbe("/connecttest.txt"));
  TEST_ASSERT_FALSE(provisioning::isCaptivePortalProbe("/"));
  TEST_ASSERT_FALSE(provisioning::isCaptivePortalProbe("/stream"));
  TEST_ASSERT_FALSE(provisioning::isCaptivePortalProbe(nullptr));
}

void testDecidesBootMode() {
  using provisioning::BootMode;
  TEST_ASSERT_EQUAL(static_cast<int>(BootMode::kProvisioning),
                    static_cast<int>(provisioning::decideBootMode(false, false)));
  TEST_ASSERT_EQUAL(static_cast<int>(BootMode::kProvisioning),
                    static_cast<int>(provisioning::decideBootMode(true, false)));
  TEST_ASSERT_EQUAL(static_cast<int>(BootMode::kProvisioning),
                    static_cast<int>(provisioning::decideBootMode(false, true)));
  TEST_ASSERT_EQUAL(static_cast<int>(BootMode::kStation),
                    static_cast<int>(provisioning::decideBootMode(true, true)));
}

void testEncodesBasicAuthorizationValue() {
  char output[32]{};
  TEST_ASSERT_EQUAL_UINT32(
      16, app::core::security::base64Encode("admin:secret", 12, output,
                                            sizeof(output)));
  TEST_ASSERT_EQUAL_STRING("YWRtaW46c2VjcmV0", output);

  char padded[8]{};
  TEST_ASSERT_EQUAL_UINT32(
      4, app::core::security::base64Encode("a", 1, padded, sizeof(padded)));
  TEST_ASSERT_EQUAL_STRING("YQ==", padded);
  TEST_ASSERT_EQUAL_UINT32(
      4, app::core::security::base64Encode("ab", 2, padded, sizeof(padded)));
  TEST_ASSERT_EQUAL_STRING("YWI=", padded);
}

void testRejectsAnUndersizedBase64Buffer() {
  char output[4]{};
  TEST_ASSERT_EQUAL_UINT32(
      0, app::core::security::base64Encode("admin:secret", 12, output,
                                           sizeof(output)));
  TEST_ASSERT_EQUAL_UINT32(
      0, app::core::security::base64Encode(nullptr, 12, output,
                                           sizeof(output)));
}

void testComparesAuthorizationValuesInConstantTime() {
  using app::core::security::constantTimeEquals;
  TEST_ASSERT_TRUE(constantTimeEquals("abc", 3, "abc", 3));
  TEST_ASSERT_FALSE(constantTimeEquals("abc", 3, "abd", 3));
  TEST_ASSERT_FALSE(constantTimeEquals("abc", 3, "ab", 2));
  TEST_ASSERT_FALSE(constantTimeEquals(nullptr, 0, "abc", 3));
  TEST_ASSERT_FALSE(constantTimeEquals("abc", 3, nullptr, 0));
}

void testRejectsNonNumericControlValues() {
  using app::core::security::parseInteger;
  int value = -1;
  TEST_ASSERT_TRUE(parseInteger("12", value));
  TEST_ASSERT_EQUAL_INT(12, value);
  TEST_ASSERT_TRUE(parseInteger("-5", value));
  TEST_ASSERT_EQUAL_INT(-5, value);
  TEST_ASSERT_TRUE(parseInteger("0", value));
  TEST_ASSERT_EQUAL_INT(0, value);
  TEST_ASSERT_FALSE(parseInteger("abc", value));
  TEST_ASSERT_FALSE(parseInteger("12abc", value));
  TEST_ASSERT_FALSE(parseInteger("", value));
  TEST_ASSERT_FALSE(parseInteger("2147483648", value));
}

void testAcceptsRequestsFromTheCameraOrigin() {
  using app::core::security::isSameOrigin;
  TEST_ASSERT_TRUE(
      isSameOrigin("http://octoprint-cam.local", nullptr, "octoprint-cam.local"));
  TEST_ASSERT_TRUE(
      isSameOrigin("http://OCTOPRINT-CAM.LOCAL", nullptr, "octoprint-cam.local"));
  TEST_ASSERT_TRUE(isSameOrigin("http://octoprint-cam.local:80", nullptr,
                                "octoprint-cam.local"));
  TEST_ASSERT_TRUE(isSameOrigin(nullptr, "http://octoprint-cam.local/#/view",
                                "octoprint-cam.local"));
  TEST_ASSERT_TRUE(isSameOrigin(nullptr, nullptr, "octoprint-cam.local"));
  TEST_ASSERT_FALSE(
      isSameOrigin("http://evil.example.com", nullptr, "octoprint-cam.local"));
  TEST_ASSERT_FALSE(
      isSameOrigin(nullptr, "http://evil.example.com/", "octoprint-cam.local"));
  TEST_ASSERT_FALSE(isSameOrigin("null", nullptr, "octoprint-cam.local"));
  TEST_ASSERT_FALSE(isSameOrigin(nullptr, nullptr, nullptr));
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
  RUN_TEST(testDecodesFormComponents);
  RUN_TEST(testParsesProvisioningForm);
  RUN_TEST(testAcceptsOpenNetworkWithDhcpDefaults);
  RUN_TEST(testRejectsInvalidProvisioningForms);
  RUN_TEST(testValidatesProvisioningSecrets);
  RUN_TEST(testRecognizesCaptivePortalProbes);
  RUN_TEST(testDecidesBootMode);
  RUN_TEST(testEncodesBasicAuthorizationValue);
  RUN_TEST(testRejectsAnUndersizedBase64Buffer);
  RUN_TEST(testComparesAuthorizationValuesInConstantTime);
  RUN_TEST(testRejectsNonNumericControlValues);
  RUN_TEST(testAcceptsRequestsFromTheCameraOrigin);
  return UNITY_END();
}
