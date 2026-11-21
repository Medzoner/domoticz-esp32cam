#pragma once

#include "core/recording/download_transfer.h"

namespace app::core::recording {

struct PlaybackDeadline {
  uint32_t startedAt;
  uint32_t budgetMs;
  bool expired(uint32_t now) const {
    return static_cast<uint32_t>(now - startedAt) >= budgetMs;
  }
};

// An SD operation cannot be preempted, but a late short read must not trigger
// another operation. Reject late completion as well as late entry.
template <typename Clock, typename Read>
std::size_t playbackRead(uint8_t* data, std::size_t size,
                         PlaybackDeadline deadline, Clock clock, Read read) {
  if (deadline.expired(clock())) return 0;
  const std::size_t count = read(data, size);
  return deadline.expired(clock()) ? 0 : count;
}

// Charge only time beyond the normal frame interval, not legitimate recording
// duration. A finite file plus this allowance bounds slow-client occupancy.
class PlaybackBudget {
 public:
  PlaybackBudget(uint32_t interval, uint32_t frameLimit, uint32_t slowdown)
      : interval_(interval), frameLimit_(frameLimit), remaining_(slowdown) {}
  PlaybackDeadline next(uint32_t now) const {
    const uint64_t allowance = static_cast<uint64_t>(remaining_) + interval_;
    return {now, remaining_ == 0 ? 0 :
        static_cast<uint32_t>(allowance < frameLimit_ ? allowance : frameLimit_)};
  }
  bool complete(PlaybackDeadline deadline, uint32_t now) {
    if (deadline.expired(now)) return false;
    const uint32_t elapsed = now - deadline.startedAt;
    const uint32_t excess = elapsed > interval_ ? elapsed - interval_ : 0;
    if (excess >= remaining_) { remaining_ = 0; return false; }
    remaining_ -= excess;
    return true;
  }
 private:
  uint32_t interval_;
  uint32_t frameLimit_;
  uint32_t remaining_;
};

// The same absolute deadline is passed through every write, including the
// preamble. Write must enforce it inside its nonblocking send loop.
template <typename Clock, typename Read, typename Write>
bool playbackFrameTransfer(const uint8_t* preamble, std::size_t preambleSize,
                           uint32_t frameSize, uint8_t* buffer,
                           std::size_t capacity, PlaybackDeadline deadline,
                           Clock clock, Read read, Write write) {
  if (preamble == nullptr || preambleSize == 0 || frameSize == 0 ||
      buffer == nullptr || capacity == 0 || deadline.expired(clock())) return false;
  if (!write(preamble, preambleSize, deadline)) return false;
  std::size_t remaining = frameSize;
  while (remaining > 0) {
    if (deadline.expired(clock())) return false;
    const std::size_t block = remaining < capacity ? remaining : capacity;
    if (!read(buffer, block) || deadline.expired(clock()) ||
        !write(buffer, block, deadline) || deadline.expired(clock())) return false;
    remaining -= block;
  }
  return true;
}

}  // namespace app::core::recording
