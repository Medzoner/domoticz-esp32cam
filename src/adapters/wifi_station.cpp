#include "adapters/wifi_station.h"

#include <Arduino.h>
#include <WiFi.h>

#include "config/app_config.h"
#include "secrets.h"

namespace app::adapters {

bool WifiStation::begin() { return store_.load(credentials_); }

bool WifiStation::connect() {
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);
  WiFi.setHostname(CAMERA_HOSTNAME);

  if (credentials_.useDhcp) {
    const IPAddress automatic;
    WiFi.config(automatic, automatic, automatic, automatic);
  } else {
    const IPAddress ip(CAMERA_IP_1, CAMERA_IP_2, CAMERA_IP_3, CAMERA_IP_4);
    const IPAddress gateway(CAMERA_GATEWAY_1, CAMERA_GATEWAY_2,
                            CAMERA_GATEWAY_3, CAMERA_GATEWAY_4);
    const IPAddress subnet(255, 255, 255, 0);
    WiFi.config(ip, gateway, subnet, gateway);
  }

  WiFi.begin(credentials_.ssid, credentials_.password);

  Serial.printf("Connecting to %s", credentials_.ssid);
  const unsigned long startedAt = millis();
  while (WiFi.status() != WL_CONNECTED &&
         millis() - startedAt < config::kWifiConnectTimeoutMs) {
    delay(config::kWifiPollIntervalMs);
    Serial.print('.');
    // A rejected passphrase will not succeed later, so do not wait the full
    // timeout before letting the onboarding portal take over.
    if (WiFi.status() == WL_CONNECT_FAILED) {
      Serial.print(" authentication rejected");
      break;
    }
  }
  Serial.println();
  return WiFi.status() == WL_CONNECTED;
}

bool WifiStation::connectWithRetries(std::uint8_t attempts) {
  if (attempts == 0) {
    attempts = 1;
  }
  for (std::uint8_t attempt = 1; attempt <= attempts; ++attempt) {
    if (attempt > 1) {
      delay(config::kWifiRetryIntervalMs);
    }
    if (connect()) {
      return true;
    }
    Serial.printf("Wi-Fi attempt %u of %u failed\n",
                  static_cast<unsigned int>(attempt),
                  static_cast<unsigned int>(attempts));
  }
  return false;
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
