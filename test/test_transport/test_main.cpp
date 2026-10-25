#include <unity.h>

#include <cstdint>
#include <cstring>

#include "core/stream/bounded_write.h"

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
  return UNITY_END();
}
