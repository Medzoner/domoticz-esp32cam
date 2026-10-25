#pragma once

#include <WiFi.h>

#include <atomic>

#include "config/camera_config.h"
#include "config/stream_config.h"
#include "core/stream/stream_adaptation.h"
#include "core/service/service_startup.h"
#include "ports/auth/authentication_port.h"
#include "ports/camera/camera_port.h"
#include "ports/recording/recording_port.h"

namespace app::adapters {

struct StreamStats {
  bool started;
  uint8_t clients;
  uint32_t sessions;
  uint32_t frames;
  uint16_t framesPerSecondTimes10;
  uint32_t stackHeadroomBytes;
  uint16_t averageDeliveryMs;
  uint8_t targetFps;
  uint8_t jpegQualityOffset;
  uint8_t pressureLevel;
  IPAddress peer;
};

class MjpegServer {
 public:
  MjpegServer(ports::CameraPort& camera, ports::RecordingPort& recordings,
              ports::AuthenticationPort& authentication);
  [[nodiscard]] bool begin();
  core::ServiceState state() const;
  void stopAll();
  void setBaseJpegQuality(uint8_t quality);
  uint8_t baseJpegQuality() const;
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
  bool drainRequestHeaders(WiFiClient& client);
  void refreshClientStats();
  void broadcastFrame();
  void refreshPerformanceStats();
  void updateAdaptiveProfile();
  void applyProfile(const core::StreamProfile& profile);

  ports::CameraPort& camera_;
  ports::RecordingPort& recordings_;
  ports::AuthenticationPort& authentication_;
  WiFiServer server_;
  WiFiClient clients_[config::kMaxStreamClients];
  // Accessed by both the HTTP task and stream task. Volatile is not C++
  // synchronization. Individual counters are safe, not a transactional snapshot.
  core::ServiceStartup startup_;
  std::atomic<bool> stopRequested_{false};
  std::atomic<uint8_t> clientCount_{0};
  std::atomic<uint32_t> sessionCount_{0};
  std::atomic<uint32_t> frameCount_{0};
  std::atomic<uint16_t> framesPerSecondTimes10_{0};
  std::atomic<uint32_t> stackHeadroomBytes_{0};
  std::atomic<uint16_t> averageDeliveryMs_{0};
  std::atomic<uint8_t> targetFps_{config::kTargetStreamFps};
  std::atomic<uint8_t> jpegQualityOffset_{0};
  std::atomic<uint8_t> pressureLevel_{0};
  std::atomic<uint8_t> baseJpegQuality_{config::kDefaultJpegQuality};
  std::atomic<uint32_t> peerAddress_{0};
  core::AdaptiveStreamController adaptiveController_;
  uint32_t statsStartedAt_ = 0;
  uint32_t statsStartedAtFrame_ = 0;
  uint32_t adaptationStartedAt_ = 0;
  uint32_t deliveryDurationTotalMs_ = 0;
  uint16_t deliverySamples_ = 0;
  uint16_t writeFailures_ = 0;
  std::atomic<uint8_t> appliedJpegQuality_{config::kDefaultJpegQuality};
};

}  // namespace app::adapters
