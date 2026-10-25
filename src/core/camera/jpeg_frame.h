#pragma once

#include <cstddef>
#include <cstdint>

namespace app::core {

// Cheap guard for the driver's JPEG framing, not a JPEG decoder or checksum.
// Never publish a known missing SOI/EOI as a complete camera image.
inline bool hasJpegBoundaries(const uint8_t* data, std::size_t size) {
  return data != nullptr && size >= 4 && data[0] == 0xff && data[1] == 0xd8 &&
         data[size - 2] == 0xff && data[size - 1] == 0xd9;
}

}  // namespace app::core
