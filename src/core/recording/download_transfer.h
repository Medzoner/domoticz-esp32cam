#pragma once

#include <cstddef>
#include <cstdint>

namespace app::core::recording {

// Read must fill a block or fail. Write must be nonblocking/bounded by the
// supplied remaining transfer budget, including any internal retries.
template <typename Clock, typename Read, typename Write>
bool downloadTransfer(uint64_t remaining, uint8_t* buffer, std::size_t capacity,
                      uint32_t timeoutMs, Clock clock, Read read, Write write) {
  if (buffer == nullptr || capacity == 0 || timeoutMs == 0) return false;
  const uint32_t startedAt = clock();
  while (remaining > 0) {
    if (static_cast<uint32_t>(clock() - startedAt) >= timeoutMs) return false;
    const std::size_t block = remaining < capacity
                                  ? static_cast<std::size_t>(remaining) : capacity;
    if (!read(buffer, block)) return false;
    const uint32_t elapsed = static_cast<uint32_t>(clock() - startedAt);
    if (elapsed >= timeoutMs ||
        !write(buffer, block, timeoutMs - elapsed)) return false;
    if (static_cast<uint32_t>(clock() - startedAt) >= timeoutMs) return false;
    remaining -= block;
  }
  return true;
}

// No next request may start until a transient storage-lock failure clears.
// The caller closes the socket before attempting reader cleanup.
template <typename Handle, typename Stop, typename Close, typename Pause>
void recordingReaderSession(Handle handle, Stop stop, Close close, Pause pause) {
  handle();
  stop();
  while (!close()) pause();
}

}  // namespace app::core::recording
