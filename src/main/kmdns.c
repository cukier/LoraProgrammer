#include "esp_log.h"
#include "mdns.h"

#include "include/kmdns.h"

static const char *TAG = "kmdns";

esp_err_t kmdns_init(void) {
  ESP_ERROR_CHECK(mdns_init());
  ESP_ERROR_CHECK(mdns_hostname_set(CONFIG_KMDNS_HOSTNAME));
  ESP_ERROR_CHECK(mdns_instance_name_set(CONFIG_KMDNS_INSTANCE_NAME));

  mdns_txt_item_t service_txt[] = {
      {"chip", CONFIG_IDF_TARGET},
      {"path", "/"},
  };

  ESP_ERROR_CHECK(mdns_service_add(
      NULL, "_http", "_tcp", CONFIG_KREST_SERVER_PORT, service_txt,
      sizeof(service_txt) / sizeof(service_txt[0])));

  ESP_LOGI(TAG, "mDNS ready: http://%s.local/", CONFIG_KMDNS_HOSTNAME);
  return ESP_OK;
}
