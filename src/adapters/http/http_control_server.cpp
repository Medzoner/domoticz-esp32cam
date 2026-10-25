#include "adapters/http/http_control_server.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <sys/types.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "adapters/http/http_request.h"
#include "adapters/http/http_response.h"
#include "config/http_config.h"
#include "config/recording_config.h"
#include "config/stream_config.h"
#include "core/camera/camera_settings.h"
#include "core/http/http_routes.h"
#include "ui_assets.h"

namespace app::adapters {
namespace {

constexpr char kContentDispositionHeader[] = "Content-Disposition";
constexpr char kContentDispositionValue[] = "inline; filename=capture.jpg";

constexpr char kMessageBadQuery[] = "var and val are required";
constexpr char kMessageBadSetting[] = "unsupported or invalid setting";
constexpr char kMessageCameraUnavailable[] = "camera unavailable";
constexpr char kMessageOutOfMemory[] = "out of memory";
constexpr char kMessageRecordingUnavailable[] = "SD recording unavailable";
constexpr char kMessageRecordingBusy[] = "stop recording and playback first";
constexpr char kMessageBadRecordingId[] = "invalid recording id";
constexpr char kMessageDeleteFailed[] = "recording could not be deleted";

constexpr std::size_t kVariableCapacity = 32;

}  // namespace

HttpControlServer::HttpControlServer(
    ports::CameraPort& camera, MjpegServer& streams,
    RecordingPlaybackServer& playback,
    ports::RecordingPort& recordings,
    ports::AuthenticationPort& authentication)
    : camera_(camera),
      streams_(streams),
      playback_(playback),
      recordings_(recordings),
      middleware_(authentication) {}

bool HttpControlServer::begin() {
  const http::Route routes[] = {
      {core::routes::kIndex, HTTP_GET, indexHandler},
      {core::routes::kHealth, HTTP_GET, healthHandler},
      {core::routes::kStatus, HTTP_GET, statusHandler},
      {core::routes::kCapture, HTTP_GET, captureHandler},
      {core::routes::kControl, HTTP_POST, controlHandler},
      {core::routes::kStopStreams, HTTP_POST, stopStreamsHandler},
      {core::routes::kRecordingStart, HTTP_POST, recordingStartHandler},
      {core::routes::kRecordingStop, HTTP_POST, recordingStopHandler},
      {core::routes::kRecordings, HTTP_GET, recordingsHandler},
      {core::routes::kRecordingDelete, HTTP_POST, recordingDeleteHandler},
  };
  return router_.begin(this, routes, sizeof(routes) / sizeof(routes[0]));
}

esp_err_t HttpControlServer::indexHandler(httpd_req_t* request) {
  HttpControlServer& self = owner(request);
  if (!self.middleware_.handle(request, http::AccessPolicy::kRead)) {
    return ESP_OK;
  }
  httpd_resp_set_type(request, http::response::kHtmlContentType);
  http::response::setSecurityHeaders(request);
  return httpd_resp_send(request, ui::kIndexHtml, HTTPD_RESP_USE_STRLEN);
}

esp_err_t HttpControlServer::healthHandler(httpd_req_t* request) {
  HttpControlServer& self = owner(request);
  if (!self.middleware_.handle(request, http::AccessPolicy::kRead)) {
    return ESP_OK;
  }
  const StreamStats stats = self.streams_.stats();
  const core::ServiceState streamState = self.streams_.state();
  const core::ServiceState playbackState = self.playback_.state();
  const ports::RecordingStats recording = self.recordings_.stats();
  char peer[16] = "none";
  if (stats.clients > 0) {
    snprintf(peer, sizeof(peer), "%u.%u.%u.%u",
             static_cast<unsigned int>(stats.peer[0]),
             static_cast<unsigned int>(stats.peer[1]),
             static_cast<unsigned int>(stats.peer[2]),
             static_cast<unsigned int>(stats.peer[3]));
  }
  char body[1088]{};
  const int length = snprintf(body, sizeof(body),
           "{\"status\":\"%s\",\"stream_service\":\"%s\","
           "\"playback_service\":\"%s\",\"uptime_ms\":%lu,"
           "\"chip_temperature_c\":%.1f,\"heap_size\":%u,"
           "\"free_heap\":%u,\"psram_size\":%u,\"free_psram\":%u,"
           "\"min_free_heap\":%u,\"max_alloc_heap\":%u,\"rssi\":%d,"
           "\"stream_server\":%s,\"stream_active\":%s,"
           "\"stream_sessions\":%u,\"stream_frames\":%u,"
           "\"stream_fps\":%u.%u,\"stream_stack_headroom\":%u,"
           "\"stream_delivery_ms\":%u,\"stream_target_fps\":%u,"
           "\"stream_quality_offset\":%u,\"stream_pressure\":%u,"
           "\"stream_clients\":%u,\"stream_peer\":\"%s\","
           "\"recording_supported\":%s,\"sd_available\":%s,"
           "\"recording_active\":%s,\"recording_pending\":%s,"
           "\"recording_segment\":%u,\"recording_segments\":%u,"
           "\"recording_frames\":%u,\"recording_failures\":%u,"
           "\"recording_bytes\":%llu,\"sd_total_bytes\":%llu,"
           "\"sd_free_bytes\":%llu}",
           streamState == core::ServiceState::kRunning &&
                   playbackState == core::ServiceState::kRunning ? "ok" : "degraded",
           core::serviceStateName(streamState), core::serviceStateName(playbackState),
           millis(), static_cast<double>(temperatureRead()), ESP.getHeapSize(),
           ESP.getFreeHeap(), ESP.getPsramSize(), ESP.getFreePsram(),
           ESP.getMinFreeHeap(), ESP.getMaxAllocHeap(), WiFi.RSSI(),
            streamState == core::ServiceState::kRunning ? "true" : "false",
           stats.clients > 0 ? "true" : "false", stats.sessions, stats.frames,
           static_cast<unsigned int>(stats.framesPerSecondTimes10 / 10),
           static_cast<unsigned int>(stats.framesPerSecondTimes10 % 10),
           stats.stackHeadroomBytes, stats.averageDeliveryMs, stats.targetFps,
           stats.jpegQualityOffset, stats.pressureLevel, stats.clients, peer,
           recording.supported ? "true" : "false",
           recording.available ? "true" : "false",
           recording.active ? "true" : "false",
           recording.startPending ? "true" : "false", recording.segment,
           recording.segments, recording.frames, recording.failures,
           static_cast<unsigned long long>(recording.bytes),
           static_cast<unsigned long long>(recording.totalBytes),
            static_cast<unsigned long long>(recording.freeBytes));
  if (length < 0 || static_cast<std::size_t>(length) >= sizeof(body)) {
    return http::response::error(request,
                                 http::response::kStatusInternalServerError,
                                 "health response overflow");
  }
  return http::response::json(request, body);
}

esp_err_t HttpControlServer::statusHandler(httpd_req_t* request) {
  HttpControlServer& self = owner(request);
  if (!self.middleware_.handle(request, http::AccessPolicy::kRead)) {
    return ESP_OK;
  }
  core::CameraStatus status = self.camera_.status();
  status.quality = self.streams_.baseJpegQuality();
  char body[256]{};
  snprintf(body, sizeof(body),
           "{\"framesize\":%u,\"quality\":%u,\"brightness\":%d,"
           "\"contrast\":%d,\"saturation\":%d,\"vflip\":%s,"
           "\"hmirror\":%s,\"flash\":%s}",
           status.frameSize, status.quality, status.brightness, status.contrast,
           status.saturation, status.verticalFlip ? "true" : "false",
           status.horizontalMirror ? "true" : "false",
           status.flashEnabled ? "true" : "false");
  return http::response::json(request, body);
}

esp_err_t HttpControlServer::captureHandler(httpd_req_t* request) {
  HttpControlServer& self = owner(request);
  if (!self.middleware_.handle(request, http::AccessPolicy::kRead)) {
    return ESP_OK;
  }
  http::response::setSecurityHeaders(request);

  ports::CameraFrame frame;
  if (!self.camera_.acquire(frame) || frame.data == nullptr || frame.size == 0) {
    if (frame.data != nullptr) {
      self.camera_.release(frame);
    }
    return http::response::error(request,
                                 http::response::kStatusInternalServerError,
                                 kMessageCameraUnavailable);
  }

  const std::size_t length = frame.size;
  auto* copy = static_cast<uint8_t*>(
      heap_caps_malloc(length, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (copy == nullptr) {
    copy = static_cast<uint8_t*>(malloc(length));
  }
  if (copy == nullptr) {
    self.camera_.release(frame);
    return http::response::error(request,
                                 http::response::kStatusInternalServerError,
                                 kMessageOutOfMemory);
  }
  memcpy(copy, frame.data, length);
  self.camera_.release(frame);

  httpd_resp_set_type(request, http::response::kJpegContentType);
  httpd_resp_set_hdr(request, kContentDispositionHeader,
                     kContentDispositionValue);
  const uint32_t deadline = millis() + config::kCaptureSendBudgetMs;
  std::size_t offset = 0;
  while (offset < length &&
         static_cast<int32_t>(millis() - deadline) < 0) {
    const std::size_t chunk =
        std::min<std::size_t>(config::kStreamWriteBlockSize, length - offset);
    if (httpd_resp_send_chunk(request,
                              reinterpret_cast<const char*>(copy + offset),
                              static_cast<ssize_t>(chunk)) != ESP_OK) {
      break;
    }
    offset += chunk;
  }
  free(copy);
  httpd_resp_send_chunk(request, nullptr, 0);
  if (offset < length) {
    Serial.println("[http] capture aborted before the frame was complete");
    return ESP_FAIL;
  }
  return ESP_OK;
}

esp_err_t HttpControlServer::controlHandler(httpd_req_t* request) {
  HttpControlServer& self = owner(request);
  if (!self.middleware_.handle(request, http::AccessPolicy::kMutation)) {
    return ESP_OK;
  }
  char variable[kVariableCapacity]{};
  int parsedValue = 0;
  if (!http::request::queryValue(request, core::routes::kQueryVariable,
                                 variable, sizeof(variable)) ||
      !http::request::queryInteger(request, core::routes::kQueryValue,
                                   parsedValue)) {
    return http::response::error(request, http::response::kStatusBadRequest,
                                 kMessageBadQuery);
  }

  core::CameraSetting setting;
  if (!core::parseCameraSetting(variable, setting) ||
      !self.camera_.apply(setting, parsedValue)) {
    return http::response::error(request, http::response::kStatusBadRequest,
                                 kMessageBadSetting);
  }
  if (setting == core::CameraSetting::kQuality) {
    self.streams_.setBaseJpegQuality(parsedValue);
  }
  return http::response::empty(request);
}

esp_err_t HttpControlServer::stopStreamsHandler(httpd_req_t* request) {
  HttpControlServer& self = owner(request);
  if (!self.middleware_.handle(request, http::AccessPolicy::kMutation)) {
    return ESP_OK;
  }
  self.streams_.stopAll();
  return http::response::json(request, "{\"stopping\":true}");
}

esp_err_t HttpControlServer::recordingStartHandler(httpd_req_t* request) {
  HttpControlServer& self = owner(request);
  if (!self.middleware_.handle(request, http::AccessPolicy::kMutation)) {
    return ESP_OK;
  }
  if (self.streams_.state() != core::ServiceState::kRunning) {
    return http::response::error(request,
                                 http::response::kStatusServiceUnavailable,
                                 "stream service unavailable");
  }
  if (!self.recordings_.requestStart()) {
    return http::response::error(request,
                                 http::response::kStatusServiceUnavailable,
                                 kMessageRecordingUnavailable);
  }
  return http::response::json(request, "{\"starting\":true}");
}

esp_err_t HttpControlServer::recordingStopHandler(httpd_req_t* request) {
  HttpControlServer& self = owner(request);
  if (!self.middleware_.handle(request, http::AccessPolicy::kMutation)) {
    return ESP_OK;
  }
  self.recordings_.requestStop();
  return http::response::json(request, "{\"stopping\":true}");
}

esp_err_t HttpControlServer::recordingsHandler(httpd_req_t* request) {
  HttpControlServer& self = owner(request);
  if (!self.middleware_.handle(request, http::AccessPolicy::kRead)) {
    return ESP_OK;
  }
  const ports::RecordingStats stats = self.recordings_.stats();
  if (!stats.available) {
    return http::response::error(request,
                                 http::response::kStatusServiceUnavailable,
                                 kMessageRecordingUnavailable);
  }
  if (stats.active || stats.startPending) {
    return http::response::error(request, http::response::kStatusConflict,
                                 kMessageRecordingBusy);
  }

  ports::RecordingFileInfo files[config::kMaxRecordingFiles]{};
  const std::size_t count =
      self.recordings_.list(files, config::kMaxRecordingFiles);
  httpd_resp_set_type(request, http::response::kJsonContentType);
  http::response::setSecurityHeaders(request);
  if (httpd_resp_sendstr_chunk(request, "{\"files\":[") != ESP_OK) {
    return ESP_FAIL;
  }
  for (std::size_t index = 0; index < count; ++index) {
    char entry[96]{};
    snprintf(entry, sizeof(entry), "%s{\"id\":%u,\"bytes\":%llu}",
             index == 0 ? "" : ",", files[index].index,
             static_cast<unsigned long long>(files[index].bytes));
    if (httpd_resp_sendstr_chunk(request, entry) != ESP_OK) {
      return ESP_FAIL;
    }
  }
  if (httpd_resp_sendstr_chunk(request, "]}") != ESP_OK) {
    return ESP_FAIL;
  }
  return httpd_resp_send_chunk(request, nullptr, 0);
}

esp_err_t HttpControlServer::recordingDeleteHandler(httpd_req_t* request) {
  HttpControlServer& self = owner(request);
  if (!self.middleware_.handle(request, http::AccessPolicy::kMutation)) {
    return ESP_OK;
  }
  int recordingId = 0;
  if (!http::request::queryInteger(request, core::routes::kQueryId,
                                   recordingId) ||
      recordingId <= 0 ||
      recordingId >
          static_cast<int>(config::kMaximumRecordingSegmentIndex)) {
    return http::response::error(request, http::response::kStatusBadRequest,
                                 kMessageBadRecordingId);
  }
  if (!self.recordings_.remove(static_cast<uint32_t>(recordingId))) {
    return http::response::error(request, http::response::kStatusConflict,
                                 kMessageDeleteFailed);
  }
  return http::response::empty(request);
}

HttpControlServer& HttpControlServer::owner(httpd_req_t* request) {
  return *static_cast<HttpControlServer*>(request->user_ctx);
}

}  // namespace app::adapters
