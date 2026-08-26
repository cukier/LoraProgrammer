#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"

#include "include/kwifi.h"

#define KWIFI_CONNECTED_BIT BIT0
#define KWIFI_FAIL_BIT BIT1

static const char *TAG = "kwifi";

#if CONFIG_KWIFI_MODE_STA
static EventGroupHandle_t s_event_group;
static int s_retry_num = 0;
#endif
static bool s_connected = false;
static char s_ip_addr[16] = "0.0.0.0";

static void kwifi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data) {
#if CONFIG_KWIFI_MODE_STA
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED) {
    s_connected = false;
    if (s_retry_num < CONFIG_KWIFI_STA_MAXIMUM_RETRY) {
      esp_wifi_connect();
      s_retry_num++;
      ESP_LOGW(TAG, "retrying connection (%d/%d)", s_retry_num,
               CONFIG_KWIFI_STA_MAXIMUM_RETRY);
    } else {
      xEventGroupSetBits(s_event_group, KWIFI_FAIL_BIT);
    }
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    snprintf(s_ip_addr, sizeof(s_ip_addr), IPSTR, IP2STR(&event->ip_info.ip));
    s_retry_num = 0;
    s_connected = true;
    ESP_LOGI(TAG, "got ip: %s", s_ip_addr);
    xEventGroupSetBits(s_event_group, KWIFI_CONNECTED_BIT);
  }
#else
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
    wifi_event_ap_staconnected_t *event =
        (wifi_event_ap_staconnected_t *)event_data;
    ESP_LOGI(TAG, "station " MACSTR " joined, AID=%d", MAC2STR(event->mac),
             event->aid);
  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_AP_STADISCONNECTED) {
    wifi_event_ap_stadisconnected_t *event =
        (wifi_event_ap_stadisconnected_t *)event_data;
    ESP_LOGI(TAG, "station " MACSTR " left, AID=%d", MAC2STR(event->mac),
             event->aid);
  }
#endif
}

#if CONFIG_KWIFI_MODE_STA
static esp_err_t kwifi_init_sta(void) {
  s_event_group = xEventGroupCreate();

  esp_netif_create_default_wifi_sta();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, &kwifi_event_handler, NULL, NULL));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      IP_EVENT, IP_EVENT_STA_GOT_IP, &kwifi_event_handler, NULL, NULL));

  wifi_config_t wifi_config = {0};
  strlcpy((char *)wifi_config.sta.ssid, CONFIG_KWIFI_SSID,
          sizeof(wifi_config.sta.ssid));
  strlcpy((char *)wifi_config.sta.password, CONFIG_KWIFI_PASSWORD,
          sizeof(wifi_config.sta.password));
  wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());

  ESP_LOGI(TAG, "connecting to SSID:%s", CONFIG_KWIFI_SSID);

  EventBits_t bits =
      xEventGroupWaitBits(s_event_group, KWIFI_CONNECTED_BIT | KWIFI_FAIL_BIT,
                          pdFALSE, pdFALSE, portMAX_DELAY);

  if (bits & KWIFI_CONNECTED_BIT) {
    return ESP_OK;
  }

  ESP_LOGE(TAG, "failed to connect to SSID:%s", CONFIG_KWIFI_SSID);
  return ESP_FAIL;
}
#else
static esp_err_t kwifi_init_ap(void) {
  esp_netif_create_default_wifi_ap();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, &kwifi_event_handler, NULL, NULL));

  wifi_config_t wifi_config = {
      .ap =
          {
              .channel = CONFIG_KWIFI_AP_CHANNEL,
              .max_connection = CONFIG_KWIFI_AP_MAX_CONN,
              .authmode = WIFI_AUTH_WPA2_PSK,
              .pmf_cfg = {.required = false},
          },
  };
  strlcpy((char *)wifi_config.ap.ssid, CONFIG_KWIFI_SSID,
          sizeof(wifi_config.ap.ssid));
  wifi_config.ap.ssid_len = strlen(CONFIG_KWIFI_SSID);
  strlcpy((char *)wifi_config.ap.password, CONFIG_KWIFI_PASSWORD,
          sizeof(wifi_config.ap.password));

  if (strlen(CONFIG_KWIFI_PASSWORD) == 0) {
    wifi_config.ap.authmode = WIFI_AUTH_OPEN;
  }

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());

  strlcpy(s_ip_addr, "192.168.4.1", sizeof(s_ip_addr));
  s_connected = true;

  ESP_LOGI(TAG, "AP started, SSID:%s IP:%s", CONFIG_KWIFI_SSID, s_ip_addr);

  return ESP_OK;
}
#endif // CONFIG_KWIFI_MODE_STA

esp_err_t kwifi_init(void) {
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());

#if CONFIG_KWIFI_MODE_STA
  return kwifi_init_sta();
#else
  return kwifi_init_ap();
#endif
}

bool kwifi_is_connected(void) { return s_connected; }

void kwifi_get_ip_str(char *buf, size_t len) {
  strlcpy(buf, s_ip_addr, len);
}

esp_err_t kwifi_get_rssi(int8_t *out_rssi) {
#if CONFIG_KWIFI_MODE_STA
  if (!s_connected) {
    return ESP_ERR_INVALID_STATE;
  }
  wifi_ap_record_t ap_info;
  esp_err_t err = esp_wifi_sta_get_ap_info(&ap_info);
  if (err != ESP_OK) {
    return err;
  }
  *out_rssi = ap_info.rssi;
  return ESP_OK;
#else
  (void)out_rssi;
  return ESP_ERR_NOT_SUPPORTED;
#endif
}
