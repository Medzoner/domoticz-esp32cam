#pragma once

#include <cstring>

#include "ports/camera/camera_port.h"

namespace app::adapters {

enum class FrameCopyResult {
  kCopied,
  kInvalidBuffer,
  kAcquireFailed,
  kInvalidFrame,
  kTooLarge
};

namespace detail {
class CameraFrameLease {
 public:
  CameraFrameLease(ports::CameraPort& camera, ports::CameraFrame& frame)
      : camera_(camera), frame_(frame) {}
  ~CameraFrameLease() { camera_.release(frame_); }
  CameraFrameLease(const CameraFrameLease&) = delete;
  CameraFrameLease& operator=(const CameraFrameLease&) = delete;

 private:
  ports::CameraPort& camera_;
  ports::CameraFrame& frame_;
};
}  // namespace detail

// Run a synchronous copy operation under a validated camera lease. Copy must
// not do network/storage I/O or retain the source. All outcomes release the
// successfully acquired frame before the caller can deliver data or errors.
template <typename Copy>
FrameCopyResult copyCameraFrameUsing(ports::CameraPort& camera, Copy copy) {
  ports::CameraFrame frame;
  if (!camera.acquire(frame)) {
    return FrameCopyResult::kAcquireFailed;
  }
  const detail::CameraFrameLease lease(camera, frame);
  if (frame.data == nullptr || frame.size == 0) {
    return FrameCopyResult::kInvalidFrame;
  }
  return copy(frame.data, frame.size);
}

// Destination is separately owned, non-overlapping storage. No allocation or
// delivery is performed while the camera frame is leased. On any unsuccessful
// result, size is zero and the caller must not consume destination's old bytes.
inline FrameCopyResult copyCameraFrame(ports::CameraPort& camera,
                                     uint8_t* destination, std::size_t capacity,
                                     std::size_t& size) {
  size = 0;
  if (destination == nullptr || capacity == 0) {
    return FrameCopyResult::kInvalidBuffer;
  }
  return copyCameraFrameUsing(camera, [&](const uint8_t* data, std::size_t length) {
    if (length > capacity) {
      return FrameCopyResult::kTooLarge;
    }
    std::memcpy(destination, data, length);
    size = length;
    return FrameCopyResult::kCopied;
  });
}

}  // namespace app::adapters
