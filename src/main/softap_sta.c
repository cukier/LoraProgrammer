/*
 * softap_sta.c
 *
 *  Created on: Ago 27, 2025
 *      Author: cukier
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_check.h"
#include "esp_chip_info.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_vfs.h"
#include "esp_wifi.h"

#include "cJSON.h"
#include "lwip/apps/netbiosns.h"
#include "mdns.h"

#include "loraprogrammer.h"


#define FILE_PATH_MAX (ESP_VFS_PATH_MAX + 128)
#define SCRATCH_BUFSIZE (10240)

// #define RAW_RADIO_DATA

typedef struct rest_server_context {
  char base_path[ESP_VFS_PATH_MAX + 1];
  char scratch[SCRATCH_BUFSIZE];
} rest_server_context_t;

static const char *TAG = "softap_sta";

static loraprogrammer_t *loraprogrammer = NULL;
static SemaphoreHandle_t semphr = NULL;

static void k_wifi_event_handler(void *arg, esp_event_base_t event_base,
                                 int32_t event_id, void *event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED) {
    esp_wifi_connect();
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
  }
}

static void initialise_mdns(void) {
  mdns_init();
  mdns_hostname_set(CONFIG_KMDNS_HOSTNAME);
  mdns_instance_name_set(CONFIG_KMDNS_INSTANCE_NAME);

  mdns_txt_item_t serviceTxtData[] = {{"chip", CONFIG_IDF_TARGET},
                                      {"path", "/"}};

  ESP_ERROR_CHECK(
      mdns_service_add("ESP32-WebServer", "_http", "_tcp", 80, serviceTxtData,
                       sizeof(serviceTxtData) / sizeof(serviceTxtData[0])));
}

static esp_err_t system_info_get_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "application/json");

  cJSON *root = cJSON_CreateObject();
  esp_chip_info_t chip_info;
  size_t free_heap_size = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);

  esp_chip_info(&chip_info);
  cJSON_AddStringToObject(root, "chip", CONFIG_IDF_TARGET);
  cJSON_AddStringToObject(root, "idf_version", IDF_VER);
  cJSON_AddNumberToObject(root, "cores", chip_info.cores);
  cJSON_AddNumberToObject(root, "app version", CONFIG_BOOTLOADER_PROJECT_VER);
  cJSON_AddNumberToObject(root, "heap size(bytes)", free_heap_size);
  cJSON_AddNumberToObject(root, "TSB(hrs)",
                          (double)esp_log_timestamp() / 3600000.0f);

  radio_data_t radio = {0};

  if (k_take_semphr(semphr)) {
    memcpy(&radio, loraprogrammer->radio, sizeof(radio_data_t));
    cJSON_AddStringToObject(root, "Last message", loraprogrammer->lastMensage);
    cJSON_AddNumberToObject(root, "RSSI (dBm)", loraprogrammer->rssi);
    xSemaphoreGive(semphr);
  }

#ifdef RAW_RADIO_DATA
  cJSON *radioJ = cJSON_CreateObject();

  cJSON_AddNumberToObject(radioJ, "Baudrate", radio.baudrate);
  cJSON_AddNumberToObject(radioJ, "Parity", radio.parity);
  cJSON_AddNumberToObject(radioJ, "Frequency", radio.frequency);
  cJSON_AddNumberToObject(radioJ, "Factor", radio.rf_factor);
  cJSON_AddNumberToObject(radioJ, "Mode", radio.mode);
  cJSON_AddNumberToObject(radioJ, "BW", radio.rf_bw);
  cJSON_AddNumberToObject(radioJ, "ID", radio.id);
  cJSON_AddNumberToObject(radioJ, "NetID", radio.net_id);
  cJSON_AddNumberToObject(radioJ, "Power", radio.rf_power);
  cJSON_AddItemToObject(root, "radio", radioJ);
#else
  char *parse = RF1276_toString(&radio);

  if (parse != NULL) {
    cJSON_AddStringToObject(root, "radio", parse);
    free(parse);
  }
#endif

  const char *sys_info = cJSON_Print(root);

  httpd_resp_sendstr(req, sys_info);
  free((void *)sys_info);
  cJSON_Delete(root);

  return ESP_OK;
}

static esp_err_t config_post_handler(httpd_req_t *req) {
  int total_len = req->content_len;
  int cur_len = 0;
  char *buf = ((rest_server_context_t *)(req->user_ctx))->scratch;
  int received = 0;
  if (total_len >= SCRATCH_BUFSIZE) {
    /* Respond with 500 Internal Server Error */
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                        "content too long");
    return ESP_FAIL;
  }
  while (cur_len < total_len) {
    received = httpd_req_recv(req, buf + cur_len, total_len);
    if (received <= 0) {
      /* Respond with 500 Internal Server Error */
      httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                          "Failed to post control value");
      return ESP_FAIL;
    }
    cur_len += received;
  }
  buf[total_len] = '\0';

  ESP_LOGI(TAG, "buf %s", buf);
  ESP_LOGI(TAG, "total_len %d", total_len);
  ESP_LOGI(TAG, "base_path %s",
           ((rest_server_context_t *)(req->user_ctx))->base_path);

  cJSON *root = cJSON_Parse(buf);

  if (root == NULL)
    return ESP_FAIL;

  cJSON *baud = cJSON_GetObjectItem(root, "Baudrate");
  cJSON *parity = cJSON_GetObjectItem(root, "Parity");
  cJSON *freq = cJSON_GetObjectItem(root, "Frequency");
  cJSON *factor = cJSON_GetObjectItem(root, "Factor");
  cJSON *mode = cJSON_GetObjectItem(root, "Mode");
  cJSON *bw = cJSON_GetObjectItem(root, "BW");
  cJSON *id = cJSON_GetObjectItem(root, "ID");
  cJSON *net_id = cJSON_GetObjectItem(root, "NetID");
  cJSON *pwr = cJSON_GetObjectItem(root, "Power");

  if ((baud == NULL) || (parity == NULL) || (freq == NULL) ||
      (factor == NULL) || (mode == NULL) || (bw == NULL) || (id == NULL) ||
      (net_id == NULL) || (pwr == NULL)) {
    char resp[250] = {0};

    cJSON_free(root);
    sprintf(resp, "Post control value wrong");
    httpd_resp_sendstr(req, resp);
    return ESP_FAIL;
  }

  if (k_take_semphr(semphr)) {
    loraprogrammer->radio->baudrate = cJSON_GetNumberValue(baud);
    loraprogrammer->radio->frequency = cJSON_GetNumberValue(freq);
    loraprogrammer->radio->id = cJSON_GetNumberValue(id);
    loraprogrammer->radio->mode = cJSON_GetNumberValue(mode);
    loraprogrammer->radio->net_id = cJSON_GetNumberValue(net_id);
    loraprogrammer->radio->parity = cJSON_GetNumberValue(parity);
    loraprogrammer->radio->rf_bw = cJSON_GetNumberValue(bw);
    loraprogrammer->radio->rf_factor = cJSON_GetNumberValue(factor);
    loraprogrammer->radio->rf_power = cJSON_GetNumberValue(pwr);
    loraprogrammer->update = 1;
    xSemaphoreGive(semphr);
  }

  cJSON_free(root);
  httpd_resp_sendstr(req, "Post control value successfully");

  return ESP_OK;
}

esp_err_t start_rest_server(const char *base_path) {
  esp_err_t ret = ESP_OK;

  ESP_RETURN_ON_FALSE(base_path && strlen(base_path) < ESP_VFS_PATH_MAX,
                      ESP_ERR_INVALID_ARG, TAG, "Invalid base path");
  rest_server_context_t *rest_context =
      calloc(1, sizeof(rest_server_context_t));
  ESP_RETURN_ON_FALSE(rest_context, ESP_ERR_NO_MEM, TAG,
                      "No memory for rest context");
  strlcpy(rest_context->base_path, base_path, sizeof(rest_context->base_path));

  httpd_handle_t server = NULL;
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();

  config.server_port = CONFIG_KREST_SERVER_PORT;
  config.uri_match_fn = httpd_uri_match_wildcard;

  ESP_LOGI(TAG, "Starting HTTP Server");
  ESP_GOTO_ON_ERROR(httpd_start(&server, &config), err, TAG,
                    "Failed to start http server");

  httpd_uri_t system_info_get_uri = {.uri = "/",
                                     .method = HTTP_GET,
                                     .handler = system_info_get_handler,
                                     .user_ctx = rest_context};
  httpd_register_uri_handler(server, &system_info_get_uri);

  httpd_uri_t config_post_uri = {.uri = "/",
                                 .method = HTTP_POST,
                                 .handler = config_post_handler,
                                 .user_ctx = rest_context};
  httpd_register_uri_handler(server, &config_post_uri);

  return ESP_OK;
err:
  if (rest_context) {
    free(rest_context);
  }
  return ret;
}

void initialise_wifi() {
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  esp_event_handler_instance_t instance_any_id;
  esp_event_handler_instance_t instance_got_ip;
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, &k_wifi_event_handler, NULL,
      &instance_any_id));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      IP_EVENT, IP_EVENT_STA_GOT_IP, &k_wifi_event_handler, NULL,
      &instance_got_ip));

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));

  wifi_config_t wifi_config = {
      .ap =
          {
              .ssid = CONFIG_KWIFI_SSID,
              .ssid_len = strlen(CONFIG_KWIFI_SSID),
              .channel = CONFIG_KWIFI_AP_CHANNEL,
              .password = CONFIG_KWIFI_PASSWORD,
              .max_connection = CONFIG_KWIFI_AP_MAX_CONN,
              .authmode = WIFI_AUTH_WPA2_PSK,
              .pmf_cfg =
                  {
                      .required = false,
                  },
          },
  };
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
}

esp_err_t softap_sta_init(void *pvParameters) {
  loraprogrammer = (loraprogrammer_t *)pvParameters;
  semphr = loraprogrammer->semphr;

  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  initialise_mdns();
  netbiosns_init();
  netbiosns_set_name(CONFIG_KMDNS_HOSTNAME);
  esp_netif_create_default_wifi_ap();
  initialise_wifi();

  ESP_ERROR_CHECK(esp_wifi_start());
  ESP_ERROR_CHECK(start_rest_server("/www"));

  ESP_LOGI(TAG, "Init...");

  return ESP_OK;
}