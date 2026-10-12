#include "adapters/mjpeg_server.h"

#include <Arduino.h>

#include "core/mjpeg_protocol.h"

namespace app::adapters {
MjpegServer::MjpegServer(ports::CameraPort& camera)
    : camera_(camera), server_(config::kStreamPort) {}

void MjpegServer::begin() {
  xTaskCreatePinnedToCore(taskEntry, config::kStreamTaskName,
                          config::kStreamTaskStackSize, this,
                          config::kStreamTaskPriority, &taskHandle_,
                          config::kStreamTaskCore);
}

void MjpegServer::stopAll() { stopRequested_ = true; }

StreamStats MjpegServer::stats() const {
  return {started_,
          clientCount_,
          sessionCount_,
          frameCount_,
          framesPerSecondTimes10_,
          stackHeadroomBytes_,
          IPAddress(peerAddress_)};
}

void MjpegServer::taskEntry(void* context) {
  static_cast<MjpegServer*>(context)->run();
}

void MjpegServer::run() {
  server_.begin();
  server_.setNoDelay(true);
  statsStartedAt_ = millis();
  statsStartedAtFrame_ = frameCount_;
  started_ = true;

  for (;;) {
    if (stopRequested_) {
      for (std::size_t index = 0; index < config::kMaxStreamClients;
           ++index) {
        closeClient(index);
      }
      stopRequested_ = false;
    }

    acceptClient();
    refreshClientStats();
    refreshPerformanceStats();
    if (clientCount_ == 0) {
      vTaskDelay(pdMS_TO_TICKS(config::kIdleDelayMs));
      continue;
    }
    const uint32_t frameStartedAt = millis();
    broadcastFrame();
    const uint32_t frameDuration = millis() - frameStartedAt;
    if (frameDuration < config::kStreamFrameIntervalMs) {
      vTaskDelay(
          pdMS_TO_TICKS(config::kStreamFrameIntervalMs - frameDuration));
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
  while (candidate.available()) {
    candidate.read();
  }
  if (!core::mjpeg::isStreamRequestLine(requestLine)) {
    candidate.print(core::mjpeg::kNotFoundResponse);
    candidate.stop();
    return;
  }

  for (std::size_t index = 0; index < config::kMaxStreamClients; ++index) {
    if (!clients_[index] || !clients_[index].connected()) {
      closeClient(index);
      clients_[index] = candidate;
      clients_[index].print(core::mjpeg::kSuccessResponse);
      ++sessionCount_;
      return;
    }
  }
  candidate.print(core::mjpeg::kUnavailableResponse);
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
  const uint32_t deadline = millis() + config::kClientWriteTimeoutMs;
  while (length > 0 && client.connected()) {
    const std::size_t block =
        min(length, static_cast<std::size_t>(config::kStreamWriteBlockSize));
    const std::size_t written = client.write(data, block);
    if (written == 0) {
      return false;
    }
    data += written;
    length -= written;
    if (static_cast<int32_t>(millis() - deadline) >= 0 && length > 0) {
      return false;
    }
  }
  return length == 0;
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
  stackHeadroomBytes_ = uxTaskGetStackHighWaterMark(taskHandle_);
  statsStartedAt_ = now;
  statsStartedAtFrame_ = frameCount_;
}

void MjpegServer::broadcastFrame() {
  ports::CameraFrame frame;
  if (!camera_.acquire(frame)) {
    return;
  }

  char partHeader[config::kPartHeaderCapacity];
  const std::size_t headerLength = core::mjpeg::formatPartHeader(
      partHeader, sizeof(partHeader), frame.size);
  if (headerLength == 0) {
    camera_.release(frame);
    return;
  }

  for (std::size_t index = 0; index < config::kMaxStreamClients; ++index) {
    WiFiClient& client = clients_[index];
    if (!client || !client.connected() ||
        !writeAll(client,
                  reinterpret_cast<const uint8_t*>(core::mjpeg::kBoundary),
                  sizeof(core::mjpeg::kBoundary) - 1) ||
        !writeAll(client, reinterpret_cast<const uint8_t*>(partHeader),
                  headerLength) ||
        !writeAll(client, frame.data, frame.size)) {
      closeClient(index);
    }
  }
  camera_.release(frame);
  ++frameCount_;
}

}  // namespace app::adapters
