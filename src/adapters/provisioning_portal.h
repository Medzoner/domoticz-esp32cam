#pragma once

#include <DNSServer.h>
#include <esp_http_server.h>

#include "adapters/wifi_credentials.h"
#include "core/provisioning_rules.h"

namespace app::adapters {

// Soft access point with a DNS captive portal and a small HTTP server that
// asks the user for Wi-Fi credentials. Only started when the camera has no
// usable network configuration.
class ProvisioningPortal {
 public:
  // restartOnTimeout: retry station mode when the window expires. False when
  // there is nothing to retry, in which case the portal stays open forever.
  bool begin(bool restartOnTimeout);
  void loop();

 private:
  static ProvisioningPortal& owner(httpd_req_t* request);
  static esp_err_t pageHandler(httpd_req_t* request);
  static esp_err_t scanHandler(httpd_req_t* request);
  static esp_err_t saveHandler(httpd_req_t* request);
  static esp_err_t redirectHandler(httpd_req_t* request);
  static esp_err_t sendError(httpd_req_t* request, const char* message);
  static httpd_uri_t makeUri(const char* path,
                             esp_err_t (*handler)(httpd_req_t*),
                             ProvisioningPortal* portal,
                             httpd_method_t method = HTTP_GET);

  bool startAccessPoint();
  bool startNameService();
  bool startHttpServer();

  DNSServer dnsServer_;
  httpd_handle_t server_ = nullptr;
  WifiCredentialStore store_;
  char accessPointSsid_[core::provisioning::kSsidCapacity]{};
  bool restartOnTimeout_ = false;
  std::uint32_t startedAt_ = 0;
  std::uint32_t restartAt_ = 0;
};

}  // namespace app::adapters
