#include <unity.h>

#include "adapters/recording/sd_mjpeg_recorder.h"
#include "fake_storage.h"

namespace {
using app::adapters::SdMjpegRecorder;
constexpr uint8_t kFrame[]{1, 2, 3};

void start(SdMjpegRecorder& recorder) {
  TEST_ASSERT_TRUE(recorder.begin());
  TEST_ASSERT_TRUE(recorder.requestStart());
  recorder.service();
  TEST_ASSERT_TRUE(recorder.stats().active);
}

void createSegment(SdMjpegRecorder& recorder) {
  start(recorder);
  TEST_ASSERT_TRUE(recorder.appendFrame(kFrame, sizeof(kFrame)));
  TEST_ASSERT_TRUE(recorder.requestStop());
  recorder.service();
}

void validSnapshotPreserves64BitCounters() {
  SdMjpegRecorder recorder;
  start(recorder);
  TEST_ASSERT_TRUE(recorder.appendFrame(kFrame, sizeof(kFrame)));
  const auto stats = recorder.stats();
  TEST_ASSERT_TRUE(stats.snapshotValid);
  TEST_ASSERT_TRUE(stats.active);
  TEST_ASSERT_EQUAL_UINT64(fake::totalBytes, stats.totalBytes);
  TEST_ASSERT_EQUAL_UINT64(15, stats.bytes);
  TEST_ASSERT_EQUAL_UINT(1, stats.frames);
}

void failedSnapshotNeverReadsLiveState() {
  SdMjpegRecorder recorder;
  start(recorder);
  TEST_ASSERT_TRUE(recorder.appendFrame(kFrame, sizeof(kFrame)));
  const unsigned accesses = fake::filesystemCalls;
  fake::mutex.failTake = true;
  const auto stats = recorder.stats();
  TEST_ASSERT_TRUE(stats.supported);
  TEST_ASSERT_FALSE(stats.snapshotValid);
  TEST_ASSERT_FALSE(stats.active);
  TEST_ASSERT_EQUAL_UINT64(0, stats.totalBytes);
  TEST_ASSERT_EQUAL_UINT64(0, stats.bytes);
  TEST_ASSERT_EQUAL_UINT(0, stats.frames);
  TEST_ASSERT_EQUAL_UINT(accesses, fake::filesystemCalls);
  fake::mutex.failTake = false;
  const auto recovered = recorder.stats();
  TEST_ASSERT_TRUE(recovered.snapshotValid);
  TEST_ASSERT_TRUE(recovered.active);
  TEST_ASSERT_EQUAL_UINT64(15, recovered.bytes);
}

void stopTimeoutIsNotAcceptedAndCanBeRetried() {
  SdMjpegRecorder recorder;
  start(recorder);
  fake::mutex.failTake = true;
  TEST_ASSERT_FALSE(recorder.requestStop());
  TEST_ASSERT_TRUE(recorder.wantsFrames());
  fake::mutex.failTake = false;
  recorder.service();
  TEST_ASSERT_TRUE(recorder.stats().active);
  TEST_ASSERT_TRUE(recorder.requestStop());
  recorder.service();
  TEST_ASSERT_FALSE(recorder.wantsFrames());
  TEST_ASSERT_FALSE(recorder.stats().active);
}

void failedStartDoesNotQueueRecording() {
  SdMjpegRecorder recorder;
  TEST_ASSERT_TRUE(recorder.begin());
  fake::mutex.failTake = true;
  TEST_ASSERT_FALSE(recorder.requestStart());
  TEST_ASSERT_FALSE(recorder.wantsFrames());
  fake::mutex.failTake = false;
  recorder.service();
  TEST_ASSERT_FALSE(recorder.stats().active);
}

void failedAppendAndServiceNeverTouchFiles() {
  SdMjpegRecorder recorder;
  start(recorder);
  const unsigned accesses = fake::filesystemCalls;
  fake::mutex.failTake = true;
  TEST_ASSERT_FALSE(recorder.appendFrame(kFrame, sizeof(kFrame)));
  fake::now = 5000;
  recorder.service();
  TEST_ASSERT_TRUE(recorder.wantsFrames());
  TEST_ASSERT_EQUAL_UINT(accesses, fake::filesystemCalls);
  fake::mutex.failTake = false;
  TEST_ASSERT_EQUAL_UINT(0, recorder.stats().frames);
  TEST_ASSERT_TRUE(recorder.appendFrame(kFrame, sizeof(kFrame)));
}

void listingTimeoutIsNotAnEmptySuccess() {
  SdMjpegRecorder recorder;
  TEST_ASSERT_TRUE(recorder.begin());
  app::ports::RecordingFileInfo files[2]{};
  std::size_t count = 99;
  TEST_ASSERT_TRUE(recorder.list(files, 2, count));
  TEST_ASSERT_EQUAL_UINT(0, count);
  fake::mutex.failTake = true;
  count = 99;
  TEST_ASSERT_FALSE(recorder.list(files, 2, count));
  TEST_ASSERT_EQUAL_UINT(0, count);
}

void listingAndReaderMetadataAreValidUnderLock() {
  SdMjpegRecorder recorder;
  createSegment(recorder);
  app::ports::RecordingFileInfo files[2]{};
  std::size_t count = 0;
  TEST_ASSERT_TRUE(recorder.list(files, 2, count));
  TEST_ASSERT_EQUAL_UINT(1, count);
  TEST_ASSERT_EQUAL_UINT(1, files[0].index);
  TEST_ASSERT_EQUAL_UINT64(15, files[0].bytes);
  TEST_ASSERT_TRUE(recorder.openReader(1));
  uint64_t size = 0;
  TEST_ASSERT_TRUE(recorder.readerSize(size));
  TEST_ASSERT_EQUAL_UINT64(15, size);
  TEST_ASSERT_TRUE(recorder.closeReader());
}

void readerCloseTimeoutRetainsOwnershipUntilRetry() {
  SdMjpegRecorder recorder;
  createSegment(recorder);
  TEST_ASSERT_TRUE(recorder.openReader(1));
  const unsigned accesses = fake::filesystemCalls;
  fake::mutex.failTake = true;
  uint64_t size = 99;
  TEST_ASSERT_FALSE(recorder.readerSize(size));
  TEST_ASSERT_EQUAL_UINT64(0, size);
  TEST_ASSERT_FALSE(recorder.closeReader());
  uint8_t byte = 0;
  TEST_ASSERT_EQUAL_UINT(0, recorder.read(&byte, 1));
  TEST_ASSERT_EQUAL_UINT(accesses, fake::filesystemCalls);
  fake::mutex.failTake = false;
  TEST_ASSERT_FALSE(recorder.requestStart());
  TEST_ASSERT_TRUE(recorder.readerSize(size));
  TEST_ASSERT_EQUAL_UINT64(15, size);
  TEST_ASSERT_TRUE(recorder.closeReader());
  TEST_ASSERT_FALSE(recorder.readerSize(size));
  TEST_ASSERT_EQUAL_UINT64(0, size);
  TEST_ASSERT_TRUE(recorder.requestStart());
}

void failedMutationCannotDeleteOrOpenReader() {
  SdMjpegRecorder recorder;
  createSegment(recorder);
  const unsigned accesses = fake::filesystemCalls;
  fake::mutex.failTake = true;
  TEST_ASSERT_FALSE(recorder.remove(1));
  TEST_ASSERT_FALSE(recorder.openReader(1));
  TEST_ASSERT_EQUAL_UINT(accesses, fake::filesystemCalls);
  fake::mutex.failTake = false;
  TEST_ASSERT_TRUE(recorder.openReader(1));
  TEST_ASSERT_TRUE(recorder.closeReader());
}

void shortWriteStopsTheRealAdapter() {
  SdMjpegRecorder recorder;
  start(recorder);
  fake::shortWrite = true;
  TEST_ASSERT_FALSE(recorder.appendFrame(kFrame, sizeof(kFrame)));
  const auto stats = recorder.stats();
  TEST_ASSERT_TRUE(stats.snapshotValid);
  TEST_ASSERT_FALSE(stats.active);
  TEST_ASSERT_EQUAL_UINT(1, stats.failures);
  TEST_ASSERT_EQUAL_UINT(0, stats.frames);
}

void missingMutexFailsSafely() {
  fake::failCreate = true;
  SdMjpegRecorder recorder;
  TEST_ASSERT_FALSE(recorder.begin());
  TEST_ASSERT_FALSE(recorder.stats().snapshotValid);
  TEST_ASSERT_FALSE(recorder.requestStop());
  TEST_ASSERT_FALSE(recorder.requestStart());
  uint64_t size = 99;
  TEST_ASSERT_FALSE(recorder.readerSize(size));
  TEST_ASSERT_EQUAL_UINT64(0, size);
  TEST_ASSERT_TRUE(recorder.closeReader());
  TEST_ASSERT_EQUAL_UINT(0, fake::filesystemCalls);
}

void acceptedStopSurvivesServiceLockTimeout() {
  SdMjpegRecorder recorder;
  start(recorder);
  TEST_ASSERT_TRUE(recorder.requestStop());
  fake::mutex.failTake = true;
  recorder.service();
  TEST_ASSERT_TRUE(recorder.wantsFrames());
  fake::mutex.failTake = false;
  recorder.service();
  TEST_ASSERT_FALSE(recorder.wantsFrames());
  TEST_ASSERT_FALSE(recorder.stats().active);
}

void pendingStartIsRetainedButCannotBeListedAsIdle() {
  SdMjpegRecorder recorder;
  TEST_ASSERT_TRUE(recorder.begin());
  TEST_ASSERT_TRUE(recorder.requestStart());
  fake::mutex.failTake = true;
  recorder.service();
  TEST_ASSERT_TRUE(recorder.wantsFrames());
  fake::mutex.failTake = false;
  app::ports::RecordingFileInfo files[2]{};
  std::size_t count = 99;
  TEST_ASSERT_FALSE(recorder.list(files, 2, count));
  TEST_ASSERT_EQUAL_UINT(0, count);
  recorder.service();
  TEST_ASSERT_TRUE(recorder.stats().active);
}

void acceptedStartCancelsQueuedStopWithoutRestartingSegment() {
  SdMjpegRecorder recorder;
  start(recorder);
  TEST_ASSERT_TRUE(recorder.appendFrame(kFrame, sizeof(kFrame)));
  TEST_ASSERT_TRUE(recorder.requestStop());
  TEST_ASSERT_TRUE(recorder.requestStart());
  recorder.service();
  const auto stats = recorder.stats();
  TEST_ASSERT_TRUE(stats.active);
  TEST_ASSERT_FALSE(stats.startPending);
  TEST_ASSERT_EQUAL_UINT(1, stats.segment);
  TEST_ASSERT_EQUAL_UINT(1, stats.frames);
}

void stopCancelsPendingStartBeforeService() {
  SdMjpegRecorder recorder;
  TEST_ASSERT_TRUE(recorder.begin());
  TEST_ASSERT_TRUE(recorder.requestStart());
  TEST_ASSERT_TRUE(recorder.requestStop());
  recorder.service();
  TEST_ASSERT_FALSE(recorder.wantsFrames());
  TEST_ASSERT_FALSE(recorder.stats().active);
  TEST_ASSERT_TRUE(fake::files.empty());
}

void failedStartCannotCancelAnAcceptedStop() {
  SdMjpegRecorder recorder;
  start(recorder);
  TEST_ASSERT_TRUE(recorder.requestStop());
  fake::mutex.failTake = true;
  TEST_ASSERT_FALSE(recorder.requestStart());
  fake::mutex.failTake = false;
  recorder.service();
  TEST_ASSERT_FALSE(recorder.stats().active);
  TEST_ASSERT_FALSE(recorder.wantsFrames());
}
}  // namespace

void setUp() { fake::reset(); }
void tearDown() {
  TEST_ASSERT_EQUAL_UINT(0, fake::unlockedCalls);
  TEST_ASSERT_FALSE(fake::mutex.locked);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(validSnapshotPreserves64BitCounters);
  RUN_TEST(failedSnapshotNeverReadsLiveState);
  RUN_TEST(stopTimeoutIsNotAcceptedAndCanBeRetried);
  RUN_TEST(failedStartDoesNotQueueRecording);
  RUN_TEST(failedAppendAndServiceNeverTouchFiles);
  RUN_TEST(listingTimeoutIsNotAnEmptySuccess);
  RUN_TEST(listingAndReaderMetadataAreValidUnderLock);
  RUN_TEST(readerCloseTimeoutRetainsOwnershipUntilRetry);
  RUN_TEST(failedMutationCannotDeleteOrOpenReader);
  RUN_TEST(shortWriteStopsTheRealAdapter);
  RUN_TEST(missingMutexFailsSafely);
  RUN_TEST(acceptedStopSurvivesServiceLockTimeout);
  RUN_TEST(pendingStartIsRetainedButCannotBeListedAsIdle);
  RUN_TEST(acceptedStartCancelsQueuedStopWithoutRestartingSegment);
  RUN_TEST(stopCancelsPendingStartBeforeService);
  RUN_TEST(failedStartCannotCancelAnAcceptedStop);
  return UNITY_END();
}
