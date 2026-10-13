#include "adapters/http_control_server.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <sys/types.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "config/app_config.h"
#include "core/camera_settings.h"
#include "core/http_routes.h"
#include "core/http_security.h"
#include "ui/index_html.h"

namespace app::adapters {
namespace {

constexpr char kJsonContentType[] = "application/json";
constexpr char kTextContentType[] = "text/plain; charset=utf-8";
constexpr char kHtmlContentType[] = "text/html; charset=utf-8";
constexpr char kJpegContentType[] = "image/jpeg";
constexpr char kCacheControl[] = "no-store, no-cache, must-revalidate";

// httpd_resp_set_status() and httpd_resp_set_hdr() keep the pointer they are
// given, so every status line and header value must have static storage.
constexpr char kStatusForbidden[] = "403 Forbidden";
constexpr char kStatusBadRequest[] = "400 Bad Request";
constexpr char kStatusTooManyRequests[] = "429 Too Many Requests";
constexpr char kStatusInternalServerError[] = "500 Internal Server Error";
constexpr char kContentDispositionHeader[] = "Content-Disposition";
constexpr char kContentDispositionValue[] = "inline; filename=capture.jpg";

constexpr char kMessageCrossOrigin[] = "cross-origin request rejected";
constexpr char kMessageTooManyRequests[] = "too many mutations, slow down";
constexpr char kMessageBadQuery[] = "var and val are required";
constexpr char kMessageBadSetting[] = "unsupported or invalid setting";
constexpr char kMessageCameraUnavailable[] = "camera unavailable";
constexpr char kMessageOutOfMemory[] = "out of memory";

constexpr std::size_t kQueryCapacity = 96;
constexpr std::size_t kVariableCapacity = 32;
constexpr std::size_t kValueCapacity = 16;
constexpr std::size_t kHeaderValueCapacity = 192;

std::size_t copyHeader(httpd_req_t* request, const char* name, char* buffer,
                       std::size_t capacity) {
  if (buffer == nullptr || capacity == 0) {
    return 0;
  }
  buffer[0] = '\0';
  if (httpd_req_get_hdr_value_str(request, name, buffer, capacity) != ESP_OK) {
    buffer[0] = '\0';
    return 0;
  }
  return strlen(buffer);
}

}  // namespace

HttpControlServer::HttpControlServer(ports::CameraPort& camera,
                                     MjpegServer& streams)
    : camera_(camera), streams_(streams) {}

bool HttpControlServer::begin() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = config::kControlPort;
  config.max_open_sockets = config::kMaxControlClients;
  config.lru_purge_enable = true;
  config.recv_wait_timeout = 5;
  config.send_wait_timeout = 5;
  if (httpd_start(&server_, &config) != ESP_OK) {
    return false;
  }

  const httpd_uri_t routes[] = {
      makeUri(core::routes::kIndex, indexHandler, this),
      makeUri(core::routes::kHealth, healthHandler, this),
      makeUri(core::routes::kStatus, statusHandler, this),
      makeUri(core::routes::kCapture, captureHandler, this),
      makeUri(core::routes::kControl, controlHandler, this, HTTP_POST),
      makeUri(core::routes::kStopStreams, stopStreamsHandler, this, HTTP_POST),
  };
  for (const auto& route : routes) {
    if (httpd_register_uri_handler(server_, &route) != ESP_OK) {
      return false;
    }
  }
  return true;
}

esp_err_t HttpControlServer::indexHandler(httpd_req_t* request) {
  httpd_resp_set_type(request, kHtmlContentType);
  setCommonHeaders(request);
  return httpd_resp_send(request, ui::kIndexHtml, HTTPD_RESP_USE_STRLEN);
}

esp_err_t HttpControlServer::healthHandler(httpd_req_t* request) {
  const StreamStats stats = owner(request).streams_.stats();
  char peer[16] = "none";
  if (stats.clients > 0) {
    snprintf(peer, sizeof(peer), "%u.%u.%u.%u",
             static_cast<unsigned int>(stats.peer[0]),
             static_cast<unsigned int>(stats.peer[1]),
             static_cast<unsigned int>(stats.peer[2]),
             static_cast<unsigned int>(stats.peer[3]));
  }
  char response[640];
  snprintf(response, sizeof(response),
           "{\"status\":\"ok\",\"uptime_ms\":%lu,\"heap_size\":%u,"
           "\"free_heap\":%u,\"psram_size\":%u,\"free_psram\":%u,"
           "\"min_free_heap\":%u,\"max_alloc_heap\":%u,\"rssi\":%d,"
           "\"stream_server\":%s,\"stream_active\":%s,"
           "\"stream_sessions\":%u,\"stream_frames\":%u,"
           "\"stream_fps\":%u.%u,\"stream_stack_headroom\":%u,"
           "\"stream_delivery_ms\":%u,\"stream_target_fps\":%u,"
           "\"stream_quality_offset\":%u,\"stream_pressure\":%u,"
           "\"stream_clients\":%u,\"stream_peer\":\"%s\"}",
           millis(), ESP.getHeapSize(), ESP.getFreeHeap(), ESP.getPsramSize(),
           ESP.getFreePsram(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap(),
           WiFi.RSSI(),
           stats.started ? "true" : "false",
           stats.clients > 0 ? "true" : "false", stats.sessions, stats.frames,
           static_cast<unsigned int>(stats.framesPerSecondTimes10 / 10),
           static_cast<unsigned int>(stats.framesPerSecondTimes10 % 10),
           stats.stackHeadroomBytes, stats.averageDeliveryMs, stats.targetFps,
           stats.jpegQualityOffset, stats.pressureLevel, stats.clients, peer);
  httpd_resp_set_type(request, kJsonContentType);
  setCommonHeaders(request);
  return httpd_resp_send(request, response, HTTPD_RESP_USE_STRLEN);
}

esp_err_t HttpControlServer::statusHandler(httpd_req_t* request) {
  core::CameraStatus status = owner(request).camera_.status();
  status.quality = owner(request).streams_.baseJpegQuality();
  char response[256];
  snprintf(response, sizeof(response),
           "{\"framesize\":%u,\"quality\":%u,\"brightness\":%d,"
           "\"contrast\":%d,\"saturation\":%d,\"vflip\":%s,"
           "\"hmirror\":%s}",
           status.frameSize, status.quality, status.brightness, status.contrast,
           status.saturation, status.verticalFlip ? "true" : "false",
           status.horizontalMirror ? "true" : "false");
  httpd_resp_set_type(request, kJsonContentType);
  setCommonHeaders(request);
  return httpd_resp_send(request, response, HTTPD_RESP_USE_STRLEN);
}

esp_err_t HttpControlServer::captureHandler(httpd_req_t* request) {
  setCommonHeaders(request);

  // The frame is copied out of the camera buffer before a single byte goes on
  // the network, so the camera mutex is never held while a slow client reads.
  ports::CameraFrame frame;
  if (!owner(request).camera_.acquire(frame) || frame.data == nullptr ||
      frame.size == 0) {
    if (frame.data != nullptr) {
      owner(request).camera_.release(frame);
    }
    return reject(request, kStatusInternalServerError,
                  kMessageCameraUnavailable);
  }

  const std::size_t length = frame.size;
  auto* copy = static_cast<uint8_t*>(
      heap_caps_malloc(length, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (copy == nullptr) {
    copy = static_cast<uint8_t*>(malloc(length));
  }
  if (copy == nullptr) {
    owner(request).camera_.release(frame);
    return reject(request, kStatusInternalServerError, kMessageOutOfMemory);
  }
  memcpy(copy, frame.data, length);
  owner(request).camera_.release(frame);

  httpd_resp_set_type(request, kJpegContentType);
  httpd_resp_set_hdr(request, kContentDispositionHeader,
                     kContentDispositionValue);

  const uint32_t deadline = millis() + config::kCaptureSendBudgetMs;
  std::size_t offset = 0;
  while (offset < length) {
    if (static_cast<int32_t>(millis() - deadline) >= 0) {
      break;
    }
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
  const HttpAccess access = mutationAccess(request);
  if (access != HttpAccess::kGranted) {
    return denyAccess(request, access);
  }
  char query[kQueryCapacity]{};
  char variable[kVariableCapacity]{};
  char value[kValueCapacity]{};
  if (httpd_req_get_url_query_str(request, query, sizeof(query)) != ESP_OK ||
      httpd_query_key_value(query, core::routes::kQueryVariable, variable,
                            sizeof(variable)) != ESP_OK ||
      httpd_query_key_value(query, core::routes::kQueryValue, value,
                            sizeof(value)) != ESP_OK) {
    return reject(request, kStatusBadRequest, kMessageBadQuery);
  }

  core::CameraSetting setting;
  int parsedValue = 0;
  if (!core::parseCameraSetting(variable, setting) ||
      !core::security::parseInteger(value, parsedValue) ||
      !owner(request).camera_.apply(setting, parsedValue)) {
    return reject(request, kStatusBadRequest, kMessageBadSetting);
  }
  if (setting == core::CameraSetting::kQuality) {
    owner(request).streams_.setBaseJpegQuality(parsedValue);
  }
  setCommonHeaders(request);
  return httpd_resp_send(request, nullptr, 0);
}

esp_err_t HttpControlServer::stopStreamsHandler(httpd_req_t* request) {
  const HttpAccess access = mutationAccess(request);
  if (access != HttpAccess::kGranted) {
    return denyAccess(request, access);
  }
  owner(request).streams_.stopAll();
  httpd_resp_set_type(request, kJsonContentType);
  setCommonHeaders(request);
  return httpd_resp_sendstr(request, "{\"stopping\":true}");
}

HttpAccess HttpControlServer::mutationAccess(httpd_req_t* request) {
  // Browser mutations are accepted only from the camera UI. Requests without
  // browser origin headers remain available to trusted IoT clients.
  char origin[kHeaderValueCapacity]{};
  char referer[kHeaderValueCapacity]{};
  char host[kHeaderValueCapacity]{};
  const bool hasOrigin = copyHeader(request, "Origin", origin,
                                    sizeof(origin)) > 0;
  const bool hasReferer = copyHeader(request, "Referer", referer,
                                     sizeof(referer)) > 0;
  const bool hasHost = copyHeader(request, "Host", host, sizeof(host)) > 0;
  if (!hasHost ||
      !core::security::isSameOrigin(hasOrigin ? origin : nullptr,
                                    hasReferer ? referer : nullptr, host)) {
    return HttpAccess::kCrossOrigin;
  }

  HttpControlServer& self = owner(request);
  const uint32_t now = millis();
  if (self.lastMutationAt_ != 0 &&
      now - self.lastMutationAt_ < config::kControlMinIntervalMs) {
    return HttpAccess::kRateLimited;
  }
  self.lastMutationAt_ = now;
  return HttpAccess::kGranted;
}

esp_err_t HttpControlServer::denyAccess(httpd_req_t* request, HttpAccess access) {
  switch (access) {
    case HttpAccess::kCrossOrigin:
      return reject(request, kStatusForbidden, kMessageCrossOrigin);
    case HttpAccess::kRateLimited:
      return reject(request, kStatusTooManyRequests, kMessageTooManyRequests);
    case HttpAccess::kGranted:
      break;
  }
  return ESP_FAIL;
}

HttpControlServer& HttpControlServer::owner(httpd_req_t* request) {
  return *static_cast<HttpControlServer*>(request->user_ctx);
}

void HttpControlServer::setCommonHeaders(httpd_req_t* request) {
  // No wildcard CORS header: it would let any origin read the camera state.
  // The camera is reachable from the local network only, and mutations are
  // additionally screened by mutationAccess().
  httpd_resp_set_hdr(request, "Cache-Control", kCacheControl);
  httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
  httpd_resp_set_hdr(request, "X-Frame-Options", "DENY");
  httpd_resp_set_hdr(request, "Referrer-Policy", "same-origin");
}

esp_err_t HttpControlServer::reject(httpd_req_t* request, const char* status,
                                    const char* message) {
  httpd_resp_set_status(request, status);
  httpd_resp_set_type(request, kTextContentType);
  httpd_resp_set_hdr(request, "Cache-Control", kCacheControl);
  return httpd_resp_send(request, message, HTTPD_RESP_USE_STRLEN);
}

httpd_uri_t HttpControlServer::makeUri(
    const char* path, esp_err_t (*handler)(httpd_req_t*),
    HttpControlServer* server, httpd_method_t method) {
  httpd_uri_t uri{};
  uri.uri = path;
  uri.method = method;
  uri.handler = handler;
  uri.user_ctx = server;
  return uri;
}

}  // namespace app::adapters
