#include <Arduino.h>
#include <ESPmDNS.h>

#include "adapters/esp32_camera.h"
#include "adapters/http_control_server.h"
#include "adapters/mjpeg_server.h"
#include "adapters/provisioning_portal.h"
#include "adapters/wifi_station.h"
#include "config/app_config.h"
#include "core/provisioning_rules.h"
#include "secrets.h"

namespace {

app::adapters::Esp32Camera camera;
app::adapters::WifiStation wifi;
app::adapters::MjpegServer streams(camera);
app::adapters::HttpControlServer controlServer(camera, streams);

[[noreturn]] void restartAfterFailure(const char* message) {
  Serial.println(message);
  delay(3000);
  ESP.restart();
  while (true) {
    delay(1000);
  }
}

// Blocks until the camera stores new credentials, then restarts. When there is
// nothing to retry the portal stays open instead of rebooting forever.
[[noreturn]] void runOnboardingPortal(bool canRetry) {
  app::adapters::ProvisioningPortal portal;
  if (!portal.begin(canRetry)) {
    restartAfterFailure("Onboarding portal initialization failed");
  }
  while (true) {
    portal.loop();
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println("\nESP32-CAM starting");

  if (!camera.begin()) {
    restartAfterFailure("Camera initialization failed");
  }

  const bool hasCredentials = wifi.begin();
  const bool connected =
      hasCredentials &&
      wifi.connectWithRetries(app::config::kWifiConnectAttempts);
  if (app::core::provisioning::decideBootMode(hasCredentials, connected) ==
      app::core::provisioning::BootMode::kProvisioning) {
    Serial.println(hasCredentials
                       ? "WiFi connection failed, opening the setup portal"
                       : "No WiFi configuration found, opening the setup portal");
    runOnboardingPortal(hasCredentials);
  }

  if (MDNS.begin(CAMERA_HOSTNAME)) {
    MDNS.addService(app::config::kMdnsService, app::config::kMdnsProtocol,
                    app::config::kControlPort);
  }
  streams.begin();
  if (!controlServer.begin()) {
    restartAfterFailure("HTTP server initialization failed");
  }

  Serial.print("Control: http://");
  Serial.println(wifi.ipAddress());
  Serial.print("Stream: http://");
  Serial.print(wifi.ipAddress());
  Serial.printf(":%u/stream\n", app::config::kStreamPort);
  Serial.printf("mDNS: http://%s.local\n", CAMERA_HOSTNAME);
}

void loop() {
  wifi.maintain();
  delay(250);
}
