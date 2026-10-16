#pragma once

#ifndef APP_ENABLE_SD_RECORDING
#define APP_ENABLE_SD_RECORDING 0
#endif

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
  void requestStop() override;
  bool wantsFrames() const override;
  void service() override;
  bool appendFrame(const uint8_t* data, std::size_t size) override;
  ports::RecordingStats stats() const override;
  std::size_t list(ports::RecordingFileInfo* files,
                   std::size_t capacity) override;
  bool remove(uint32_t index) override;
  bool openReader(uint32_t index) override;
  std::size_t read(uint8_t* destination, std::size_t capacity) override;
  uint64_t readerSize() const override;
  void closeReader() override;

 private:
#if APP_ENABLE_SD_RECORDING
  bool openNextSegment();
  void closeSegment();
  void refreshCapacity();

  fs::File file_;
  fs::File reader_;
  mutable SemaphoreHandle_t storageMutex_ = nullptr;
#endif
  volatile bool available_ = false;
  volatile bool active_ = false;
  volatile bool startRequested_ = false;
  volatile bool stopRequested_ = false;
  volatile uint32_t currentSegment_ = 0;
  volatile uint32_t segmentCount_ = 0;
  volatile uint32_t frameCount_ = 0;
  volatile uint32_t failureCount_ = 0;
  volatile uint64_t byteCount_ = 0;
  volatile uint64_t totalBytes_ = 0;
  volatile uint64_t freeBytes_ = 0;
  uint64_t segmentBytes_ = 0;
  uint32_t nextSegmentIndex_ = 1;
  uint32_t lastFlushAt_ = 0;
  uint64_t readerSize_ = 0;
};

}  // namespace app::adapters
