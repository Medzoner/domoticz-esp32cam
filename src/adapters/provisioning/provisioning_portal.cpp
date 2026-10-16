#include "adapters/provisioning/provisioning_portal.h"

#include <Arduino.h>
#include <WiFi.h>
#include <cstring>

#include "config/provisioning_config.h"
#include "core/http/http_routes.h"
#include "secrets.h"
#include "ui_assets.h"

// Optional passphrase for the setup access point. When it is empty the access
// point is open, which is acceptable because the portal only exists until the
// camera has been configured.
#ifndef CAMERA_SETUP_AP_PASSWORD
#define CAMERA_SETUP_AP_PASSWORD ""
#endif

namespace app::adapters {
namespace {

constexpr char kHtmlContentType[] = "text/html; charset=utf-8";
constexpr char kJsonContentType[] = "application/json";
constexpr char kNoStore[] = "no-store, no-cache, must-revalidate";
constexpr std::size_t kBodyCapacity = core::provisioning::kMaximumFormBodyBytes;
constexpr std::size_t kEscapeCapacity = 96;

// esp_http_server passes the request target including any query string, and
// invokes the matcher with the maximum number of bytes it may read.
bool matchUri(const char* pattern, const char* uri, std::size_t matchUpto) {
  if (pattern == nullptr || uri == nullptr) {
    return false;
  }
  std::size_t patternLength = 0;
  while (pattern[patternLength] != '\0') {
    ++patternLength;
  }
  std::size_t uriLength = 0;
  while (uriLength < matchUpto && uri[uriLength] != '\0' &&
         uri[uriLength] != '?') {
    ++uriLength;
  }

  if (patternLength > 0 && pattern[patternLength - 1] == '*') {
    const std::size_t prefixLength = patternLength - 1;
    return uriLength >= prefixLength &&
           std::strncmp(pattern, uri, prefixLength) == 0;
  }
  return uriLength == patternLength &&
         std::strncmp(pattern, uri, patternLength) == 0;
}

void escapeMarkup(const char* input, char* output, std::size_t capacity) {
  if (capacity == 0) {
    return;
  }
  std::size_t written = 0;
  for (const char* cursor = input; *cursor != '\0'; ++cursor) {
    const char character = *cursor;
    const char* replacement = nullptr;
    if (character == '&') {
      replacement = "&amp;";
    } else if (character == '<') {
      replacement = "&lt;";
    } else if (character == '>') {
      replacement = "&gt;";
    } else if (character == '"') {
      replacement = "&quot;";
    } else if (character == '\'') {
      replacement = "&#39;";
    }

    if (replacement != nullptr) {
      const std::size_t length = std::strlen(replacement);
      if (written + length + 1 > capacity) {
        break;
      }
      std::memcpy(output + written, replacement, length);
      written += length;
    } else {
      if (static_cast<unsigned char>(character) < 0x20) {
        continue;
      }
      if (written + 2 > capacity) {
        break;
      }
      output[written++] = character;
    }
  }
  output[written] = '\0';
}

void escapeJsonString(const char* input, char* output, std::size_t capacity) {
  if (capacity == 0) {
    return;
  }
  std::size_t written = 0;
  for (const char* cursor = input; *cursor != '\0'; ++cursor) {
    const char character = *cursor;
    if (character == '"' || character == '\\') {
      if (written + 3 > capacity) {
        break;
      }
      output[written++] = '\\';
      output[written++] = character;
    } else {
      if (static_cast<unsigned char>(character) < 0x20) {
        continue;
      }
      if (written + 2 > capacity) {
        break;
      }
      output[written++] = character;
    }
  }
  output[written] = '\0';
}

bool deadlineReached(std::uint32_t deadline) {
  return static_cast<std::int32_t>(millis() - deadline) >= 0;
}

esp_err_t sendPage(httpd_req_t* request, const char* before,
                   const char* payload, const char* after) {
  httpd_resp_set_type(request, kHtmlContentType);
  httpd_resp_set_hdr(request, "Cache-Control", kNoStore);
  if (httpd_resp_send_chunk(request, before, HTTPD_RESP_USE_STRLEN) != ESP_OK) {
    return ESP_FAIL;
  }
  if (payload != nullptr &&
      httpd_resp_send_chunk(request, payload, HTTPD_RESP_USE_STRLEN) !=
          ESP_OK) {
    return ESP_FAIL;
  }
  if (httpd_resp_send_chunk(request, after, HTTPD_RESP_USE_STRLEN) != ESP_OK) {
    return ESP_FAIL;
  }
  return httpd_resp_send_chunk(request, nullptr, 0);
}

bool registerRoute(httpd_handle_t server, const httpd_uri_t& route) {
  return httpd_register_uri_handler(server, &route) == ESP_OK;
}

}  // namespace

ProvisioningPortal& ProvisioningPortal::owner(httpd_req_t* request) {
  return *static_cast<ProvisioningPortal*>(request->user_ctx);
}

bool ProvisioningPortal::begin(bool restartOnTimeout) {
  restartOnTimeout_ = restartOnTimeout;
  startedAt_ = millis();

  snprintf(accessPointSsid_, sizeof(accessPointSsid_), "%s%s", CAMERA_HOSTNAME,
           config::kProvisioningAccessPointSuffix);
  if (accessPointSsid_[0] == '\0') {
    snprintf(accessPointSsid_, sizeof(accessPointSsid_), "%s",
             config::kProvisioningAccessPointName);
  }

  Serial.printf("Starting onboarding access point %s", accessPointSsid_);
  if (CAMERA_SETUP_AP_PASSWORD[0] != '\0') {
    Serial.println(" (WPA2 protected)");
  } else {
    Serial.println(" (open)");
  }

  if (!startAccessPoint() || !startNameService() || !startHttpServer()) {
    return false;
  }
  Serial.print("Onboarding page: http://");
  Serial.println(WiFi.softAPIP());
  return true;
}

bool ProvisioningPortal::startAccessPoint() {
  WiFi.persistent(false);
  WiFi.setSleep(false);
  WiFi.mode(WIFI_AP);
  const bool secured = CAMERA_SETUP_AP_PASSWORD[0] != '\0';
  const bool started =
      secured ? WiFi.softAP(accessPointSsid_, CAMERA_SETUP_AP_PASSWORD)
              : WiFi.softAP(accessPointSsid_);
  return started && WiFi.softAPIP() != IPAddress();
}

bool ProvisioningPortal::startNameService() {
  dnsServer_.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer_.setTTL(0);
  return dnsServer_.start(config::kProvisioningDnsPort, "*",
                          WiFi.softAPIP());
}

bool ProvisioningPortal::startHttpServer() {
  httpd_config_t httpConfig = HTTPD_DEFAULT_CONFIG();
  httpConfig.server_port = config::kProvisioningHttpPort;
  httpConfig.max_open_sockets = config::kMaxProvisioningClients;
  httpConfig.lru_purge_enable = true;
  httpConfig.recv_wait_timeout = 10;
  httpConfig.send_wait_timeout = 10;
  httpConfig.uri_match_fn = matchUri;
  if (httpd_start(&server_, &httpConfig) != ESP_OK) {
    return false;
  }

  const httpd_uri_t routes[] = {
      makeUri(core::routes::kProvisioningScan, scanHandler, this),
      makeUri(core::routes::kProvisioningSave, saveHandler, this, HTTP_POST),
      makeUri("/generate_204", redirectHandler, this),
      makeUri("/gen_204", redirectHandler, this),
      makeUri("/hotspot-detect.html", redirectHandler, this),
      makeUri("/library/test/success.html", redirectHandler, this),
      makeUri("/ncsi.txt", redirectHandler, this),
      makeUri("/connecttest.txt", redirectHandler, this),
      makeUri("/fwlink", redirectHandler, this),
      makeUri("/success.txt", redirectHandler, this),
      makeUri("/canonical.html", redirectHandler, this),
      makeUri("/kindle-wifi/wifistub.html", redirectHandler, this),
      makeUri(core::routes::kIndex, pageHandler, this),
      makeUri("/*", redirectHandler, this),
  };
  for (const auto& route : routes) {
    if (!registerRoute(server_, route)) {
      httpd_stop(server_);
      server_ = nullptr;
      return false;
    }
  }
  return true;
}

void ProvisioningPortal::loop() {
  dnsServer_.processNextRequest();

  if (restartAt_ != 0 && deadlineReached(restartAt_)) {
    Serial.println("Credentials saved, restarting");
    ESP.restart();
  }
  if (restartOnTimeout_ && deadlineReached(startedAt_ + config::kProvisioningTimeoutMs)) {
    Serial.println("Onboarding window expired, retrying the saved network");
    ESP.restart();
  }
  delay(5);
}

esp_err_t ProvisioningPortal::pageHandler(httpd_req_t* request) {
  return sendPage(request, ui::kOnboardingHtmlBeforeAccessPoint,
                  owner(request).accessPointSsid_,
                  ui::kOnboardingHtmlAfterAccessPoint);
}

esp_err_t ProvisioningPortal::scanHandler(httpd_req_t* request) {
  httpd_resp_set_type(request, kJsonContentType);
  httpd_resp_set_hdr(request, "Cache-Control", kNoStore);
  if (httpd_resp_send_chunk(request, "[", 1) != ESP_OK) {
    return ESP_FAIL;
  }

  const int found = WiFi.scanNetworks();
  for (int index = 0; index < found; ++index) {
    char escaped[kEscapeCapacity + 40]{};
    const String ssid = WiFi.SSID(index);
    escapeJsonString(ssid.c_str(), escaped, sizeof(escaped));
    const bool open = WiFi.encryptionType(index) == WIFI_AUTH_OPEN;
    char entry[192];
    snprintf(entry, sizeof(entry),
             "%s{\"ssid\":\"%s\",\"rssi\":%d,\"open\":%s}", index == 0 ? "" : ",",
             escaped, static_cast<int>(WiFi.RSSI(index)),
             open ? "true" : "false");
    if (httpd_resp_send_chunk(request, entry, HTTPD_RESP_USE_STRLEN) !=
        ESP_OK) {
      WiFi.scanDelete();
      return ESP_FAIL;
    }
  }
  WiFi.scanDelete();

  if (httpd_resp_send_chunk(request, "]", 1) != ESP_OK) {
    return ESP_FAIL;
  }
  return httpd_resp_send_chunk(request, nullptr, 0);
}

esp_err_t ProvisioningPortal::saveHandler(httpd_req_t* request) {
  ProvisioningPortal& portal = owner(request);
  const std::size_t length = request->content_len;
  if (length == 0 || length > kBodyCapacity) {
    return sendError(
        request,
        core::provisioning::describeParseStatus(
            core::provisioning::ParseStatus::kBodyTooLong));
  }

  char body[kBodyCapacity + 1]{};
  std::size_t received = 0;
  while (received < length) {
    const int chunk =
        httpd_req_recv(request, body + received, length - received);
    if (chunk <= 0) {
      return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,
                                 "the form could not be read");
    }
    received += static_cast<std::size_t>(chunk);
  }
  body[length] = '\0';

  core::provisioning::Request parsed;
  const core::provisioning::ParseStatus status =
      core::provisioning::parseFormBody(body, length, parsed);
  if (status != core::provisioning::ParseStatus::kOk) {
    return sendError(request, core::provisioning::describeParseStatus(status));
  }

  WifiCredentials credentials;
  std::memcpy(credentials.ssid, parsed.ssid, sizeof(credentials.ssid));
  std::memcpy(credentials.password, parsed.password,
              sizeof(credentials.password));
  credentials.useDhcp = parsed.useDhcp;
  if (!portal.store_.save(credentials)) {
    return sendError(request, "the settings could not be stored, try again");
  }

  char escaped[kEscapeCapacity]{};
  escapeMarkup(parsed.ssid, escaped, sizeof(escaped));
  portal.restartAt_ = millis() + config::kProvisioningRestartDelayMs;
  return sendPage(request, ui::kOnboardingSavedHtmlBefore, escaped,
                  ui::kOnboardingSavedHtmlAfter);
}

esp_err_t ProvisioningPortal::sendError(httpd_req_t* request,
                                        const char* message) {
  httpd_resp_set_status(request, "400 Bad Request");
  return sendPage(request, ui::kOnboardingErrorHtmlBefore, message,
                  ui::kOnboardingErrorHtmlAfter);
}

esp_err_t ProvisioningPortal::redirectHandler(httpd_req_t* request) {
  const IPAddress accessPoint = WiFi.softAPIP();
  char location[40];
  snprintf(location, sizeof(location), "http://%u.%u.%u.%u/", accessPoint[0],
           accessPoint[1], accessPoint[2], accessPoint[3]);
  httpd_resp_set_status(request, "302 Found");
  httpd_resp_set_hdr(request, "Location", location);
  httpd_resp_set_hdr(request, "Cache-Control", kNoStore);
  return httpd_resp_send(request, nullptr, 0);
}

httpd_uri_t ProvisioningPortal::makeUri(const char* path,
                                        esp_err_t (*handler)(httpd_req_t*),
                                        ProvisioningPortal* portal,
                                        httpd_method_t method) {
  httpd_uri_t uri{};
  uri.uri = path;
  uri.method = method;
  uri.handler = handler;
  uri.user_ctx = portal;
  return uri;
}

}  // namespace app::adapters
