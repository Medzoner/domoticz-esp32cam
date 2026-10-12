#pragma once

#include <WiFi.h>

#include "config/app_config.h"
#include "ports/camera_port.h"

namespace app::adapters {

struct StreamStats {
  bool started;
  uint8_t clients;
  uint32_t sessions;
  uint32_t frames;
  uint16_t framesPerSecondTimes10;
  uint32_t stackHeadroomBytes;
  IPAddress peer;
};

class MjpegServer {
 public:
  explicit MjpegServer(ports::CameraPort& camera);
  void begin();
  void stopAll();
  StreamStats stats() const;

 private:
  static void taskEntry(void* context);
  void run();
  void acceptClient();
  void closeClient(std::size_t index);
  bool writeAll(WiFiClient& client, const uint8_t* data,
                std::size_t length);
  bool readRequestLine(WiFiClient& client, char* destination,
                       std::size_t capacity);
  void refreshClientStats();
  void broadcastFrame();
  void refreshPerformanceStats();

  ports::CameraPort& camera_;
  WiFiServer server_;
  WiFiClient clients_[config::kMaxStreamClients];
  volatile bool started_ = false;
  volatile bool stopRequested_ = false;
  volatile uint8_t clientCount_ = 0;
  volatile uint32_t sessionCount_ = 0;
  volatile uint32_t frameCount_ = 0;
  volatile uint16_t framesPerSecondTimes10_ = 0;
  volatile uint32_t stackHeadroomBytes_ = 0;
  volatile uint32_t peerAddress_ = 0;
  TaskHandle_t taskHandle_ = nullptr;
  uint32_t statsStartedAt_ = 0;
  uint32_t statsStartedAtFrame_ = 0;
};

}  // namespace app::adapters
