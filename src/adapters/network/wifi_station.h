#pragma once

#include <Arduino.h>
#include <WiFi.h>

#include <cstdint>

#include "adapters/network/wifi_credentials.h"

namespace app::adapters {

class WifiStation {
 public:
  // Loads credentials from non-volatile storage, falling back to the values
  // compiled from include/secrets.h. Returns false when there is nothing to
  // try, which means the onboarding portal must take over.
  bool begin();

  // A single bounded connection attempt.
  bool connect();

  // Repeats connect() up to `attempts` times. Returns true on success.
  bool connectWithRetries(std::uint8_t attempts);

  void maintain();
  IPAddress ipAddress() const;

 private:
  WifiCredentialStore store_;
  WifiCredentials credentials_{};
  unsigned long lastRetryAt_ = 0;
};

}  // namespace app::adapters
