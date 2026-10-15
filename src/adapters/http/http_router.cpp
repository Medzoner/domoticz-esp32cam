#include "adapters/http/http_router.h"

#include "config/app_config.h"

namespace app::adapters::http {

bool HttpRouter::begin(void* context, const Route* routes,
                       std::size_t routeCount) {
  if (context == nullptr || routes == nullptr || routeCount == 0 ||
      routeCount > config::kMaxControlRoutes) {
    return false;
  }
  httpd_config_t serverConfig = HTTPD_DEFAULT_CONFIG();
  serverConfig.server_port = config::kControlPort;
  serverConfig.max_open_sockets = config::kMaxControlClients;
  serverConfig.max_uri_handlers = config::kMaxControlRoutes;
  serverConfig.lru_purge_enable = true;
  serverConfig.recv_wait_timeout = 5;
  serverConfig.send_wait_timeout = 5;
  if (httpd_start(&server_, &serverConfig) != ESP_OK) {
    return false;
  }

  for (std::size_t index = 0; index < routeCount; ++index) {
    httpd_uri_t uri{};
    uri.uri = routes[index].path;
    uri.method = routes[index].method;
    uri.handler = routes[index].handler;
    uri.user_ctx = context;
    if (httpd_register_uri_handler(server_, &uri) != ESP_OK) {
      httpd_stop(server_);
      server_ = nullptr;
      return false;
    }
  }
  return true;
}

}  // namespace app::adapters::http
