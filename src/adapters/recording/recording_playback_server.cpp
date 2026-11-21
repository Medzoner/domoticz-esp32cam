#include "adapters/recording/recording_playback_server.h"

#include <Arduino.h>

#include <cstdlib>
#include <cstring>

#include "config/recording_config.h"
#include "config/stream_config.h"
#include "core/auth/basic_auth.h"
#include "core/recording/recording_rules.h"
#include "core/recording/recording_reader.h"
#include "core/recording/download_transfer.h"
#include "core/recording/playback_transfer.h"
#include "core/stream/mjpeg_protocol.h"
#include "adapters/network/bounded_socket_write.h"

namespace app::adapters {
namespace {

constexpr char kPlaybackResponse[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: multipart/x-mixed-replace;boundary=frame-boundary\r\n"
    "Cache-Control: no-store\r\n"
    "X-Content-Type-Options: nosniff\r\n\r\n";
constexpr char kNotFoundResponse[] =
    "HTTP/1.1 404 Not Found\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
constexpr char kBusyResponse[] =
    "HTTP/1.1 409 Conflict\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";

bool parseRequest(const char* requestLine, const char* route, uint32_t& index) {
  if (requestLine == nullptr || route == nullptr ||
      std::strncmp(requestLine, "GET ", 4) != 0) {
    return false;
  }
  const std::size_t routeLength = std::strlen(route);
  const char* target = requestLine + 4;
  if (std::strncmp(target, route, routeLength) != 0 ||
      std::strncmp(target + routeLength, "?id=", 4) != 0) {
    return false;
  }
  char* end = nullptr;
  const unsigned long parsed = std::strtoul(target + routeLength + 4, &end, 10);
  if (parsed == 0 || parsed > config::kMaximumRecordingSegmentIndex ||
      end == nullptr || std::strncmp(end, " HTTP/1.", 8) != 0) {
    return false;
  }
  index = static_cast<uint32_t>(parsed);
  return true;
}

}  // namespace

RecordingPlaybackServer::RecordingPlaybackServer(
    ports::RecordingPort& recordings,
    ports::AuthenticationPort& authentication)
    : recordings_(recordings),
      authentication_(authentication),
      server_(config::kPlaybackPort) {}

bool RecordingPlaybackServer::begin() {
  return startup_.start([this] {
    const bool created = xTaskCreatePinnedToCore(taskEntry, config::kPlaybackTaskName,
                           config::kPlaybackTaskStackSize, this,
                           config::kPlaybackTaskPriority, nullptr,
                           config::kPlaybackTaskCore) == pdPASS;
    if (!created) {
      Serial.println("[playback] task creation failed");
    }
    return created;
  });
}

core::ServiceState RecordingPlaybackServer::state() const {
  return startup_.state();
}

void RecordingPlaybackServer::taskEntry(void* context) {
  static_cast<RecordingPlaybackServer*>(context)->run();
}

void RecordingPlaybackServer::run() {
  server_.begin();
  if (!server_) {
    startup_.fail();
    Serial.println("[playback] listener startup failed");
    server_.end();
    vTaskDelete(nullptr);
    return;
  }
  server_.setNoDelay(true);
  startup_.ready();
  for (;;) {
    WiFiClient client = server_.available();
    if (!client) {
      vTaskDelay(pdMS_TO_TICKS(config::kIdleDelayMs));
      continue;
    }
    client.setTimeout(config::kClientReadTimeoutMs);
    client.setNoDelay(true);
    core::recording::recordingReaderSession(
        [&] { handleClient(client); }, [&] { client.stop(); },
        [&] { return recordings_.closeReader(); },
        [] { vTaskDelay(pdMS_TO_TICKS(config::kIdleDelayMs)); });
  }
}

void RecordingPlaybackServer::handleClient(WiFiClient& client) {
  char requestLine[config::kRequestLineCapacity]{};
  if (!readRequestLine(client, requestLine, sizeof(requestLine)) ||
      !drainRequestHeaders(client)) {
    return;
  }
  uint32_t index = 0;
  if (parseRequest(requestLine, "/play", index)) {
    play(client, index);
    return;
  }
  if (parseRequest(requestLine, "/download", index)) {
    download(client, index);
    return;
  }
  writeAll(client, reinterpret_cast<const uint8_t*>(kNotFoundResponse),
           sizeof(kNotFoundResponse) - 1);
}

bool RecordingPlaybackServer::play(WiFiClient& client, uint32_t index) {
  if (!recordings_.openReader(index)) {
    writeAll(client, reinterpret_cast<const uint8_t*>(kBusyResponse),
             sizeof(kBusyResponse) - 1);
    return false;
  }
  uint8_t magic[sizeof(core::recording::kFileMagic)]{};
  uint64_t fileSize = 0;
  if (!recordings_.readerSize(fileSize)) {
    writeAll(client, reinterpret_cast<const uint8_t*>(kBusyResponse),
             sizeof(kBusyResponse) - 1);
    return false;
  }
  if (fileSize < sizeof(magic) || !readExact(magic, sizeof(magic)) ||
      std::memcmp(magic, core::recording::kFileMagic, sizeof(magic)) != 0) {
    writeAll(client, reinterpret_cast<const uint8_t*>(kNotFoundResponse),
             sizeof(kNotFoundResponse) - 1);
    return false;
  }
  if (!writeAll(client, reinterpret_cast<const uint8_t*>(kPlaybackResponse),
                sizeof(kPlaybackResponse) - 1)) {
    return false;
  }

  uint64_t remainingFile = fileSize - sizeof(magic);
  core::recording::PlaybackBudget budget(
      config::kPlaybackFrameIntervalMs, config::kPlaybackFrameWriteTimeoutMs,
      config::kPlaybackSlowdownBudgetMs);
  uint32_t frameIndex = 0;
  while (client.connected()) {
    const auto deadline = budget.next(millis());
    if (deadline.expired(millis())) {
      Serial.println("[playback] slowdown allowance exhausted");
      return false;
    }
    const auto readFrame = [&](uint8_t* data, std::size_t size) {
      return core::recording::playbackRead(
          data, size, deadline, [] { return millis(); },
          [this](uint8_t* bytes, std::size_t count) {
            return recordings_.read(bytes, count);
          });
    };
    const auto frame = core::recording::nextFrame(
        remainingFile, config::kMaximumRecordedFrameBytes,
        readFrame);
    if (frame.status == core::recording::FrameReadStatus::kEnd) {
      return true;
    }
    if (frame.status != core::recording::FrameReadStatus::kFrame) {
      Serial.println("[playback] incomplete, invalid or unreadable frame; stopping");
      return false;
    }
    const uint32_t frameSize = frame.size;
    ++frameIndex;

    char preamble[config::kPartHeaderCapacity]{};
    const std::size_t preambleLength = core::mjpeg::formatPartPreamble(
        preamble, sizeof(preamble), frameSize);
    const char* failure = "frame deadline or slowdown allowance";
    const bool completed = core::recording::playbackFrameTransfer(
        reinterpret_cast<const uint8_t*>(preamble), preambleLength,
        frameSize, readBuffer_, sizeof(readBuffer_), deadline,
        [] { return millis(); },
        [&](uint8_t* data, std::size_t size) {
          if (core::recording::readExact(data, size, readFrame)) return true;
          failure = "SD payload read";
          return false;
        },
        [&](const uint8_t* data, std::size_t size, core::recording::PlaybackDeadline writeDeadline) {
          if (boundedSocketWriteFrom(client, data, size, config::kStreamWriteBlockSize,
                                     writeDeadline.budgetMs, config::kPlaybackWriteIdleTimeoutMs,
                                     writeDeadline.startedAt)) return true;
          failure = "socket write or timeout";
          return false;
        });
    if (!completed || !budget.complete(deadline, millis())) {
      Serial.printf("[playback] segment=%u frame=%u failed: %s\n",
                    static_cast<unsigned int>(index),
                    static_cast<unsigned int>(frameIndex), failure);
      return false;
    }
    const uint32_t elapsed = millis() - deadline.startedAt;
    if (elapsed < config::kPlaybackFrameIntervalMs) {
      vTaskDelay(pdMS_TO_TICKS(config::kPlaybackFrameIntervalMs - elapsed));
    }
  }
  return true;
}

bool RecordingPlaybackServer::download(WiFiClient& client, uint32_t index) {
  if (!recordings_.openReader(index)) {
    writeAll(client, reinterpret_cast<const uint8_t*>(kBusyResponse),
             sizeof(kBusyResponse) - 1);
    return false;
  }
  char header[256]{};
  uint64_t remaining = 0;
  if (!recordings_.readerSize(remaining)) {
    writeAll(client, reinterpret_cast<const uint8_t*>(kBusyResponse),
             sizeof(kBusyResponse) - 1);
    return false;
  }
  const int length = snprintf(
      header, sizeof(header),
      "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\n"
      "Content-Disposition: attachment; filename=recording-%05u.mjr\r\n"
      "Content-Length: %llu\r\nConnection: close\r\n\r\n",
      static_cast<unsigned int>(index),
      static_cast<unsigned long long>(remaining));
  if (length <= 0 || static_cast<std::size_t>(length) >= sizeof(header) ||
      !writeAll(client, reinterpret_cast<const uint8_t*>(header), length)) {
    return false;
  }
  const bool completed = core::recording::downloadTransfer(
      remaining, readBuffer_, sizeof(readBuffer_),
      config::kDownloadTransferTimeoutMs, [] { return millis(); },
      [&](uint8_t* data, std::size_t size) { return readExact(data, size); },
      [&](const uint8_t* data, std::size_t size, uint32_t remainingMs) {
        const uint32_t budget = remainingMs < config::kDownloadWriteTimeoutMs
                                    ? remainingMs : config::kDownloadWriteTimeoutMs;
        return boundedSocketWrite(client, data, size,
                                  config::kStreamWriteBlockSize, budget,
                                  config::kDownloadWriteIdleTimeoutMs);
      });
  if (!completed) {
    Serial.println("[download] SD/socket failure or deadline before Content-Length was reached");
  }
  return completed;
}

bool RecordingPlaybackServer::readRequestLine(WiFiClient& client,
                                               char* destination,
                                               std::size_t capacity) {
  if (destination == nullptr || capacity == 0) {
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

bool RecordingPlaybackServer::drainRequestHeaders(WiFiClient& client) {
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
    if (value < 0 || budget == 0) {
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

bool RecordingPlaybackServer::writeAll(WiFiClient& client,
                                       const uint8_t* data,
                                       std::size_t length) {
  return boundedSocketWrite(client, data, length, config::kStreamWriteBlockSize,
                            config::kClientWriteTimeoutMs);
}

bool RecordingPlaybackServer::readExact(uint8_t* destination,
                                        std::size_t length) {
  return core::recording::readExact(destination, length,
      [this](uint8_t* data, std::size_t size) { return recordings_.read(data, size); });
}

}  // namespace app::adapters
