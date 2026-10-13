#pragma once

#include <cstddef>

#include "core/provisioning_rules.h"

namespace app::adapters {

struct WifiCredentials {
  char ssid[core::provisioning::kSsidCapacity]{};
  char password[core::provisioning::kPasswordCapacity]{};
  bool useDhcp = false;
};

// Reads credentials from non-volatile storage and falls back to the values
// compiled from include/secrets.h when nothing has been provisioned yet.
class WifiCredentialStore {
 public:
  bool load(WifiCredentials& credentials);
  bool save(const WifiCredentials& credentials);

  // True when credentials exist in non-volatile storage rather than being
  // inherited from the firmware build.
  bool hasStoredCredentials() const;

 private:
  bool loadFromStorage(WifiCredentials& credentials);
  bool loadFromBuild(WifiCredentials& credentials);
};

}  // namespace app::adapters
