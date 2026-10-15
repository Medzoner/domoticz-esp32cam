#pragma once

#include <cstddef>

namespace app::core::auth {

bool isConfigurationValid(const char* username, const char* password);
bool isEnabled(const char* username, const char* password);
std::size_t formatBasicAuthorization(char* destination, std::size_t capacity,
                                     const char* username,
                                     const char* password);
bool isBasicAuthorizationValid(const char* authorization,
                               const char* username, const char* password);
bool hasSessionCookie(const char* cookieHeader, const char* sessionToken);
void captureHeader(const char* line, char* authorization,
                   std::size_t authorizationCapacity, char* cookie,
                   std::size_t cookieCapacity);

}  // namespace app::core::auth
