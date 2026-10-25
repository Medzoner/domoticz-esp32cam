#include <unity.h>

#include <cstring>

#include "core/recording/recording_reader.h"
#include "core/recording/recording_writer.h"

namespace {
using app::core::recording::FrameReadStatus;

struct Source {
  const uint8_t* data;
  std::size_t size;
  std::size_t offset = 0;
  std::size_t block = 1;

  std::size_t read(uint8_t* destination, std::size_t capacity) {
    std::size_t count = size - offset;
    if (count > capacity) count = capacity;
    if (count > block) count = block;
    if (count != 0) std::memcpy(destination, data + offset, count);
    offset += count;
    return count;
  }

  auto next(uint64_t& remaining, uint32_t maximum = 100) {
    return app::core::recording::nextFrame(remaining, maximum,
        [this](uint8_t* bytes, std::size_t count) { return read(bytes, count); });
  }
};

void acceptsShortReadsAndExactBoundary() {
  const uint8_t bytes[]{3, 0, 0, 0, 1, 2, 3};
  Source source{bytes, sizeof(bytes)};
  uint64_t remaining = sizeof(bytes);
  const auto frame = source.next(remaining, 3);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(FrameReadStatus::kFrame),
                        static_cast<int>(frame.status));
  TEST_ASSERT_EQUAL_UINT(3, frame.size);
  TEST_ASSERT_EQUAL_UINT64(0, remaining);
  uint8_t payload[3]{};
  TEST_ASSERT_TRUE(app::core::recording::readExact(payload, sizeof(payload),
      [&](uint8_t* data, std::size_t size) { return source.read(data, size); }));
  TEST_ASSERT_EQUAL_MEMORY(bytes + 4, payload, sizeof(payload));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(FrameReadStatus::kEnd),
                        static_cast<int>(source.next(remaining).status));
}

void cleanEofDoesNotRead() {
  Source source{nullptr, 0};
  uint64_t remaining = 0;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(FrameReadStatus::kEnd),
                        static_cast<int>(source.next(remaining).status));
  TEST_ASSERT_EQUAL_UINT(0, source.offset);
}

void partialHeaderIsNotAFrame() {
  const uint8_t bytes[]{1, 0, 0};
  Source source{bytes, sizeof(bytes)};
  uint64_t remaining = sizeof(bytes);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(FrameReadStatus::kTruncated),
                        static_cast<int>(source.next(remaining).status));
  TEST_ASSERT_EQUAL_UINT(0, source.offset);
}

void partialPayloadIsRejectedBeforeConsumption() {
  const uint8_t bytes[]{4, 0, 0, 0, 1, 2, 3};
  Source source{bytes, sizeof(bytes)};
  uint64_t remaining = sizeof(bytes);
  const auto frame = source.next(remaining);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(FrameReadStatus::kTruncated),
                        static_cast<int>(frame.status));
  TEST_ASSERT_EQUAL_UINT(0, frame.size);
  TEST_ASSERT_EQUAL_UINT(4, source.offset);
}

void rejectsZeroAndOversizedLengths() {
  const uint8_t zero[]{0, 0, 0, 0};
  Source source{zero, sizeof(zero)};
  uint64_t remaining = 4;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(FrameReadStatus::kInvalid),
                        static_cast<int>(source.next(remaining).status));
  const uint8_t huge[]{0xff, 0xff, 0xff, 0xff};
  Source oversized{huge, sizeof(huge)};
  remaining = UINT64_MAX;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(FrameReadStatus::kInvalid),
                        static_cast<int>(oversized.next(remaining).status));
}

void detectsReadFailureDespiteClaimedFileSize() {
  const uint8_t bytes[]{1, 0};
  Source source{bytes, sizeof(bytes)};
  uint64_t remaining = 5;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(FrameReadStatus::kReadFailed),
                        static_cast<int>(source.next(remaining).status));
}

void exactReadRejectsInvalidCountsAndInvalidBuffer() {
  uint8_t buffer[4]{};
  TEST_ASSERT_FALSE(app::core::recording::readExact(buffer, sizeof(buffer),
      [](uint8_t*, std::size_t capacity) { return capacity + 1; }));
  TEST_ASSERT_FALSE(app::core::recording::readExact(nullptr, 1,
      [](uint8_t*, std::size_t) { return std::size_t{0}; }));
  TEST_ASSERT_TRUE(app::core::recording::readExact(nullptr, 0,
      [](uint8_t*, std::size_t) { return std::size_t{0}; }));
}

void shortHeaderWriteDoesNotWritePayload() {
  const uint8_t data[]{1, 2, 3};
  unsigned calls = 0;
  TEST_ASSERT_FALSE(app::core::recording::writeFrame(data, sizeof(data), 100,
      [&](const uint8_t*, std::size_t size) {
        ++calls;
        return size - 1;
      }));
  TEST_ASSERT_EQUAL_UINT(1, calls);
}

void shortPayloadWriteFailsWithoutRetry() {
  const uint8_t data[]{1, 2, 3};
  unsigned calls = 0;
  TEST_ASSERT_FALSE(app::core::recording::writeFrame(data, sizeof(data), 100,
      [&](const uint8_t*, std::size_t size) {
        ++calls;
        return calls == 1 ? size : size - 1;
      }));
  TEST_ASSERT_EQUAL_UINT(2, calls);
}

void writerAndReaderShareLittleEndianFormat() {
  const uint8_t data[]{1, 2, 3};
  uint8_t bytes[7]{};
  std::size_t offset = 0;
  TEST_ASSERT_TRUE(app::core::recording::writeFrame(data, sizeof(data), 100,
      [&](const uint8_t* payload, std::size_t size) {
        std::memcpy(bytes + offset, payload, size);
        offset += size;
        return size;
      }));
  Source source{bytes, sizeof(bytes)};
  uint64_t remaining = sizeof(bytes);
  const auto frame = source.next(remaining);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(FrameReadStatus::kFrame),
                        static_cast<int>(frame.status));
  TEST_ASSERT_EQUAL_UINT(3, frame.size);
  TEST_ASSERT_EQUAL_MEMORY(data, bytes + 4, sizeof(data));
}

void invalidFrameWritesDoNotTouchStorage() {
  unsigned calls = 0;
  const auto write = [&](const uint8_t*, std::size_t size) {
    ++calls;
    return size;
  };
  const uint8_t data[]{1};
  TEST_ASSERT_FALSE(app::core::recording::writeFrame(nullptr, 1, 100, write));
  TEST_ASSERT_FALSE(app::core::recording::writeFrame(data, 0, 100, write));
  TEST_ASSERT_FALSE(app::core::recording::writeFrame(data, 1, 0, write));
  TEST_ASSERT_EQUAL_UINT(0, calls);
}

void preservesCompleteFramesBeforeATruncatedTail() {
  const uint8_t bytes[]{1, 0, 0, 0, 42, 2, 0, 0, 0, 43};
  Source source{bytes, sizeof(bytes)};
  uint64_t remaining = sizeof(bytes);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(FrameReadStatus::kFrame),
                        static_cast<int>(source.next(remaining).status));
  uint8_t payload = 0;
  TEST_ASSERT_EQUAL_UINT(1, source.read(&payload, 1));
  TEST_ASSERT_EQUAL_UINT(42, payload);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(FrameReadStatus::kTruncated),
                        static_cast<int>(source.next(remaining).status));
  TEST_ASSERT_EQUAL_UINT(9, source.offset);
}
}  // namespace

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(acceptsShortReadsAndExactBoundary);
  RUN_TEST(cleanEofDoesNotRead);
  RUN_TEST(partialHeaderIsNotAFrame);
  RUN_TEST(partialPayloadIsRejectedBeforeConsumption);
  RUN_TEST(rejectsZeroAndOversizedLengths);
  RUN_TEST(detectsReadFailureDespiteClaimedFileSize);
  RUN_TEST(exactReadRejectsInvalidCountsAndInvalidBuffer);
  RUN_TEST(shortHeaderWriteDoesNotWritePayload);
  RUN_TEST(shortPayloadWriteFailsWithoutRetry);
  RUN_TEST(writerAndReaderShareLittleEndianFormat);
  RUN_TEST(invalidFrameWritesDoNotTouchStorage);
  RUN_TEST(preservesCompleteFramesBeforeATruncatedTail);
  return UNITY_END();
}
