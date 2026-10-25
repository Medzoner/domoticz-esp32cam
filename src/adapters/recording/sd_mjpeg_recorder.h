#pragma once

#ifndef APP_ENABLE_SD_RECORDING
#define APP_ENABLE_SD_RECORDING 0
#endif

#include <atomic>

#if APP_ENABLE_SD_RECORDING
#include <FS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#endif

#include "ports/recording/recording_port.h"

namespace app::adapters {

class SdMjpegRecorder final : public ports::RecordingPort {
 public:
  bool begin() override;
  bool requestStart() override;
  bool requestStop() override;
  bool wantsFrames() const override;
  void service() override;
  bool appendFrame(const uint8_t* data, std::size_t size) override;
  ports::RecordingStats stats() const override;
  bool list(ports::RecordingFileInfo* files, std::size_t capacity,
            std::size_t& count) override;
  bool remove(uint32_t index) override;
  bool openReader(uint32_t index) override;
  std::size_t read(uint8_t* destination, std::size_t capacity) override;
  bool readerSize(uint64_t& size) const override;
  bool closeReader() override;

 private:
#if APP_ENABLE_SD_RECORDING
  bool openNextSegment();
  void closeSegment();
  void refreshCapacity();

  fs::File file_;
  fs::File reader_;
  mutable SemaphoreHandle_t storageMutex_ = nullptr;
#endif
  // Only these two flags are read outside storageMutex_ (capture-demand hint).
  std::atomic<bool> active_{false};
  std::atomic<bool> startRequested_{false};
  // All other state, including 64-bit counters and reader size, is mutex-owned.
  bool available_ = false;
  bool stopRequested_ = false;
  uint32_t currentSegment_ = 0;
  uint32_t segmentCount_ = 0;
  uint32_t frameCount_ = 0;
  uint32_t failureCount_ = 0;
  uint64_t byteCount_ = 0;
  uint64_t totalBytes_ = 0;
  uint64_t freeBytes_ = 0;
  uint64_t segmentBytes_ = 0;
  uint32_t nextSegmentIndex_ = 1;
  uint32_t lastFlushAt_ = 0;
  uint64_t readerSize_ = 0;
};

}  // namespace app::adapters
