#pragma once

#include <cstddef>
#include <cstdint>

namespace app::core::recording {

// A short SD write is a failed record, not a committed frame. Do not retry the
// whole record after partial progress: the caller must stop/close the segment.
template <typename Write>
bool writeFrame(const uint8_t* data, std::size_t size, uint32_t maximumSize,
                Write write) {
  if (data == nullptr || size == 0 || size > maximumSize) {
    return false;
  }
  const uint32_t length = static_cast<uint32_t>(size);
  const uint8_t header[]{static_cast<uint8_t>(length),
                         static_cast<uint8_t>(length >> 8),
                         static_cast<uint8_t>(length >> 16),
                         static_cast<uint8_t>(length >> 24)};
  return write(header, sizeof(header)) == sizeof(header) &&
         write(data, size) == size;
}

}  // namespace app::core::recording
