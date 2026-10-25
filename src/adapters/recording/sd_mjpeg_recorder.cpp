#include "adapters/recording/sd_mjpeg_recorder.h"

#include <Arduino.h>

#if APP_ENABLE_SD_RECORDING
#include <SD_MMC.h>
#endif

#include "config/recording_config.h"
#include "core/recording/recording_rules.h"
#include "core/recording/recording_writer.h"

namespace app::adapters {
namespace {

#if APP_ENABLE_SD_RECORDING
class StorageLock {
 public:
  explicit StorageLock(SemaphoreHandle_t mutex) : mutex_(mutex) {
    locked_ = mutex_ != nullptr &&
              xSemaphoreTake(mutex_, pdMS_TO_TICKS(
                                         config::kRecordingLockTimeoutMs)) ==
                  pdTRUE;
  }
  ~StorageLock() {
    if (locked_) {
      xSemaphoreGive(mutex_);
    }
  }
  explicit operator bool() const { return locked_; }
  StorageLock(const StorageLock&) = delete;
  StorageLock& operator=(const StorageLock&) = delete;

 private:
  SemaphoreHandle_t mutex_;
  bool locked_ = false;
};
#endif

}  // namespace

bool SdMjpegRecorder::begin() {
#if APP_ENABLE_SD_RECORDING
  storageMutex_ = xSemaphoreCreateMutex();
  if (storageMutex_ == nullptr) {
    return false;
  }
  StorageLock lock(storageMutex_);
  if (!lock) {
    return false;
  }
  available_ = SD_MMC.begin("/sdcard", true, false);
  if (available_) {
    refreshCapacity();
  }
  return available_;
#else
  return false;
#endif
}

bool SdMjpegRecorder::requestStart() {
#if APP_ENABLE_SD_RECORDING
  StorageLock lock(storageMutex_);
  if (!lock || !available_ || reader_) {
    return false;
  }
  // Last accepted command wins. A start while still active cancels a queued
  // stop rather than acknowledging a request that service() would undo.
  stopRequested_ = false;
  if (active_ || startRequested_) {
    return true;
  }
  startRequested_ = true;
  return true;
#else
  return false;
#endif
}

bool SdMjpegRecorder::requestStop() {
#if APP_ENABLE_SD_RECORDING
  StorageLock lock(storageMutex_);
  if (!lock) {
    return false;
  }
  startRequested_ = false;
  stopRequested_ = true;
  return true;
#else
  return false;
#endif
}

bool SdMjpegRecorder::wantsFrames() const {
  return active_ || startRequested_;
}

void SdMjpegRecorder::service() {
#if APP_ENABLE_SD_RECORDING
  StorageLock lock(storageMutex_);
  if (!lock) {
    return;
  }
  if (stopRequested_) {
    closeSegment();
    stopRequested_ = false;
  }
  if (startRequested_ && !active_) {
    frameCount_ = 0;
    byteCount_ = 0;
    segmentCount_ = 0;
    startRequested_ = false;
    openNextSegment();
  }
  const uint32_t now = millis();
  if (active_ && now - lastFlushAt_ >= config::kRecordingFlushIntervalMs) {
    file_.flush();
    lastFlushAt_ = now;
    refreshCapacity();
  }
#endif
}

bool SdMjpegRecorder::appendFrame(const uint8_t* data, std::size_t size) {
#if APP_ENABLE_SD_RECORDING
  StorageLock lock(storageMutex_);
  if (!lock) {
    return false;
  }
  if (!active_ || data == nullptr || size == 0 ||
      size > config::kMaximumRecordedFrameBytes) {
    return false;
  }
  constexpr std::size_t kFrameHeaderBytes = sizeof(uint32_t);
  const std::size_t storedSize = size + kFrameHeaderBytes;
  if (freeBytes_ <= config::kRecordingMinimumFreeBytes ||
      storedSize > freeBytes_ - config::kRecordingMinimumFreeBytes) {
    ++failureCount_;
    closeSegment();
    return false;
  }
  if (core::recording::shouldStartNewSegment(
          segmentBytes_, storedSize, config::kRecordingSegmentMaxBytes)) {
    closeSegment();
    if (!openNextSegment()) {
      return false;
    }
  }
  if (!core::recording::writeFrame(data, size, config::kMaximumRecordedFrameBytes,
      [this](const uint8_t* bytes, std::size_t count) { return file_.write(bytes, count); })) {
    ++failureCount_;
    closeSegment();
    return false;
  }
  segmentBytes_ += storedSize;
  byteCount_ += storedSize;
  freeBytes_ -= storedSize;
  ++frameCount_;
  return true;
#else
  static_cast<void>(data);
  static_cast<void>(size);
  return false;
#endif
}

ports::RecordingStats SdMjpegRecorder::stats() const {
#if APP_ENABLE_SD_RECORDING
  StorageLock lock(storageMutex_);
  if (!lock) {
    ports::RecordingStats unavailable{};
    unavailable.supported = true;
    return unavailable;
  }
  return {true,
          available_,
          active_,
          startRequested_,
          currentSegment_,
          segmentCount_,
          frameCount_,
          failureCount_,
          byteCount_,
          totalBytes_,
          freeBytes_,
          true};
#else
  ports::RecordingStats unsupported{};
  unsupported.snapshotValid = true;
  return unsupported;
#endif
}

bool SdMjpegRecorder::list(ports::RecordingFileInfo* files, std::size_t capacity,
                           std::size_t& count) {
  count = 0;
#if APP_ENABLE_SD_RECORDING
  StorageLock lock(storageMutex_);
  if (!lock) {
    return false;
  }
  if (!available_ || active_ || startRequested_ || files == nullptr || capacity == 0) {
    return false;
  }
  fs::File root = SD_MMC.open("/");
  if (!root || !root.isDirectory()) {
    return false;
  }
  for (fs::File entry = root.openNextFile(); entry && count < capacity;
       entry = root.openNextFile()) {
    uint32_t index = 0;
    if (!entry.isDirectory() &&
        core::recording::parseSegmentPath(entry.name(), index)) {
      files[count++] = {index, entry.size()};
    }
    entry.close();
  }
  root.close();
  for (std::size_t i = 1; i < count; ++i) {
    const ports::RecordingFileInfo value = files[i];
    std::size_t position = i;
    while (position > 0 && files[position - 1].index > value.index) {
      files[position] = files[position - 1];
      --position;
    }
    files[position] = value;
  }
  return true;
#else
  static_cast<void>(files);
  static_cast<void>(capacity);
  return false;
#endif
}

bool SdMjpegRecorder::remove(uint32_t index) {
#if APP_ENABLE_SD_RECORDING
  StorageLock lock(storageMutex_);
  if (!lock) {
    return false;
  }
  if (!available_ || active_ || reader_) {
    return false;
  }
  char path[32]{};
  if (core::recording::formatSegmentPath(path, sizeof(path), index) == 0 ||
      !SD_MMC.exists(path)) {
    return false;
  }
  const bool removed = SD_MMC.remove(path);
  refreshCapacity();
  return removed;
#else
  static_cast<void>(index);
  return false;
#endif
}

bool SdMjpegRecorder::openReader(uint32_t index) {
#if APP_ENABLE_SD_RECORDING
  StorageLock lock(storageMutex_);
  if (!lock) {
    return false;
  }
  if (!available_ || active_ || startRequested_ || reader_) {
    return false;
  }
  char path[32]{};
  if (core::recording::formatSegmentPath(path, sizeof(path), index) == 0) {
    return false;
  }
  reader_ = SD_MMC.open(path, FILE_READ);
  if (!reader_) {
    return false;
  }
  readerSize_ = reader_.size();
  return true;
#else
  static_cast<void>(index);
  return false;
#endif
}

std::size_t SdMjpegRecorder::read(uint8_t* destination, std::size_t capacity) {
#if APP_ENABLE_SD_RECORDING
  StorageLock lock(storageMutex_);
  if (!lock) {
    return 0;
  }
  if (!reader_ || destination == nullptr || capacity == 0) {
    return 0;
  }
  return reader_.read(destination, capacity);
#else
  static_cast<void>(destination);
  static_cast<void>(capacity);
  return 0;
#endif
}

bool SdMjpegRecorder::readerSize(uint64_t& size) const {
  size = 0;
#if APP_ENABLE_SD_RECORDING
  StorageLock lock(storageMutex_);
  if (!lock || !reader_) {
    return false;
  }
  size = readerSize_;
  return true;
#else
  return false;
#endif
}

bool SdMjpegRecorder::closeReader() {
#if APP_ENABLE_SD_RECORDING
  // A failed mutex allocation cannot have created a reader in the first place.
  // begin() is called once during setup, before publishing this adapter.
  if (storageMutex_ == nullptr) {
    return true;
  }
  StorageLock lock(storageMutex_);
  if (!lock) {
    return false;
  }
  if (reader_) {
    reader_.close();
  }
  readerSize_ = 0;
  return true;
#else
  return true;
#endif
}

#if APP_ENABLE_SD_RECORDING
bool SdMjpegRecorder::openNextSegment() {
  refreshCapacity();
  if (freeBytes_ < config::kRecordingMinimumFreeBytes) {
    ++failureCount_;
    return false;
  }

  char path[32]{};
  while (nextSegmentIndex_ <= config::kMaximumRecordingSegmentIndex) {
    if (core::recording::formatSegmentPath(path, sizeof(path),
                                           nextSegmentIndex_) == 0) {
      ++failureCount_;
      return false;
    }
    if (!SD_MMC.exists(path)) {
      break;
    }
    ++nextSegmentIndex_;
  }
  if (nextSegmentIndex_ > config::kMaximumRecordingSegmentIndex) {
    ++failureCount_;
    return false;
  }

  file_ = SD_MMC.open(path, FILE_WRITE);
  if (!file_) {
    ++failureCount_;
    return false;
  }
  currentSegment_ = nextSegmentIndex_++;
  ++segmentCount_;
  if (file_.write(core::recording::kFileMagic,
                  sizeof(core::recording::kFileMagic)) !=
      sizeof(core::recording::kFileMagic)) {
    ++failureCount_;
    file_.close();
    return false;
  }
  segmentBytes_ = sizeof(core::recording::kFileMagic);
  byteCount_ += sizeof(core::recording::kFileMagic);
  lastFlushAt_ = millis();
  active_ = true;
  return true;
}

void SdMjpegRecorder::closeSegment() {
  if (file_) {
    file_.flush();
    file_.close();
  }
  active_ = false;
  segmentBytes_ = 0;
  refreshCapacity();
}

void SdMjpegRecorder::refreshCapacity() {
  totalBytes_ = SD_MMC.totalBytes();
  const uint64_t used = SD_MMC.usedBytes();
  freeBytes_ = used < totalBytes_ ? totalBytes_ - used : 0;
}
#endif

}  // namespace app::adapters
