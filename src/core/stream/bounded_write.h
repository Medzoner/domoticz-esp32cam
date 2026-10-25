#pragma once

#include <cstddef>
#include <cstdint>

namespace app::core {

// Send must be nonblocking: >0 bytes accepted, 0 retryable, <0 fatal.
// Clock returns a uint32_t monotonic millisecond counter; pause must yield
// and allow it to advance. A single total budget covers all partial writes.
template <typename Clock, typename Send, typename Pause>
bool boundedWriteWithIdleTimeout(const uint8_t* data, std::size_t length,
                                 std::size_t blockSize, uint32_t timeoutMs,
                                 uint32_t idleTimeoutMs, Clock clock,
                                 Send send, Pause pause) {
  if (length == 0) {
    return true;
  }
  if (data == nullptr || blockSize == 0 || timeoutMs == 0 || idleTimeoutMs == 0) {
    return false;
  }
  const uint32_t startedAt = clock();
  uint32_t lastProgressAt = startedAt;
  while (length > 0) {
    const uint32_t now = clock();
    if (static_cast<uint32_t>(now - startedAt) >= timeoutMs ||
        static_cast<uint32_t>(now - lastProgressAt) >= idleTimeoutMs) {
      return false;
    }
    const std::size_t block = length < blockSize ? length : blockSize;
    const std::ptrdiff_t written = send(data, block);
    if (written < 0 || static_cast<std::size_t>(written) > block) {
      return false;
    }
    // Even a final successful write must finish inside the original budget.
    const uint32_t finishedAt = clock();
    if (static_cast<uint32_t>(finishedAt - startedAt) >= timeoutMs ||
        static_cast<uint32_t>(finishedAt - lastProgressAt) >= idleTimeoutMs) {
      return false;
    }
    if (written == 0) {
      pause();
      continue;
    }
    lastProgressAt = finishedAt;
    data += written;
    length -= static_cast<std::size_t>(written);
  }
  return true;
}

template <typename Clock, typename Send, typename Pause>
bool boundedWrite(const uint8_t* data, std::size_t length,
                  std::size_t blockSize, uint32_t timeoutMs, Clock clock,
                  Send send, Pause pause) {
  return boundedWriteWithIdleTimeout(data, length, blockSize, timeoutMs,
                                     timeoutMs, clock, send, pause);
}

}  // namespace app::core
