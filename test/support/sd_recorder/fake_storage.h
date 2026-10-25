#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace fake {
struct Mutex {
  bool locked = false;
  bool failTake = false;
  unsigned takes = 0;
  unsigned gives = 0;
};
inline Mutex mutex;
inline bool failCreate = false;
inline unsigned filesystemCalls = 0;
inline unsigned unlockedCalls = 0;
inline bool shortWrite = false;
inline uint32_t now = 0;
inline uint64_t totalBytes = (uint64_t{1} << 40) + 123;
inline std::map<std::string, std::shared_ptr<std::vector<uint8_t>>> files;

inline void accessFilesystem() {
  ++filesystemCalls;
  if (!mutex.locked) ++unlockedCalls;
}

inline void reset() {
  mutex = {};
  failCreate = false;
  filesystemCalls = 0;
  unlockedCalls = 0;
  shortWrite = false;
  now = 0;
  files.clear();
  totalBytes = (uint64_t{1} << 40) + 123;
}
}  // namespace fake
