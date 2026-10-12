#include "core/mjpeg_protocol.h"

#include <cstdio>
#include <cstring>

namespace app::core::mjpeg {

bool isStreamRequestLine(const char* requestLine) {
  if (requestLine == nullptr) {
    return false;
  }
  constexpr char kMethodAndRoute[] = "GET /stream";
  if (std::strncmp(requestLine, kMethodAndRoute,
                   sizeof(kMethodAndRoute) - 1) != 0) {
    return false;
  }
  const char separator = requestLine[sizeof(kMethodAndRoute) - 1];
  return separator == ' ' || separator == '?';
}

std::size_t formatPartHeader(char* destination, std::size_t capacity,
                             std::size_t jpegSize) {
  if (destination == nullptr || capacity == 0) {
    return 0;
  }
  const int length = std::snprintf(destination, capacity,
                                   "Content-Type: image/jpeg\r\n"
                                   "Content-Length: %zu\r\n\r\n",
                                   jpegSize);
  if (length < 0 || static_cast<std::size_t>(length) >= capacity) {
    destination[0] = '\0';
    return 0;
  }
  return static_cast<std::size_t>(length);
}

}  // namespace app::core::mjpeg
