#pragma once

#include <Arduino.h>
#include <WiFi.h>

namespace app::adapters {

class WifiStation {
 public:
  bool connect();
  void maintain();
  IPAddress ipAddress() const;

 private:
  unsigned long lastRetryAt_ = 0;
};

}  // namespace app::adapters
