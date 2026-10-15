#include "adapters/http/http_middleware.h"

#include <Arduino.h>

#include "adapters/http/http_request.h"
#include "adapters/http/http_response.h"
#include "config/app_config.h"
#include "core/http_security.h"

namespace app::adapters::http {
namespace {

constexpr std::size_t kHeaderCapacity = 192;
constexpr char kStatusForbidden[] = "403 Forbidden";
constexpr char kStatusTooManyRequests[] = "429 Too Many Requests";
constexpr char kMessageCrossOrigin[] = "cross-origin request rejected";
constexpr char kMessageTooManyRequests[] = "too many mutations, slow down";

const char* methodName(int method) {
  switch (method) {
    case HTTP_GET:
      return "GET";
    case HTTP_POST:
      return "POST";
    case HTTP_DELETE:
      return "DELETE";
    default:
      return "OTHER";
  }
}

}  // namespace

HttpMiddleware::HttpMiddleware(ports::AuthenticationPort& authentication)
    : authentication_(authentication) {}

bool HttpMiddleware::handle(httpd_req_t* httpRequest, AccessPolicy policy) {
  log(httpRequest);
  return authenticate(httpRequest) &&
         (policy == AccessPolicy::kRead || acceptMutation(httpRequest));
}

bool HttpMiddleware::authenticate(httpd_req_t* httpRequest) {
  if (!authentication_.enabled()) {
    return true;
  }
  char authorization[kHeaderCapacity]{};
  char cookie[kHeaderCapacity]{};
  request::copyHeader(httpRequest, "Authorization", authorization,
                      sizeof(authorization));
  request::copyHeader(httpRequest, "Cookie", cookie, sizeof(cookie));
  const ports::AuthenticationDecision decision = authentication_.authorize(
      authorization[0] == '\0' ? nullptr : authorization,
      cookie[0] == '\0' ? nullptr : cookie);
  if (!decision.granted) {
    response::unauthorized(httpRequest);
    return false;
  }
  if (decision.issueSession) {
    httpd_resp_set_hdr(httpRequest, "Set-Cookie",
                       authentication_.sessionCookie());
  }
  return true;
}

bool HttpMiddleware::acceptMutation(httpd_req_t* httpRequest) {
  char origin[kHeaderCapacity]{};
  char referer[kHeaderCapacity]{};
  char host[kHeaderCapacity]{};
  const bool hasOrigin =
      request::copyHeader(httpRequest, "Origin", origin, sizeof(origin)) > 0;
  const bool hasReferer =
      request::copyHeader(httpRequest, "Referer", referer, sizeof(referer)) > 0;
  const bool hasHost =
      request::copyHeader(httpRequest, "Host", host, sizeof(host)) > 0;
  if (!hasHost ||
      !core::security::isSameOrigin(hasOrigin ? origin : nullptr,
                                    hasReferer ? referer : nullptr, host)) {
    response::error(httpRequest, kStatusForbidden, kMessageCrossOrigin);
    return false;
  }

  const uint32_t now = millis();
  if (lastMutationAt_ != 0 &&
      now - lastMutationAt_ < config::kControlMinIntervalMs) {
    response::error(httpRequest, kStatusTooManyRequests,
                    kMessageTooManyRequests);
    return false;
  }
  lastMutationAt_ = now;
  return true;
}

void HttpMiddleware::log(httpd_req_t* httpRequest) {
  if constexpr (config::kLogHttpRequests) {
    Serial.printf("[http] %s %s\n", methodName(httpRequest->method),
                  httpRequest->uri);
  }
}

}  // namespace app::adapters::http
