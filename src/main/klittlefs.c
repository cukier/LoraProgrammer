#include "esp_littlefs.h"
#include "esp_log.h"

#include "include/klittlefs.h"

static const char *TAG = "klittlefs";

esp_err_t klittlefs_init(void) {
  esp_vfs_littlefs_conf_t conf = {
      .base_path = CONFIG_KLITTLEFS_BASE_PATH,
      .partition_label = CONFIG_KLITTLEFS_PARTITION_LABEL,
      .format_if_mount_failed = true,
      .dont_mount = false,
  };

  esp_err_t ret = esp_vfs_littlefs_register(&conf);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "failed to mount LittleFS on %s: %s",
             CONFIG_KLITTLEFS_BASE_PATH, esp_err_to_name(ret));
    return ret;
  }

  size_t total = 0, used = 0;
  esp_littlefs_info(CONFIG_KLITTLEFS_PARTITION_LABEL, &total, &used);
  ESP_LOGI(TAG, "LittleFS mounted on %s (%u/%u KB used)",
           CONFIG_KLITTLEFS_BASE_PATH, (unsigned)(used / 1024),
           (unsigned)(total / 1024));

  return ESP_OK;
}
