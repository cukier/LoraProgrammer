#include <stdio.h>
#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"

#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_sntp.h"
#include "esp_timer.h"

#include "include/kdatetime.h"

static const char *TAG = "kdatetime";
static bool s_synced = false;

esp_err_t kdatetime_init(void) {
  setenv("TZ", CONFIG_KDATETIME_TZ, 1);
  tzset();

  esp_sntp_config_t config =
      ESP_NETIF_SNTP_DEFAULT_CONFIG(CONFIG_KDATETIME_NTP_SERVER);
  ESP_ERROR_CHECK(esp_netif_sntp_init(&config));

  esp_err_t ret = esp_netif_sntp_sync_wait(
      pdMS_TO_TICKS(CONFIG_KDATETIME_SYNC_TIMEOUT_SEC * 1000));
  if (ret != ESP_OK) {
    ESP_LOGW(TAG,
             "NTP sync did not complete within %d s, will keep retrying in "
             "the background",
             CONFIG_KDATETIME_SYNC_TIMEOUT_SEC);
    return ret;
  }

  s_synced = true;
  ESP_LOGI(TAG, "time synced from %s", CONFIG_KDATETIME_NTP_SERVER);
  return ESP_OK;
}

bool kdatetime_is_synced(void) {
  if (!s_synced) {
    s_synced = sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED;
  }
  return s_synced;
}

int64_t kdatetime_uptime_seconds(void) {
  return esp_timer_get_time() / 1000000;
}

void kdatetime_uptime_str(char *buf, size_t len) {
  int64_t s = kdatetime_uptime_seconds();
  int days = (int)(s / 86400);
  int hours = (int)((s % 86400) / 3600);
  int minutes = (int)((s % 3600) / 60);
  int seconds = (int)(s % 60);
  snprintf(buf, len, "%dd %02dh %02dm %02ds", days, hours, minutes, seconds);
}

esp_err_t kdatetime_local_iso8601(char *buf, size_t len) {
  if (!kdatetime_is_synced()) {
    snprintf(buf, len, "unsynced");
    return ESP_ERR_INVALID_STATE;
  }

  time_t now = 0;
  struct tm timeinfo = {0};
  time(&now);
  localtime_r(&now, &timeinfo);
  strftime(buf, len, "%Y-%m-%d %H:%M:%S", &timeinfo);
  return ESP_OK;
}
