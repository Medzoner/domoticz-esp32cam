#include "core/recording/recording_rules.h"

#include <cstdio>
#include <cstring>

namespace app::core::recording {

bool shouldStartNewSegment(uint64_t currentBytes, std::size_t nextFrameBytes,
                           uint64_t maximumBytes) {
  return currentBytes > 0 &&
         (currentBytes >= maximumBytes ||
          nextFrameBytes > maximumBytes - currentBytes);
}

std::size_t formatSegmentPath(char* destination, std::size_t capacity,
                              uint32_t index) {
  if (destination == nullptr || capacity == 0 || index == 0) {
    return 0;
  }
  const int length = snprintf(destination, capacity, "/recording-%05u.mjr",
                              static_cast<unsigned int>(index));
  if (length < 0 || static_cast<std::size_t>(length) >= capacity) {
    destination[0] = '\0';
    return 0;
  }
  return static_cast<std::size_t>(length);
}

bool parseSegmentPath(const char* path, uint32_t& index) {
  if (path == nullptr) {
    return false;
  }
  const char* name = std::strrchr(path, '/');
  name = name == nullptr ? path : name + 1;
  constexpr char kPrefix[] = "recording-";
  constexpr char kSuffix[] = ".mjr";
  if (std::strncmp(name, kPrefix, sizeof(kPrefix) - 1) != 0) {
    return false;
  }
  const char* digits = name + sizeof(kPrefix) - 1;
  uint32_t value = 0;
  for (std::size_t position = 0; position < 5; ++position) {
    if (digits[position] < '0' || digits[position] > '9') {
      return false;
    }
    value = value * 10 + static_cast<uint32_t>(digits[position] - '0');
  }
  if (value == 0 || std::strcmp(digits + 5, kSuffix) != 0) {
    return false;
  }
  index = value;
  return true;
}

}  // namespace app::core::recording
