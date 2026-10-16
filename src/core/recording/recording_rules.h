#pragma once

#include <cstddef>
#include <cstdint>

namespace app::core::recording {

inline constexpr uint8_t kFileMagic[] = {'M', 'J', 'R', '1', '\r', '\n', 0x1a,
                                         '\n'};

bool shouldStartNewSegment(uint64_t currentBytes, std::size_t nextFrameBytes,
                           uint64_t maximumBytes);
std::size_t formatSegmentPath(char* destination, std::size_t capacity,
                              uint32_t index);
bool parseSegmentPath(const char* path, uint32_t& index);

}  // namespace app::core::recording
