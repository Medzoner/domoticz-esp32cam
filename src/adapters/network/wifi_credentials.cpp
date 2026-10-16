#include "adapters/network/wifi_credentials.h"

#include <Preferences.h>
#include <cstring>

#include "secrets.h"

namespace app::adapters {
namespace {

constexpr char kPreferencesNamespace[] = "camsetup";
constexpr char kKeySsid[] = "ssid";
constexpr char kKeyPassword[] = "pass";
constexpr char kKeyUseDhcp[] = "dhcp";
constexpr char kKeyConfigured[] = "configured";

bool copyValue(const char* source, char* destination, std::size_t capacity) {
  const std::size_t length = std::strlen(source);
  if (length >= capacity) {
    return false;
  }
  std::memcpy(destination, source, length + 1);
  return true;
}

}  // namespace

bool WifiCredentialStore::load(WifiCredentials& credentials) {
  credentials = WifiCredentials{};
  if (loadFromStorage(credentials)) {
    return true;
  }
  return loadFromBuild(credentials);
}

bool WifiCredentialStore::loadFromStorage(WifiCredentials& credentials) {
  Preferences preferences;
  if (!preferences.begin(kPreferencesNamespace, true)) {
    return false;
  }
  const bool configured =
      preferences.isKey(kKeyConfigured) && preferences.getBool(kKeyConfigured);
  const String ssid = preferences.getString(kKeySsid, "");
  const String password = preferences.getString(kKeyPassword, "");
  const bool useDhcp = preferences.getBool(kKeyUseDhcp, true);
  preferences.end();

  if (!configured || ssid.isEmpty() || ssid.length() >= sizeof(credentials.ssid) ||
      password.length() >= sizeof(credentials.password)) {
    return false;
  }
  copyValue(ssid.c_str(), credentials.ssid, sizeof(credentials.ssid));
  copyValue(password.c_str(), credentials.password,
            sizeof(credentials.password));
  credentials.useDhcp = useDhcp;
  return true;
}

bool WifiCredentialStore::loadFromBuild(WifiCredentials& credentials) {
  if (core::provisioning::isUnconfigured(WIFI_SSID)) {
    return false;
  }
  if (!copyValue(WIFI_SSID, credentials.ssid, sizeof(credentials.ssid))) {
    return false;
  }
  if (!copyValue(WIFI_PASSWORD, credentials.password,
                 sizeof(credentials.password))) {
    return false;
  }
  // Build-time credentials keep the static addressing defined in secrets.h.
  credentials.useDhcp = false;
  return true;
}

bool WifiCredentialStore::save(const WifiCredentials& credentials) {
  if (!core::provisioning::isValidSsid(credentials.ssid,
                                       std::strlen(credentials.ssid)) ||
      !core::provisioning::isValidPassword(
          credentials.password, std::strlen(credentials.password))) {
    return false;
  }

  Preferences preferences;
  if (!preferences.begin(kPreferencesNamespace, false)) {
    return false;
  }
  bool saved = true;
  if (preferences.putString(kKeySsid, credentials.ssid) == 0) {
    saved = false;
  }
  if (credentials.password[0] == '\0') {
    // An open network has no passphrase: drop any previous secret instead of
    // storing an empty string, which the Preferences API rejects.
    preferences.remove(kKeyPassword);
  } else if (preferences.putString(kKeyPassword, credentials.password) == 0) {
    saved = false;
  }
  if (!preferences.putBool(kKeyUseDhcp, credentials.useDhcp) ||
      !preferences.putBool(kKeyConfigured, true)) {
    saved = false;
  }
  preferences.end();
  return saved;
}

bool WifiCredentialStore::hasStoredCredentials() const {
  Preferences preferences;
  if (!preferences.begin(kPreferencesNamespace, true)) {
    return false;
  }
  const bool configured =
      preferences.isKey(kKeyConfigured) && preferences.getBool(kKeyConfigured);
  preferences.end();
  return configured;
}

}  // namespace app::adapters
