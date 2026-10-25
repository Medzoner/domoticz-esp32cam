#pragma once

#include <WiFi.h>

#include <cerrno>
#include <lwip/sockets.h>

#include "core/stream/bounded_write.h"

namespace app::adapters {

// WiFiClient::write() has its own internal retry loop; checking millis()
// around it cannot impose a total deadline. Send directly without blocking.
inline bool boundedSocketWrite(WiFiClient& client, const uint8_t* data,
                               std::size_t length, std::size_t blockSize,
                               uint32_t timeoutMs) {
  const int socket = client.fd();
  if (socket < 0) {
    return false;
  }
  return core::boundedWrite(
      data, length, blockSize, timeoutMs, [] { return millis(); },
      [socket](const uint8_t* bytes, std::size_t count) -> std::ptrdiff_t {
        const int result = ::send(socket, bytes, count, MSG_DONTWAIT);
        if (result > 0) {
          return result;
        }
        if (result < 0 &&
            (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
          return 0;
        }
        return -1;
      },
      [] { vTaskDelay(1); });
}

}  // namespace app::adapters
