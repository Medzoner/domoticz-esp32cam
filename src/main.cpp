#include <Arduino.h>
#include <ESPmDNS.h>

#include "adapters/esp32_camera.h"
#include "adapters/http_control_server.h"
#include "adapters/mjpeg_server.h"
#include "adapters/wifi_station.h"
#include "config/app_config.h"
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

}  // namespace

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println("\nESP32-CAM starting");

  if (!camera.begin()) {
    restartAfterFailure("Camera initialization failed");
  }
  while (!wifi.connect()) {
    Serial.println("WiFi connection failed, retrying");
    delay(5000);
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
