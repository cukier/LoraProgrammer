#include <stdio.h>
#include <stdlib.h>

#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "cJSON.h"

#include "include/kdatetime.h"
#include "include/kota.h"
#include "include/krest.h"
#include "include/kuart.h"
#include "include/kwifi.h"

static const char *TAG = "krest";

static const char *OTA_UPLOAD_PAGE =
    "<!DOCTYPE html><html><body>"
    "<h3>Lora programmer firmware update</h3>"
    "<input type='file' id='fw' onchange='u(this.files[0])'>"
    "<script>"
    "function u(f){var r=new XMLHttpRequest();r.open('POST','/ota');"
    "r.onload=function(){alert(r.responseText);};r.send(f);}"
    "</script></body></html>";

static esp_timer_handle_t s_reboot_timer;

static esp_err_t index_get_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_sendstr(req, OTA_UPLOAD_PAGE);
}

static esp_err_t info_get_handler(httpd_req_t *req) {
  const esp_app_desc_t *app_desc = esp_app_get_description();

  char ip[16];
  kwifi_get_ip_str(ip, sizeof(ip));

  char uptime_str[32];
  kdatetime_uptime_str(uptime_str, sizeof(uptime_str));

  char local_time[32];
  kdatetime_local_iso8601(local_time, sizeof(local_time));

  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "target", CONFIG_IDF_TARGET);
  cJSON_AddStringToObject(root, "idf_version", IDF_VER);
  cJSON_AddStringToObject(root, "firmware_version", app_desc->version);
  cJSON_AddNumberToObject(root, "free_heap_bytes",
                          (double)heap_caps_get_free_size(MALLOC_CAP_DEFAULT));
  cJSON_AddNumberToObject(
      root, "min_free_heap_bytes",
      (double)heap_caps_get_minimum_free_size(MALLOC_CAP_DEFAULT));
  cJSON_AddStringToObject(root, "ip_address", ip);
  int8_t wifi_rssi;
  if (kwifi_get_rssi(&wifi_rssi) == ESP_OK) {
    cJSON_AddNumberToObject(root, "wifi_rssi_dbm", wifi_rssi);
  } else {
    cJSON_AddNullToObject(root, "wifi_rssi_dbm");
  }
  cJSON_AddNumberToObject(root, "uptime_seconds",
                          (double)kdatetime_uptime_seconds());
  cJSON_AddStringToObject(root, "uptime", uptime_str);
  cJSON_AddBoolToObject(root, "time_synced", kdatetime_is_synced());
  cJSON_AddStringToObject(root, "local_time", local_time);

  char *json = cJSON_PrintUnformatted(root);

  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, json);

  free(json);
  cJSON_Delete(root);

  return ESP_OK;
}

/* Fires 5s after the response goes out so the client actually gets the
 * "rebooting" reply before the connection drops. */
static void reboot_timer_callback(void *arg) {
  ESP_LOGW(TAG, "rebooting now (requested via POST /reboot)");
  esp_restart();
}

static esp_err_t reboot_post_handler(httpd_req_t *req) {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "message", "rebooting in 5s");
  char *json = cJSON_PrintUnformatted(root);

  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, json);

  free(json);
  cJSON_Delete(root);

  ESP_ERROR_CHECK(esp_timer_start_once(s_reboot_timer, 5 * 1000000));

  return ESP_OK;
}

esp_err_t krest_init(void) {
  const esp_timer_create_args_t reboot_timer_args = {
      .callback = &reboot_timer_callback,
      .name = "reboot_timer",
  };
  ESP_ERROR_CHECK(esp_timer_create(&reboot_timer_args, &s_reboot_timer));

  httpd_handle_t server = NULL;
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = CONFIG_KREST_SERVER_PORT;
  config.stack_size = 8192;
  config.uri_match_fn = httpd_uri_match_wildcard;
  config.max_uri_handlers = 16;

  esp_err_t err = httpd_start(&server, &config);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
    return err;
  }

  httpd_uri_t index_uri = {.uri = "/",
                           .method = HTTP_GET,
                           .handler = index_get_handler,
                           .user_ctx = NULL};
  httpd_uri_t info_uri = {.uri = "/info",
                          .method = HTTP_GET,
                          .handler = info_get_handler,
                          .user_ctx = NULL};
  httpd_uri_t reboot_uri = {.uri = "/reboot",
                            .method = HTTP_POST,
                            .handler = reboot_post_handler,
                            .user_ctx = NULL};
  httpd_uri_t lora_get_uri = {.uri = "/lora",
                           .method = HTTP_GET,
                           .handler = lora_get_handler,
                           .user_ctx = NULL};

  ESP_ERROR_CHECK(httpd_register_uri_handler(server, &index_uri));
  ESP_ERROR_CHECK(httpd_register_uri_handler(server, &info_uri));
  ESP_ERROR_CHECK(httpd_register_uri_handler(server, &reboot_uri));
  ESP_ERROR_CHECK(httpd_register_uri_handler(server, &lora_get_uri));
  ESP_ERROR_CHECK(kota_register(server));

  ESP_LOGI(TAG, "REST server started on port %d", CONFIG_KREST_SERVER_PORT);

  return ESP_OK;
}
