#include "adapters/stream/mjpeg_server.h"

#include <Arduino.h>

#include "core/auth/basic_auth.h"
#include "core/stream/mjpeg_protocol.h"
#include "adapters/network/bounded_socket_write.h"

namespace app::adapters {
MjpegServer::MjpegServer(ports::CameraPort& camera,
                         ports::RecordingPort& recordings,
                         ports::AuthenticationPort& authentication)
    : camera_(camera),
      recordings_(recordings),
      authentication_(authentication),
      server_(config::kStreamPort) {}

bool MjpegServer::begin() {
  return startup_.start([this] {
    const bool created = xTaskCreatePinnedToCore(taskEntry, config::kStreamTaskName,
                           config::kStreamTaskStackSize, this,
                           config::kStreamTaskPriority, nullptr,
                           config::kStreamTaskCore) == pdPASS;
    if (!created) {
      Serial.println("[stream] task creation failed");
    }
    return created;
  });
}

core::ServiceState MjpegServer::state() const { return startup_.state(); }

void MjpegServer::stopAll() { stopRequested_ = true; }

void MjpegServer::setBaseJpegQuality(uint8_t quality) {
  baseJpegQuality_ = quality;
  appliedJpegQuality_ = UINT8_MAX;
}

uint8_t MjpegServer::baseJpegQuality() const { return baseJpegQuality_; }

StreamStats MjpegServer::stats() const {
  return {state() == core::ServiceState::kRunning,
          clientCount_,
          sessionCount_,
          frameCount_,
          framesPerSecondTimes10_,
          stackHeadroomBytes_,
          averageDeliveryMs_,
          targetFps_,
          jpegQualityOffset_,
          pressureLevel_,
          IPAddress(peerAddress_)};
}

void MjpegServer::taskEntry(void* context) {
  static_cast<MjpegServer*>(context)->run();
}

void MjpegServer::run() {
  server_.begin();
  if (!server_) {
    startup_.fail();
    Serial.println("[stream] listener startup failed");
    server_.end();
    vTaskDelete(nullptr);
    return;
  }
  server_.setNoDelay(true);
  statsStartedAt_ = millis();
  statsStartedAtFrame_ = frameCount_;
  adaptationStartedAt_ = statsStartedAt_;
  startup_.ready();

  for (;;) {
    recordings_.service();
    if (stopRequested_.exchange(false)) {
      for (std::size_t index = 0; index < config::kMaxStreamClients;
           ++index) {
        closeClient(index);
      }
    }

    acceptClient();
    refreshClientStats();
    refreshPerformanceStats();
    if (clientCount_ == 0 && !recordings_.wantsFrames()) {
      vTaskDelay(pdMS_TO_TICKS(config::kIdleDelayMs));
      continue;
    }
    const uint32_t frameStartedAt = millis();
    broadcastFrame();
    const uint32_t frameDuration = millis() - frameStartedAt;
    deliveryDurationTotalMs_ += frameDuration;
    ++deliverySamples_;
    const uint32_t frameInterval = 1000UL / targetFps_;
    if (frameDuration < frameInterval) {
      vTaskDelay(pdMS_TO_TICKS(frameInterval - frameDuration));
    } else {
      taskYIELD();
    }
  }
}

void MjpegServer::acceptClient() {
  WiFiClient candidate = server_.available();
  if (!candidate) {
    return;
  }
  candidate.setTimeout(config::kClientReadTimeoutMs);
  candidate.setNoDelay(true);
  char requestLine[config::kRequestLineCapacity]{};
  if (!readRequestLine(candidate, requestLine, sizeof(requestLine))) {
    candidate.stop();
    return;
  }
  if (!drainRequestHeaders(candidate)) {
    candidate.stop();
    return;
  }
  if (!core::mjpeg::isStreamRequestLine(requestLine)) {
    writeAll(candidate,
             reinterpret_cast<const uint8_t*>(core::mjpeg::kNotFoundResponse),
             sizeof(core::mjpeg::kNotFoundResponse) - 1);
    candidate.stop();
    return;
  }

  for (std::size_t index = 0; index < config::kMaxStreamClients; ++index) {
    if (!clients_[index] || !clients_[index].connected()) {
      closeClient(index);
      clients_[index] = candidate;
      if (!writeAll(clients_[index],
                    reinterpret_cast<const uint8_t*>(core::mjpeg::kSuccessResponse),
                    sizeof(core::mjpeg::kSuccessResponse) - 1)) {
        closeClient(index);
        return;
      }
      ++sessionCount_;
      return;
    }
  }
  writeAll(candidate,
           reinterpret_cast<const uint8_t*>(core::mjpeg::kUnavailableResponse),
           sizeof(core::mjpeg::kUnavailableResponse) - 1);
  candidate.stop();
}

void MjpegServer::closeClient(std::size_t index) {
  if (clients_[index]) {
    clients_[index].stop();
  }
  clients_[index] = WiFiClient();
}

bool MjpegServer::writeAll(WiFiClient& client, const uint8_t* data,
                           std::size_t length) {
  return boundedSocketWrite(client, data, length, config::kStreamWriteBlockSize,
                            config::kClientWriteTimeoutMs);
}

bool MjpegServer::readRequestLine(WiFiClient& client, char* destination,
                                  std::size_t capacity) {
  if (capacity == 0) {
    return false;
  }
  const uint32_t deadline = millis() + config::kClientReadTimeoutMs;
  std::size_t length = 0;
  while (static_cast<int32_t>(millis() - deadline) < 0) {
    const int value = client.read();
    if (value < 0) {
      vTaskDelay(1);
      continue;
    }
    if (value == '\n') {
      destination[length] = '\0';
      return length > 0;
    }
    if (value != '\r') {
      if (length + 1 >= capacity) {
        return false;
      }
      destination[length++] = static_cast<char>(value);
    }
  }
  return false;
}

bool MjpegServer::drainRequestHeaders(WiFiClient& client) {
  // Reads the request headers up to and including the blank line that ends
  // them. Both the elapsed time and the number of bytes are bounded so that a
  // client streaming data forever can never stall the stream task.
  const uint32_t deadline = millis() + config::kClientReadTimeoutMs;
  std::size_t budget = config::kMaximumRequestHeaderBytes;
  char authorization[192]{};
  char cookie[192]{};
  char line[256]{};
  std::size_t lineLength = 0;

  while (static_cast<int32_t>(millis() - deadline) < 0) {
    if (client.available() <= 0) {
      vTaskDelay(1);
      continue;
    }
    const int value = client.read();
    if (value < 0) {
      return false;
    }
    if (budget == 0) {
      return false;
    }
    --budget;
    if (value == '\n') {
      if (lineLength == 0) {
        const ports::AuthenticationDecision decision = authentication_.authorize(
            authorization[0] == '\0' ? nullptr : authorization,
            cookie[0] == '\0' ? nullptr : cookie);
        if (!decision.granted) {
          writeAll(client,
                   reinterpret_cast<const uint8_t*>(core::mjpeg::kUnauthorizedResponse),
                   sizeof(core::mjpeg::kUnauthorizedResponse) - 1);
        }
        return decision.granted;
      }
      line[lineLength] = '\0';
      core::auth::captureHeader(line, authorization, sizeof(authorization),
                                cookie, sizeof(cookie));
      lineLength = 0;
    } else if (value != '\r') {
      if (lineLength + 1 >= sizeof(line)) {
        return false;
      }
      line[lineLength++] = static_cast<char>(value);
    }
  }
  return false;
}

void MjpegServer::refreshClientStats() {
  uint8_t count = 0;
  uint32_t peer = 0;
  for (auto& client : clients_) {
    if (client && client.connected()) {
      ++count;
      if (peer == 0) {
        peer = static_cast<uint32_t>(client.remoteIP());
      }
    }
  }
  clientCount_ = count;
  peerAddress_ = peer;
}

void MjpegServer::refreshPerformanceStats() {
  const uint32_t now = millis();
  const uint32_t elapsed = now - statsStartedAt_;
  if (elapsed < config::kStreamStatsIntervalMs) {
    return;
  }
  const uint32_t frames = frameCount_ - statsStartedAtFrame_;
  framesPerSecondTimes10_ =
      static_cast<uint16_t>((frames * 10000UL) / elapsed);
  stackHeadroomBytes_ = uxTaskGetStackHighWaterMark(nullptr);
  averageDeliveryMs_ = deliverySamples_ == 0
                           ? 0
                           : deliveryDurationTotalMs_ / deliverySamples_;
  deliveryDurationTotalMs_ = 0;
  deliverySamples_ = 0;
  statsStartedAt_ = now;
  statsStartedAtFrame_ = frameCount_;
  if (now - adaptationStartedAt_ >= config::kStreamAdaptationIntervalMs) {
    updateAdaptiveProfile();
    adaptationStartedAt_ = now;
  }
}

void MjpegServer::updateAdaptiveProfile() {
  if (clientCount_ == 0) {
    applyProfile(adaptiveController_.reset());
    writeFailures_ = 0;
    return;
  }
  const core::StreamLoad load{clientCount_, averageDeliveryMs_, WiFi.RSSI(),
                              ESP.getFreeHeap(), writeFailures_};
  writeFailures_ = 0;
  applyProfile(adaptiveController_.update(load));
}

void MjpegServer::applyProfile(const core::StreamProfile& profile) {
  targetFps_ = profile.targetFps;
  jpegQualityOffset_ = profile.jpegQualityOffset;
  pressureLevel_ = profile.pressureLevel;
  const uint8_t desiredQuality = static_cast<uint8_t>(min(
      static_cast<unsigned int>(config::kMaximumJpegQuality),
      static_cast<unsigned int>(baseJpegQuality_) + profile.jpegQualityOffset));
  if (desiredQuality != appliedJpegQuality_ &&
      camera_.apply(core::CameraSetting::kQuality, desiredQuality)) {
    appliedJpegQuality_ = desiredQuality;
  }
}

void MjpegServer::broadcastFrame() {
  ports::CameraFrame frame;
  if (!camera_.acquire(frame)) {
    return;
  }

  char partPreamble[config::kPartHeaderCapacity];
  const std::size_t preambleLength = core::mjpeg::formatPartPreamble(
      partPreamble, sizeof(partPreamble), frame.size);
  if (preambleLength == 0) {
    camera_.release(frame);
    return;
  }

  for (std::size_t index = 0; index < config::kMaxStreamClients; ++index) {
    WiFiClient& client = clients_[index];
    if (!client || !client.connected()) {
      closeClient(index);
      continue;
    }
    if (!writeAll(client, reinterpret_cast<const uint8_t*>(partPreamble),
                  preambleLength) ||
        !writeAll(client, frame.data, frame.size)) {
      ++writeFailures_;
      closeClient(index);
    }
  }
  if (recordings_.wantsFrames()) {
    recordings_.appendFrame(frame.data, frame.size);
  }
  camera_.release(frame);
  ++frameCount_;
}

}  // namespace app::adapters
