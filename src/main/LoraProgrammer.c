#include "esp_log.h"
#include "nvs_flash.h"

#include "include/kdatetime.h"
#include "include/klittlefs.h"
#include "include/kmdns.h"
#include "include/kota.h"
#include "include/krest.h"
#include "include/kuart.h"
#include "include/kwifi.h"

static const char *TAG = "LoraProgrammer";

void app_main(void) {
  esp_err_t ret = nvs_flash_init();

  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }

  ESP_ERROR_CHECK(ret);
  ESP_ERROR_CHECK(k_uart_init());
  kota_mark_valid();
  ESP_ERROR_CHECK(klittlefs_init());
  ESP_ERROR_CHECK(kwifi_init());
  kmdns_init();
#if CONFIG_KWIFI_MODE_STA
  kdatetime_init();
#endif
  krest_init();

  ESP_LOGI(TAG, "ready...");
}
