#include <unity.h>

#include <cstring>

#include "adapters/camera/frame_copy.h"

namespace {
using app::adapters::FrameCopyResult;
using app::adapters::copyCameraFrame;

class FakeCamera : public app::ports::CameraPort {
 public:
  uint8_t pixels[4]{1, 2, 3, 4};
  std::size_t length = sizeof(pixels);
  bool available = true;
  bool nullPixels = false;
  bool locked = false;
  unsigned acquisitions = 0;
  unsigned releases = 0;

  bool begin() override { return true; }
  bool acquire(app::ports::CameraFrame& frame) override {
    ++acquisitions;
    if (!available) {
      return false;
    }
    TEST_ASSERT_FALSE(locked);
    locked = true;
    frame = {nullPixels ? nullptr : pixels, length, this};
    return true;
  }
  void release(app::ports::CameraFrame& frame) override {
    TEST_ASSERT_TRUE(locked);
    TEST_ASSERT_EQUAL_PTR(this, frame.nativeHandle);
    ++releases;
    locked = false;
    // A driver is free to reuse the native frame immediately after return.
    std::memset(pixels, 0xff, sizeof(pixels));
    frame = {};
  }
  bool apply(app::core::CameraSetting, int) override { return !locked; }
  app::core::CameraStatus status() override { return {}; }
};

void copiesBeforeReleaseAndUnlocksBeforeConsumption() {
  FakeCamera camera;
  uint8_t buffer[4]{};
  std::size_t size = 99;
  const uint8_t expected[]{1, 2, 3, 4};
  TEST_ASSERT_EQUAL_INT(static_cast<int>(FrameCopyResult::kCopied),
                        static_cast<int>(copyCameraFrame(camera, buffer, sizeof(buffer), size)));
  TEST_ASSERT_EQUAL_UINT(4, size);
  TEST_ASSERT_EQUAL_UINT(1, camera.releases);
  TEST_ASSERT_FALSE(camera.locked);
  TEST_ASSERT_EQUAL_HEX8(0xff, camera.pixels[0]);
  TEST_ASSERT_EQUAL_MEMORY(expected, buffer, sizeof(buffer));
  TEST_ASSERT_TRUE(camera.apply(app::core::CameraSetting::kQuality, 12));
}

void acquisitionFailureDoesNotReleaseUnownedFrame() {
  FakeCamera camera;
  camera.available = false;
  uint8_t buffer[4]{};
  std::size_t size = 99;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(FrameCopyResult::kAcquireFailed),
                        static_cast<int>(copyCameraFrame(camera, buffer, sizeof(buffer), size)));
  TEST_ASSERT_EQUAL_UINT(0, size);
  TEST_ASSERT_EQUAL_UINT(0, camera.releases);
}

void oversizedFrameIsReleasedWithoutPartialCopy() {
  FakeCamera camera;
  camera.length = 5;
  uint8_t buffer[4]{9, 9, 9, 9};
  const uint8_t expected[]{9, 9, 9, 9};
  std::size_t size = 99;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(FrameCopyResult::kTooLarge),
                        static_cast<int>(copyCameraFrame(camera, buffer, sizeof(buffer), size)));
  TEST_ASSERT_EQUAL_UINT(0, size);
  TEST_ASSERT_EQUAL_UINT(1, camera.releases);
  TEST_ASSERT_FALSE(camera.locked);
  TEST_ASSERT_EQUAL_MEMORY(expected, buffer, sizeof(buffer));
}

void invalidNativeFramesAreStillReleased() {
  FakeCamera camera;
  camera.nullPixels = true;
  uint8_t buffer[4]{};
  std::size_t size = 99;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(FrameCopyResult::kInvalidFrame),
                        static_cast<int>(copyCameraFrame(camera, buffer, sizeof(buffer), size)));
  TEST_ASSERT_EQUAL_UINT(0, size);
  TEST_ASSERT_EQUAL_UINT(1, camera.releases);
  camera.nullPixels = false;
  camera.length = 0;
  size = 99;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(FrameCopyResult::kInvalidFrame),
                        static_cast<int>(copyCameraFrame(camera, buffer, sizeof(buffer), size)));
  TEST_ASSERT_EQUAL_UINT(0, size);
  TEST_ASSERT_EQUAL_UINT(2, camera.releases);
}

void invalidDestinationsNeverAcquire() {
  FakeCamera camera;
  uint8_t buffer[4]{};
  std::size_t size = 99;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(FrameCopyResult::kInvalidBuffer),
                        static_cast<int>(copyCameraFrame(camera, nullptr, sizeof(buffer), size)));
  TEST_ASSERT_EQUAL_UINT(0, size);
  size = 99;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(FrameCopyResult::kInvalidBuffer),
                        static_cast<int>(copyCameraFrame(camera, buffer, 0, size)));
  TEST_ASSERT_EQUAL_UINT(0, size);
  TEST_ASSERT_EQUAL_UINT(0, camera.acquisitions);
}

void bufferCanBeReusedAcrossFramesAndFailures() {
  FakeCamera camera;
  uint8_t buffer[4]{};
  std::size_t size = 0;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(FrameCopyResult::kCopied),
                        static_cast<int>(copyCameraFrame(camera, buffer, sizeof(buffer), size)));
  camera.length = 5;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(FrameCopyResult::kTooLarge),
                        static_cast<int>(copyCameraFrame(camera, buffer, sizeof(buffer), size)));
  TEST_ASSERT_EQUAL_UINT(0, size);
  camera.pixels[0] = 42;
  camera.length = 1;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(FrameCopyResult::kCopied),
                        static_cast<int>(copyCameraFrame(camera, buffer, sizeof(buffer), size)));
  TEST_ASSERT_EQUAL_UINT(1, size);
  TEST_ASSERT_EQUAL_UINT(42, buffer[0]);
  TEST_ASSERT_EQUAL_UINT(3, camera.releases);
  TEST_ASSERT_FALSE(camera.locked);
}

void invalidSnapshotFramesNeverInvokeAllocatorAndAlwaysRelease() {
  FakeCamera camera;
  camera.nullPixels = true;
  unsigned allocations = 0;
  const auto allocateAndCopy = [&](const uint8_t*, std::size_t) {
    ++allocations;
    return FrameCopyResult::kCopied;
  };
  TEST_ASSERT_EQUAL_INT(static_cast<int>(FrameCopyResult::kInvalidFrame),
      static_cast<int>(app::adapters::copyCameraFrameUsing(camera, allocateAndCopy)));
  TEST_ASSERT_EQUAL_UINT(1, camera.releases);
  TEST_ASSERT_FALSE(camera.locked);
  camera.nullPixels = false;
  camera.length = 0;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(FrameCopyResult::kInvalidFrame),
      static_cast<int>(app::adapters::copyCameraFrameUsing(camera, allocateAndCopy)));
  TEST_ASSERT_EQUAL_UINT(2, camera.releases);
  TEST_ASSERT_EQUAL_UINT(0, allocations);
}

void snapshotAllocationFailureReleasesBeforeErrorHandling() {
  FakeCamera camera;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(FrameCopyResult::kInvalidBuffer),
      static_cast<int>(app::adapters::copyCameraFrameUsing(
          camera, [&](const uint8_t*, std::size_t) {
            TEST_ASSERT_TRUE(camera.locked);
            return FrameCopyResult::kInvalidBuffer;
          })));
  TEST_ASSERT_EQUAL_UINT(1, camera.releases);
  TEST_ASSERT_FALSE(camera.locked);
  TEST_ASSERT_TRUE(camera.apply(app::core::CameraSetting::kQuality, 12));
}

void failedSnapshotAcquisitionDoesNotAllocateOrRelease() {
  FakeCamera camera;
  camera.available = false;
  unsigned allocations = 0;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(FrameCopyResult::kAcquireFailed),
      static_cast<int>(app::adapters::copyCameraFrameUsing(
          camera, [&](const uint8_t*, std::size_t) {
            ++allocations;
            return FrameCopyResult::kCopied;
          })));
  TEST_ASSERT_EQUAL_UINT(0, camera.releases);
  TEST_ASSERT_EQUAL_UINT(0, allocations);
}
}  // namespace

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(copiesBeforeReleaseAndUnlocksBeforeConsumption);
  RUN_TEST(acquisitionFailureDoesNotReleaseUnownedFrame);
  RUN_TEST(oversizedFrameIsReleasedWithoutPartialCopy);
  RUN_TEST(invalidNativeFramesAreStillReleased);
  RUN_TEST(invalidDestinationsNeverAcquire);
  RUN_TEST(bufferCanBeReusedAcrossFramesAndFailures);
  RUN_TEST(invalidSnapshotFramesNeverInvokeAllocatorAndAlwaysRelease);
  RUN_TEST(snapshotAllocationFailureReleasesBeforeErrorHandling);
  RUN_TEST(failedSnapshotAcquisitionDoesNotAllocateOrRelease);
  return UNITY_END();
}
