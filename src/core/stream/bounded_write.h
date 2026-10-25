#pragma once

#include <cstddef>
#include <cstdint>

namespace app::core {

// Send must be nonblocking: >0 bytes accepted, 0 retryable, <0 fatal.
// Clock returns a uint32_t monotonic millisecond counter; pause must yield
// and allow it to advance. A single total budget covers all partial writes.
template <typename Clock, typename Send, typename Pause>
bool boundedWrite(const uint8_t* data, std::size_t length,
                  std::size_t blockSize, uint32_t timeoutMs, Clock clock,
                  Send send, Pause pause) {
  if (length == 0) {
    return true;
  }
  if (data == nullptr || blockSize == 0 || timeoutMs == 0) {
    return false;
  }
  const uint32_t startedAt = clock();
  while (length > 0) {
    if (static_cast<uint32_t>(clock() - startedAt) >= timeoutMs) {
      return false;
    }
    const std::size_t block = length < blockSize ? length : blockSize;
    const std::ptrdiff_t written = send(data, block);
    if (written < 0 || static_cast<std::size_t>(written) > block) {
      return false;
    }
    // Even a final successful write must finish inside the original budget.
    if (static_cast<uint32_t>(clock() - startedAt) >= timeoutMs) {
      return false;
    }
    if (written == 0) {
      pause();
      continue;
    }
    data += written;
    length -= static_cast<std::size_t>(written);
  }
  return true;
}

}  // namespace app::core
