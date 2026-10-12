#include "adapters/http_control_server.h"

#include <Arduino.h>
#include <WiFi.h>

#include "config/app_config.h"
#include "core/camera_settings.h"
#include "core/http_routes.h"
#include "ui/index_html.h"

namespace app::adapters {
namespace {

constexpr char kJsonContentType[] = "application/json";
constexpr char kHtmlContentType[] = "text/html; charset=utf-8";
constexpr char kJpegContentType[] = "image/jpeg";
constexpr char kCacheControl[] = "no-store, no-cache, must-revalidate";
constexpr char kCorsOrigin[] = "*";
constexpr std::size_t kQueryCapacity = 96;
constexpr std::size_t kVariableCapacity = 32;
constexpr std::size_t kValueCapacity = 16;

HttpControlServer& owner(httpd_req_t* request) {
  return *static_cast<HttpControlServer*>(request->user_ctx);
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
      makeUri(core::routes::kControl, controlHandler, this),
      makeUri(core::routes::kStopStreams, stopStreamsHandler, this),
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
  char response[448];
  snprintf(response, sizeof(response),
            "{\"status\":\"ok\",\"uptime_ms\":%lu,\"free_heap\":%u,"
            "\"min_free_heap\":%u,\"max_alloc_heap\":%u,\"rssi\":%d,"
            "\"stream_server\":%s,\"stream_active\":%s,"
            "\"stream_sessions\":%u,\"stream_frames\":%u,"
            "\"stream_fps\":%u.%u,\"stream_stack_headroom\":%u,"
            "\"stream_clients\":%u,\"stream_peer\":\"%s\"}",
            millis(), ESP.getFreeHeap(), ESP.getMinFreeHeap(),
            ESP.getMaxAllocHeap(), WiFi.RSSI(),
            stats.started ? "true" : "false",
            stats.clients > 0 ? "true" : "false", stats.sessions, stats.frames,
            static_cast<unsigned int>(stats.framesPerSecondTimes10 / 10),
            static_cast<unsigned int>(stats.framesPerSecondTimes10 % 10),
            stats.stackHeadroomBytes,
            stats.clients, peer);
  httpd_resp_set_type(request, kJsonContentType);
  setCommonHeaders(request);
  return httpd_resp_send(request, response, HTTPD_RESP_USE_STRLEN);
}

esp_err_t HttpControlServer::statusHandler(httpd_req_t* request) {
  const core::CameraStatus status = owner(request).camera_.status();
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
  ports::CameraFrame frame;
  if (!owner(request).camera_.acquire(frame)) {
    return httpd_resp_send_500(request);
  }
  httpd_resp_set_type(request, kJpegContentType);
  httpd_resp_set_hdr(request, "Content-Disposition",
                     "inline; filename=capture.jpg");
  setCommonHeaders(request);
  const esp_err_t result = httpd_resp_send(
      request, reinterpret_cast<const char*>(frame.data), frame.size);
  owner(request).camera_.release(frame);
  return result;
}

esp_err_t HttpControlServer::controlHandler(httpd_req_t* request) {
  char query[kQueryCapacity]{};
  char variable[kVariableCapacity]{};
  char value[kValueCapacity]{};
  if (httpd_req_get_url_query_str(request, query, sizeof(query)) != ESP_OK ||
      httpd_query_key_value(query, core::routes::kQueryVariable, variable,
                            sizeof(variable)) != ESP_OK ||
      httpd_query_key_value(query, core::routes::kQueryValue, value,
                            sizeof(value)) != ESP_OK) {
    return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST,
                               "var and val are required");
  }

  core::CameraSetting setting;
  if (!core::parseCameraSetting(variable, setting) ||
      !owner(request).camera_.apply(setting, atoi(value))) {
    return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST,
                               "unsupported or invalid setting");
  }
  setCommonHeaders(request);
  return httpd_resp_send(request, nullptr, 0);
}

esp_err_t HttpControlServer::stopStreamsHandler(httpd_req_t* request) {
  owner(request).streams_.stopAll();
  httpd_resp_set_type(request, kJsonContentType);
  setCommonHeaders(request);
  return httpd_resp_sendstr(request, "{\"stopping\":true}");
}

void HttpControlServer::setCommonHeaders(httpd_req_t* request) {
  httpd_resp_set_hdr(request, "Access-Control-Allow-Origin", kCorsOrigin);
  httpd_resp_set_hdr(request, "Cache-Control", kCacheControl);
}

httpd_uri_t HttpControlServer::makeUri(
    const char* path, esp_err_t (*handler)(httpd_req_t*),
    HttpControlServer* server) {
  httpd_uri_t uri{};
  uri.uri = path;
  uri.method = HTTP_GET;
  uri.handler = handler;
  uri.user_ctx = server;
  return uri;
}

}  // namespace app::adapters
