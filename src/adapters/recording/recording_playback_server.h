#pragma once

#include <WiFi.h>

#include "ports/auth/authentication_port.h"
#include "ports/recording/recording_port.h"

namespace app::adapters {

class RecordingPlaybackServer {
 public:
  RecordingPlaybackServer(ports::RecordingPort& recordings,
                          ports::AuthenticationPort& authentication);
  void begin();

 private:
  static void taskEntry(void* context);
  void run();
  void handleClient(WiFiClient& client);
  bool play(WiFiClient& client, uint32_t index);
  bool download(WiFiClient& client, uint32_t index);
  bool readRequestLine(WiFiClient& client, char* destination,
                       std::size_t capacity);
  bool drainRequestHeaders(WiFiClient& client);
  bool writeAll(WiFiClient& client, const uint8_t* data, std::size_t length);
  bool readExact(uint8_t* destination, std::size_t length);

  ports::RecordingPort& recordings_;
  ports::AuthenticationPort& authentication_;
  WiFiServer server_;
  TaskHandle_t taskHandle_ = nullptr;
};

}  // namespace app::adapters
