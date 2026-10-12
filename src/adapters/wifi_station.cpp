#include "adapters/wifi_station.h"

#include <WiFi.h>

#include "config/app_config.h"
#include "secrets.h"

namespace app::adapters {

bool WifiStation::connect() {
  const IPAddress ip(CAMERA_IP_1, CAMERA_IP_2, CAMERA_IP_3, CAMERA_IP_4);
  const IPAddress gateway(CAMERA_GATEWAY_1, CAMERA_GATEWAY_2,
                          CAMERA_GATEWAY_3, CAMERA_GATEWAY_4);
  const IPAddress subnet(255, 255, 255, 0);

  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);
  WiFi.setHostname(CAMERA_HOSTNAME);
  WiFi.config(ip, gateway, subnet, gateway);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.printf("Connecting to %s", WIFI_SSID);
  const unsigned long startedAt = millis();
  while (WiFi.status() != WL_CONNECTED &&
         millis() - startedAt < config::kWifiConnectTimeoutMs) {
    delay(config::kWifiPollIntervalMs);
    Serial.print('.');
  }
  Serial.println();
  return WiFi.status() == WL_CONNECTED;
}

void WifiStation::maintain() {
  if (WiFi.status() == WL_CONNECTED ||
      millis() - lastRetryAt_ < config::kWifiRetryIntervalMs) {
    return;
  }
  lastRetryAt_ = millis();
  Serial.println("WiFi disconnected, reconnecting");
  WiFi.reconnect();
}

IPAddress WifiStation::ipAddress() const { return WiFi.localIP(); }

}  // namespace app::adapters
