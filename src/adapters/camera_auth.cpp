#include "adapters/camera_auth.h"

#include <Arduino.h>

#include <cstdio>

#include "core/basic_auth.h"
#include "secrets.h"

#ifndef CAMERA_AUTH_USERNAME
#define CAMERA_AUTH_USERNAME ""
#endif

#ifndef CAMERA_AUTH_PASSWORD
#define CAMERA_AUTH_PASSWORD ""
#endif

namespace app::adapters {

bool CameraAuth::begin() {
  if (!core::auth::isConfigurationValid(CAMERA_AUTH_USERNAME,
                                         CAMERA_AUTH_PASSWORD)) {
    return false;
  }
  enabled_ = core::auth::isEnabled(CAMERA_AUTH_USERNAME, CAMERA_AUTH_PASSWORD);
  if (!enabled_) {
    return true;
  }
  for (std::size_t offset = 0; offset < sizeof(sessionToken_) - 1; offset += 8) {
    snprintf(sessionToken_ + offset, sizeof(sessionToken_) - offset, "%08lx",
             static_cast<unsigned long>(esp_random()));
  }
  snprintf(sessionCookie_, sizeof(sessionCookie_),
           "camera_session=%s; Path=/; HttpOnly; SameSite=Strict",
           sessionToken_);
  return true;
}

bool CameraAuth::enabled() const { return enabled_; }

ports::AuthenticationDecision CameraAuth::authorize(
    const char* authorization, const char* cookie) const {
  if (!enabled_) {
    return {true, false};
  }
  if (core::auth::hasSessionCookie(cookie, sessionToken_)) {
    return {true, false};
  }
  if (core::auth::isBasicAuthorizationValid(
          authorization, CAMERA_AUTH_USERNAME, CAMERA_AUTH_PASSWORD)) {
    return {true, true};
  }
  return {false, false};
}

const char* CameraAuth::sessionCookie() const { return sessionCookie_; }

}  // namespace app::adapters
