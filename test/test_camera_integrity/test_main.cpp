#include <unity.h>

#include "core/camera/frame_buffer_policy.h"
#include "core/camera/jpeg_frame.h"

namespace {
void reservesUxgaButStartsVgaWithPsram() {
  const auto policy = app::core::frameBufferPolicy(true, 12);
  TEST_ASSERT_EQUAL_UINT(13, policy.allocationFrameSize);
  TEST_ASSERT_EQUAL_UINT(8, policy.startupFrameSize);
  TEST_ASSERT_EQUAL_UINT(10, policy.allocationQuality);
  TEST_ASSERT_EQUAL_UINT(12, policy.startupQuality);
}

void doesNotOverAllocateWithoutPsram() {
  const auto policy = app::core::frameBufferPolicy(false, 12);
  TEST_ASSERT_EQUAL_UINT(8, policy.allocationFrameSize);
  TEST_ASSERT_EQUAL_UINT(8, policy.startupFrameSize);
  TEST_ASSERT_EQUAL_UINT(12, policy.allocationQuality);
}

void preservesHigherConfiguredQualityForAllocation() {
  const auto policy = app::core::frameBufferPolicy(true, 4);
  TEST_ASSERT_EQUAL_UINT(4, policy.allocationQuality);
  TEST_ASSERT_EQUAL_UINT(4, policy.startupQuality);
}

void rejectsRuntimeSizesBeyondTheAllocatedMaximum() {
  TEST_ASSERT_TRUE(app::core::frameSizeFitsAllocation(13, 13));
  TEST_ASSERT_TRUE(app::core::frameSizeFitsAllocation(8, 8));
  TEST_ASSERT_FALSE(app::core::frameSizeFitsAllocation(9, 8));
  TEST_ASSERT_FALSE(app::core::frameSizeFitsAllocation(-1, 13));
  TEST_ASSERT_FALSE(app::core::frameSizeFitsAllocation(14, 13));
}

void checksJpegBoundaryMarkersWithoutReadingShortBuffers() {
  const uint8_t frame[]{0xff, 0xd8, 1, 2, 0xff, 0xd9};
  TEST_ASSERT_TRUE(app::core::hasJpegBoundaries(frame, sizeof(frame)));
  TEST_ASSERT_FALSE(app::core::hasJpegBoundaries(nullptr, sizeof(frame)));
  for (unsigned size = 0; size < sizeof(frame); ++size) {
    TEST_ASSERT_FALSE(app::core::hasJpegBoundaries(frame, size));
  }
}

void rejectsWrongStartAndMissingEndMarkers() {
  const uint8_t badStart[]{0xff, 0xd9, 1, 0xff, 0xd9};
  const uint8_t badEnd[]{0xff, 0xd8, 1, 0xff, 0xd8};
  TEST_ASSERT_FALSE(app::core::hasJpegBoundaries(badStart, sizeof(badStart)));
  TEST_ASSERT_FALSE(app::core::hasJpegBoundaries(badEnd, sizeof(badEnd)));
}
}  // namespace

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(reservesUxgaButStartsVgaWithPsram);
  RUN_TEST(doesNotOverAllocateWithoutPsram);
  RUN_TEST(preservesHigherConfiguredQualityForAllocation);
  RUN_TEST(rejectsRuntimeSizesBeyondTheAllocatedMaximum);
  RUN_TEST(checksJpegBoundaryMarkersWithoutReadingShortBuffers);
  RUN_TEST(rejectsWrongStartAndMissingEndMarkers);
  return UNITY_END();
}
