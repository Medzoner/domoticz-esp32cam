#include <unity.h>

#include <cstring>

#include "config/recording_config.h"
#include "core/recording/playback_transfer.h"
#include "core/recording/recording_reader.h"
#include "core/stream/bounded_write.h"

namespace {
using app::core::recording::PlaybackDeadline;
constexpr uint8_t kPreamble[]{1, 2, 3};

void wifiPausesPreservePreambleAndMultiplePayloadBlocks() {
  uint8_t source[app::config::kRecordingReadBlockSize * 2 + 17]{};
  uint8_t received[sizeof(kPreamble) + sizeof(source)]{};
  uint8_t buffer[app::config::kRecordingReadBlockSize]{};
  for (std::size_t i = 0; i < sizeof(source); ++i) source[i] = i % 251;
  std::size_t readOffset = 0, writeOffset = 0;
  uint32_t now = UINT32_MAX - 300;
  unsigned writes = 0;
  TEST_ASSERT_TRUE(app::core::recording::playbackFrameTransfer(
      kPreamble, sizeof(kPreamble), sizeof(source), buffer, sizeof(buffer),
      {now, app::config::kPlaybackFrameWriteTimeoutMs}, [&] { return now; },
      [&](uint8_t* data, std::size_t count) {
        std::memcpy(data, source + readOffset, count);
        readOffset += count;
        return true;
      },
      [&](const uint8_t* data, std::size_t size, PlaybackDeadline deadline) {
        ++writes;
        const uint32_t start = now;
        return app::core::boundedWriteFrom(
            data, size, 512, deadline.budgetMs, app::config::kPlaybackWriteIdleTimeoutMs,
            deadline.startedAt,
            [&] { return now; },
            [&](const uint8_t* bytes, std::size_t count) -> std::ptrdiff_t {
              if (static_cast<uint32_t>(now - start) < 600) return 0;
              std::memcpy(received + writeOffset, bytes, count);
              writeOffset += count;
              return static_cast<std::ptrdiff_t>(count);
            }, [&] { ++now; });
      }));
  TEST_ASSERT_EQUAL_UINT(4, writes);
  TEST_ASSERT_EQUAL_UINT(sizeof(received), writeOffset);
  TEST_ASSERT_EQUAL_MEMORY(kPreamble, received, sizeof(kPreamble));
  TEST_ASSERT_EQUAL_MEMORY(source, received + sizeof(kPreamble), sizeof(source));
}

void headerAndBlocksShareOneHardBudget() {
  uint8_t buffer[4]{};
  uint32_t now = 0;
  unsigned writes = 0;
  TEST_ASSERT_FALSE(app::core::recording::playbackFrameTransfer(
      kPreamble, sizeof(kPreamble), 12, buffer, sizeof(buffer), {0, 5000},
      [&] { return now; }, [](uint8_t*, std::size_t) { return true; },
      [&](const uint8_t*, std::size_t, PlaybackDeadline deadline) {
        TEST_ASSERT_EQUAL_UINT32(0, deadline.startedAt);
        const uint32_t budget = deadline.budgetMs - now;
        ++writes;
        if (budget <= 1500) { now += budget; return false; }
        now += 1500;
        return true;
      }));
  TEST_ASSERT_EQUAL_UINT(4, writes);
  TEST_ASSERT_EQUAL_UINT32(5000, now);
}

void stalledPreambleDoesNotReadPayloadAndCleansUp() {
  uint8_t buffer[4]{};
  uint32_t now = 0;
  unsigned reads = 0, closes = 0;
  bool stopped = false;
  app::core::recording::recordingReaderSession(
      [&] {
        TEST_ASSERT_FALSE(app::core::recording::playbackFrameTransfer(
            kPreamble, sizeof(kPreamble), 4, buffer, sizeof(buffer),
            {now, app::config::kPlaybackFrameWriteTimeoutMs}, [&] { return now; },
            [&](uint8_t*, std::size_t) { ++reads; return true; },
            [&](const uint8_t* data, std::size_t size, PlaybackDeadline deadline) {
              return app::core::boundedWriteFrom(
                  data, size, 512, deadline.budgetMs, app::config::kPlaybackWriteIdleTimeoutMs,
                  deadline.startedAt,
                  [&] { return now; },
                  [](const uint8_t*, std::size_t) { return std::ptrdiff_t{0}; },
                  [&] { ++now; });
            }));
      }, [&] { stopped = true; },
      [&] { TEST_ASSERT_TRUE(stopped); return ++closes == 2; }, [] {});
  TEST_ASSERT_EQUAL_UINT(0, reads);
  TEST_ASSERT_EQUAL_UINT(2, closes);
  TEST_ASSERT_EQUAL_UINT32(app::config::kPlaybackWriteIdleTimeoutMs, now);
}

void readFailureNeverSendsPayload() {
  uint8_t buffer[4]{};
  unsigned writes = 0;
  TEST_ASSERT_FALSE(app::core::recording::playbackFrameTransfer(
      kPreamble, sizeof(kPreamble), 4, buffer, sizeof(buffer), {0, 5000},
      [] { return uint32_t{0}; }, [](uint8_t*, std::size_t) { return false; },
      [&](const uint8_t*, std::size_t, PlaybackDeadline) { ++writes; return true; }));
  TEST_ASSERT_EQUAL_UINT(1, writes);
}

void expiredAbsoluteDeadlineNeverSendsAndCleansUp() {
  uint8_t buffer[4]{};
  uint32_t now = 0;
  app::core::recording::PlaybackBudget budget(100, 5000, 600000);
  const auto deadline = budget.next(now);
  bool closed = false;
  unsigned sends = 0;
  app::core::recording::recordingReaderSession([&] {
    TEST_ASSERT_FALSE(app::core::recording::playbackFrameTransfer(
        kPreamble, sizeof(kPreamble), 4, buffer, sizeof(buffer), deadline,
        [&] { return now; }, [](uint8_t*, std::size_t) { return true; },
        [&](const uint8_t* data, std::size_t size, PlaybackDeadline writeDeadline) {
          // Preemption after caller's check, before entry to socket writer.
          now += 5000;
          return app::core::boundedWriteFrom(
              data, size, 512, writeDeadline.budgetMs, 2000, writeDeadline.startedAt,
              [&] { return now; },
              [&](const uint8_t*, std::size_t) { ++sends; return std::ptrdiff_t{1}; },
              [] {});
        }));
  }, [] {}, [&] { closed = true; return true; }, [] {});
  TEST_ASSERT_TRUE(closed);
  TEST_ASSERT_EQUAL_UINT(0, sends);
}

void healthyLongRecordingDoesNotConsumeSlowdownAllowance() {
  app::core::recording::PlaybackBudget budget(100, 5000, 600000);
  uint32_t now = 0;
  for (unsigned i = 0; i < 8188; ++i) {
    const auto deadline = budget.next(now);
    now += 50;
    TEST_ASSERT_TRUE(budget.complete(deadline, now));
    now += 50;
  }
  TEST_ASSERT_EQUAL_UINT32(818800, now);
  TEST_ASSERT_EQUAL_UINT32(5000, budget.next(now).budgetMs);
}

void slowdownAllowanceShrinksAcrossFrames() {
  app::core::recording::PlaybackBudget budget(100, 5000, 1000);
  uint32_t now = 0;
  auto deadline = budget.next(now);
  TEST_ASSERT_EQUAL_UINT32(1100, deadline.budgetMs);
  now += 600;
  TEST_ASSERT_TRUE(budget.complete(deadline, now));
  deadline = budget.next(now);
  TEST_ASSERT_EQUAL_UINT32(600, deadline.budgetMs);
  now += 600;
  TEST_ASSERT_FALSE(budget.complete(deadline, now));
}

void lateShortHeaderReadDoesNotRetry() {
  uint32_t now = 0;
  unsigned reads = 0;
  uint64_t remaining = 12;
  const auto frame = app::core::recording::nextFrame(
      remaining, 1024, [&](uint8_t* data, std::size_t size) {
        return app::core::recording::playbackRead(
            data, size, {0, 5000}, [&] { return now; },
            [&](uint8_t* bytes, std::size_t) -> std::size_t {
              ++reads;
              bytes[0] = 8;
              now = 5000;
              return 1;
            });
      });
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(app::core::recording::FrameReadStatus::kReadFailed),
      static_cast<int>(frame.status));
  TEST_ASSERT_EQUAL_UINT(1, reads);
}

void lateShortPayloadReadDoesNotRetryOrWrite() {
  uint8_t buffer[4]{};
  uint32_t now = 0;
  unsigned reads = 0, writes = 0;
  TEST_ASSERT_FALSE(app::core::recording::playbackFrameTransfer(
      kPreamble, sizeof(kPreamble), 4, buffer, sizeof(buffer), {0, 5000},
      [&] { return now; },
      [&](uint8_t* data, std::size_t size) {
        return app::core::recording::readExact(data, size,
            [&](uint8_t* bytes, std::size_t count) {
              return app::core::recording::playbackRead(
                  bytes, count, {0, 5000}, [&] { return now; },
                  [&](uint8_t*, std::size_t) -> std::size_t {
                    ++reads;
                    now = 5000;
                    return 1;
                  });
            });
      },
      [&](const uint8_t*, std::size_t, PlaybackDeadline) { ++writes; return true; }));
  TEST_ASSERT_EQUAL_UINT(1, reads);
  TEST_ASSERT_EQUAL_UINT(1, writes);  // Only the preamble.
}

void sdTimeDoesNotConsumePerWriteIdleAllowance() {
  uint8_t buffer[4]{};
  uint32_t now = 0;
  TEST_ASSERT_TRUE(app::core::recording::playbackFrameTransfer(
      kPreamble, sizeof(kPreamble), 4, buffer, sizeof(buffer), {0, 5000},
      [&] { return now; },
      [&](uint8_t*, std::size_t) { now += 1500; return true; },
      [&](const uint8_t* data, std::size_t size, PlaybackDeadline deadline) {
        const uint32_t start = now;
        return app::core::boundedWriteFrom(
            data, size, 512, deadline.budgetMs, 2000, deadline.startedAt,
            [&] { return now; },
            [&](const uint8_t*, std::size_t count) -> std::ptrdiff_t {
              if (now - start < 1500) return 0;
              return static_cast<std::ptrdiff_t>(count);
            }, [&] { ++now; });
      }));
  TEST_ASSERT_EQUAL_UINT32(4500, now);
}
}  // namespace

void setUp() {}
void tearDown() {}
int main() {
  UNITY_BEGIN();
  RUN_TEST(wifiPausesPreservePreambleAndMultiplePayloadBlocks);
  RUN_TEST(headerAndBlocksShareOneHardBudget);
  RUN_TEST(stalledPreambleDoesNotReadPayloadAndCleansUp);
  RUN_TEST(readFailureNeverSendsPayload);
  RUN_TEST(expiredAbsoluteDeadlineNeverSendsAndCleansUp);
  RUN_TEST(healthyLongRecordingDoesNotConsumeSlowdownAllowance);
  RUN_TEST(slowdownAllowanceShrinksAcrossFrames);
  RUN_TEST(lateShortHeaderReadDoesNotRetry);
  RUN_TEST(lateShortPayloadReadDoesNotRetryOrWrite);
  RUN_TEST(sdTimeDoesNotConsumePerWriteIdleAllowance);
  return UNITY_END();
}
