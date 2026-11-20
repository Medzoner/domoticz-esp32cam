#include <unity.h>

#include <cstdint>
#include <cstring>

#include "core/stream/bounded_write.h"
#include "config/recording_config.h"
#include "core/recording/download_transfer.h"

namespace {
constexpr uint8_t kData[]{1, 2, 3, 4, 5, 6};

struct Transport {
  uint32_t now = 0;
  uint32_t cost = 0;
  std::ptrdiff_t result = 2;
  unsigned calls = 0;
  unsigned pauses = 0;
  uint8_t received[sizeof(kData)]{};
  std::size_t receivedSize = 0;

  bool write(uint32_t timeout = 10, std::size_t block = 3) {
    return app::core::boundedWrite(
        kData, sizeof(kData), block, timeout, [this] { return now; },
        [this](const uint8_t* data, std::size_t size) -> std::ptrdiff_t {
          ++calls;
          now += cost;
          if (result <= 0) {
            return result;
          }
          const auto count = static_cast<std::size_t>(result) < size
                                 ? static_cast<std::size_t>(result)
                                 : size;
          std::memcpy(received + receivedSize, data, count);
          receivedSize += count;
          return static_cast<std::ptrdiff_t>(count);
        },
        [this] {
          ++pauses;
          ++now;
        });
  }
};

void partialWritesPreserveBytes() {
  Transport transport;
  TEST_ASSERT_TRUE(transport.write());
  TEST_ASSERT_EQUAL_UINT(3, transport.calls);
  TEST_ASSERT_EQUAL_MEMORY(kData, transport.received, sizeof(kData));
}

void blockedSocketYieldsUntilDeadline() {
  Transport transport;
  transport.result = 0;
  TEST_ASSERT_FALSE(transport.write());
  TEST_ASSERT_EQUAL_UINT(10, transport.calls);
  TEST_ASSERT_EQUAL_UINT(10, transport.pauses);
}

void disconnectFailsImmediately() {
  Transport transport;
  transport.result = -1;
  TEST_ASSERT_FALSE(transport.write());
  TEST_ASSERT_EQUAL_UINT(1, transport.calls);
  TEST_ASSERT_EQUAL_UINT(0, transport.pauses);
}

void progressDoesNotResetDeadline() {
  Transport transport;
  transport.cost = 4;
  TEST_ASSERT_FALSE(transport.write(10, 1));
  TEST_ASSERT_EQUAL_UINT(3, transport.calls);
}

void lastWriteMustMeetDeadline() {
  Transport transport;
  transport.result = sizeof(kData);
  transport.cost = 10;
  TEST_ASSERT_FALSE(transport.write(10, sizeof(kData)));
  TEST_ASSERT_EQUAL_UINT(1, transport.calls);
}

void deadlinesSurviveClockWrap() {
  Transport transport;
  transport.now = UINT32_MAX - 3;
  transport.result = 0;
  TEST_ASSERT_FALSE(transport.write());
  TEST_ASSERT_EQUAL_UINT(10, transport.calls);
  TEST_ASSERT_EQUAL_UINT32(6, transport.now);
  transport.result = 2;
  transport.cost = 1;
  transport.now = UINT32_MAX - 1;
  TEST_ASSERT_TRUE(transport.write());
}

void invalidInputsNeverSend() {
  unsigned calls = 0;
  const auto clock = [] { return uint32_t{0}; };
  const auto send = [&calls](const uint8_t*, std::size_t) {
    ++calls;
    return std::ptrdiff_t{0};
  };
  const auto pause = [] {};
  TEST_ASSERT_FALSE(app::core::boundedWrite(nullptr, 1, 1, 1, clock, send, pause));
  TEST_ASSERT_FALSE(app::core::boundedWrite(kData, 1, 0, 1, clock, send, pause));
  TEST_ASSERT_FALSE(app::core::boundedWrite(kData, 1, 1, 0, clock, send, pause));
  TEST_ASSERT_TRUE(app::core::boundedWrite(nullptr, 0, 0, 0, clock, send, pause));
  TEST_ASSERT_EQUAL_UINT(0, calls);
}

void invalidWriteCountIsRejected() {
  TEST_ASSERT_FALSE(app::core::boundedWrite(
      kData, sizeof(kData), 1, 10, [] { return uint32_t{0}; },
      [](const uint8_t*, std::size_t) { return std::ptrdiff_t{2}; }, [] {}));
}

void retriesCanRecoverWithoutDuplicatingBytes() {
  uint32_t now = 0;
  unsigned calls = 0;
  uint8_t received[sizeof(kData)]{};
  std::size_t offset = 0;
  TEST_ASSERT_TRUE(app::core::boundedWrite(
      kData, sizeof(kData), 2, 10, [&now] { return now; },
      [&](const uint8_t* data, std::size_t size) -> std::ptrdiff_t {
        ++calls;
        TEST_ASSERT_LESS_OR_EQUAL_UINT(2, size);
        if (calls % 2 == 1) {
          return 0;
        }
        std::memcpy(received + offset, data, size);
        offset += size;
        return static_cast<std::ptrdiff_t>(size);
      },
      [&now] { ++now; }));
  TEST_ASSERT_EQUAL_UINT(6, calls);
  TEST_ASSERT_EQUAL_MEMORY(kData, received, sizeof(kData));
}

void progressingPayloadCanExceedIdleBudgetWithoutTruncation() {
  uint32_t now = 0;
  unsigned calls = 0;
  TEST_ASSERT_TRUE(app::core::boundedWriteWithIdleTimeout(
      kData, sizeof(kData), 1, 30, 5, [&] { return now; },
      [&](const uint8_t*, std::size_t) { ++calls; now += 4; return std::ptrdiff_t{1}; },
      [] {}));
  TEST_ASSERT_EQUAL_UINT(6, calls);
  TEST_ASSERT_EQUAL_UINT32(24, now);
}

void stalledPayloadStopsAtIdleDeadlineBeforeTotalBudget() {
  uint32_t now = 0;
  TEST_ASSERT_FALSE(app::core::boundedWriteWithIdleTimeout(
      kData, sizeof(kData), 1, 30, 5, [&] { return now; },
      [](const uint8_t*, std::size_t) { return std::ptrdiff_t{0}; }, [&] { ++now; }));
  TEST_ASSERT_EQUAL_UINT32(5, now);
}

void progressNeverResetsTheHardPayloadDeadline() {
  uint32_t now = 0;
  unsigned calls = 0;
  TEST_ASSERT_FALSE(app::core::boundedWriteWithIdleTimeout(
      kData, sizeof(kData), 1, 10, 5, [&] { return now; },
      [&](const uint8_t*, std::size_t) { ++calls; now += 4; return std::ptrdiff_t{1}; },
      [] {}));
  TEST_ASSERT_EQUAL_UINT(3, calls);
}

void idleClockSurvivesRolloverAfterSomeProgress() {
  uint32_t now = UINT32_MAX - 2;
  unsigned calls = 0;
  TEST_ASSERT_FALSE(app::core::boundedWriteWithIdleTimeout(
      kData, sizeof(kData), 1, 30, 5, [&] { return now; },
      [&](const uint8_t*, std::size_t) { return ++calls == 1 ? std::ptrdiff_t{1} : 0; },
      [&] { ++now; }));
  TEST_ASSERT_EQUAL_UINT32(2, now);
  TEST_ASSERT_FALSE(app::core::boundedWriteWithIdleTimeout(
      kData, sizeof(kData), 1, 30, 0, [] { return uint32_t{0}; },
      [](const uint8_t*, std::size_t) { return std::ptrdiff_t{1}; }, [] {}));
}

void intermittentProgressRefreshesOnlyTheIdleClock() {
  uint32_t now = 0;
  unsigned calls = 0;
  TEST_ASSERT_TRUE(app::core::boundedWriteWithIdleTimeout(
      kData, sizeof(kData), 1, 30, 5, [&] { return now; },
      [&](const uint8_t*, std::size_t) {
        return ++calls % 4 == 0 ? std::ptrdiff_t{1} : std::ptrdiff_t{0};
      },
      [&] { ++now; }));
  TEST_ASSERT_EQUAL_UINT32(18, now);
  TEST_ASSERT_EQUAL_UINT(24, calls);
}
void downloadRecoversAfterWifiPauseWithoutLosingBytes() {
  uint32_t now = 0;
  uint8_t received[sizeof(kData)]{};
  std::size_t offset = 0;
  TEST_ASSERT_TRUE(app::core::boundedWriteWithIdleTimeout(
      kData, sizeof(kData), app::config::kRecordingReadBlockSize,
      app::config::kDownloadWriteTimeoutMs,
      app::config::kDownloadWriteIdleTimeoutMs, [&] { return now; },
      [&](const uint8_t* data, std::size_t) -> std::ptrdiff_t {
        // Backpressure exceeding the former 300 ms absolute deadline.
        if (now < 600) return 0;
        received[offset++] = *data;
        return 1;
      }, [&] { ++now; }));
  TEST_ASSERT_EQUAL_MEMORY(kData, received, sizeof(kData));
  TEST_ASSERT_EQUAL_UINT32(600, now);
}

void downloadStalledClientIsBounded() {
  uint32_t now = 0;
  TEST_ASSERT_FALSE(app::core::boundedWriteWithIdleTimeout(
      kData, sizeof(kData), app::config::kRecordingReadBlockSize,
      app::config::kDownloadWriteTimeoutMs,
      app::config::kDownloadWriteIdleTimeoutMs, [&] { return now; },
      [](const uint8_t*, std::size_t) { return std::ptrdiff_t{0}; },
      [&] { ++now; }));
  TEST_ASSERT_EQUAL_UINT32(app::config::kDownloadWriteIdleTimeoutMs, now);
}

void downloadSlowProgressStillMeetsHardDeadline() {
  uint32_t now = 0;
  uint32_t nextWrite = 1500;
  TEST_ASSERT_FALSE(app::core::boundedWriteWithIdleTimeout(
      kData, sizeof(kData), app::config::kRecordingReadBlockSize,
      app::config::kDownloadWriteTimeoutMs,
      app::config::kDownloadWriteIdleTimeoutMs, [&] { return now; },
      [&](const uint8_t*, std::size_t) -> std::ptrdiff_t {
        if (now < nextWrite) return 0;
        nextWrite += 1500;
        return 1;
      }, [&] { ++now; }));
  TEST_ASSERT_EQUAL_UINT32(app::config::kDownloadWriteTimeoutMs, now);
}
void downloadTransfersMultipleBlocksAndPartialTail() {
  uint8_t source[app::config::kRecordingReadBlockSize * 2 + 17]{};
  uint8_t received[sizeof(source)]{};
  uint8_t buffer[app::config::kRecordingReadBlockSize]{};
  for (std::size_t i = 0; i < sizeof(source); ++i) source[i] = i % 251;
  std::size_t readOffset = 0, writeOffset = 0;
  unsigned blocks = 0;
  uint32_t now = 0;
  TEST_ASSERT_TRUE(app::core::recording::downloadTransfer(
      sizeof(source), buffer, sizeof(buffer), app::config::kDownloadTransferTimeoutMs,
      [&] { return now; },
      [&](uint8_t* data, std::size_t size) {
        std::memcpy(data, source + readOffset, size);
        readOffset += size;
        return true;
      },
      [&](const uint8_t* data, std::size_t size, uint32_t remainingMs) {
        ++blocks;
        const uint32_t readyAt = now + 600;
        const uint32_t budget = remainingMs < app::config::kDownloadWriteTimeoutMs
                                    ? remainingMs : app::config::kDownloadWriteTimeoutMs;
        return app::core::boundedWriteWithIdleTimeout(
            data, size, 512, budget, app::config::kDownloadWriteIdleTimeoutMs,
            [&] { return now; },
            [&](const uint8_t* bytes, std::size_t count) -> std::ptrdiff_t {
              if (now < readyAt) return 0;
              std::memcpy(received + writeOffset, bytes, count);
              writeOffset += count;
              return static_cast<std::ptrdiff_t>(count);
            }, [&] { ++now; });
      }));
  TEST_ASSERT_EQUAL_UINT(3, blocks);
  TEST_ASSERT_EQUAL_UINT(sizeof(source), writeOffset);
  TEST_ASSERT_EQUAL_MEMORY(source, received, sizeof(source));
}

void downloadTotalDeadlineClampsFinalBlockAndReleasesReader() {
  uint8_t buffer[4]{};
  uint32_t now = UINT32_MAX - 100;
  const uint32_t started = now;
  unsigned writes = 0, closes = 0, pauses = 0;
  bool stopped = false, completed = true;
  app::core::recording::recordingReaderSession(
      [&] {
        completed = app::core::recording::downloadTransfer(
            12, buffer, sizeof(buffer), 1000, [&] { return now; },
            [](uint8_t*, std::size_t) { return true; },
            [&](const uint8_t*, std::size_t, uint32_t budget) {
              ++writes;
              if (writes == 1) {
                TEST_ASSERT_EQUAL_UINT32(1000, budget);
                now += 600;
                return true;
              }
              TEST_ASSERT_EQUAL_UINT32(400, budget);
              now += budget;
              return false;
            });
      },
      [&] { stopped = true; },
      [&] { TEST_ASSERT_TRUE(stopped); return ++closes == 3; },
      [&] { ++pauses; });
  TEST_ASSERT_FALSE(completed);
  TEST_ASSERT_EQUAL_UINT32(1000, now - started);
  TEST_ASSERT_EQUAL_UINT(2, writes);
  TEST_ASSERT_EQUAL_UINT(3, closes);
  TEST_ASSERT_EQUAL_UINT(2, pauses);
}

void downloadReadFailureSkipsWriteAndReleasesReader() {
  uint8_t buffer[4]{};
  bool closed = false;
  app::core::recording::recordingReaderSession(
      [&] {
        TEST_ASSERT_FALSE(app::core::recording::downloadTransfer(
            4, buffer, sizeof(buffer), 1000, [] { return uint32_t{0}; },
            [](uint8_t*, std::size_t) { return false; },
            [](const uint8_t*, std::size_t, uint32_t) {
              TEST_FAIL_MESSAGE("Must not send after an SD read failure");
              return true;
            }));
      }, [] {}, [&] { closed = true; return true; }, [] {});
  TEST_ASSERT_TRUE(closed);
}
}  // namespace

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(partialWritesPreserveBytes);
  RUN_TEST(blockedSocketYieldsUntilDeadline);
  RUN_TEST(disconnectFailsImmediately);
  RUN_TEST(progressDoesNotResetDeadline);
  RUN_TEST(lastWriteMustMeetDeadline);
  RUN_TEST(deadlinesSurviveClockWrap);
  RUN_TEST(invalidInputsNeverSend);
  RUN_TEST(invalidWriteCountIsRejected);
  RUN_TEST(retriesCanRecoverWithoutDuplicatingBytes);
  RUN_TEST(progressingPayloadCanExceedIdleBudgetWithoutTruncation);
  RUN_TEST(stalledPayloadStopsAtIdleDeadlineBeforeTotalBudget);
  RUN_TEST(progressNeverResetsTheHardPayloadDeadline);
  RUN_TEST(idleClockSurvivesRolloverAfterSomeProgress);
  RUN_TEST(intermittentProgressRefreshesOnlyTheIdleClock);
  RUN_TEST(downloadRecoversAfterWifiPauseWithoutLosingBytes);
  RUN_TEST(downloadStalledClientIsBounded);
  RUN_TEST(downloadSlowProgressStillMeetsHardDeadline);
  RUN_TEST(downloadTransfersMultipleBlocksAndPartialTail);
  RUN_TEST(downloadTotalDeadlineClampsFinalBlockAndReleasesReader);
  RUN_TEST(downloadReadFailureSkipsWriteAndReleasesReader);
  return UNITY_END();
}
