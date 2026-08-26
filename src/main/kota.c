#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_ota_ops.h"

#include "include/kota.h"

static const char *TAG = "kota";

void kota_mark_valid(void) {
  const esp_partition_t *running = esp_ota_get_running_partition();
  esp_ota_img_states_t state;

  if (esp_ota_get_state_partition(running, &state) != ESP_OK) {
    return;
  }

  if (state == ESP_OTA_IMG_PENDING_VERIFY) {
    esp_ota_mark_app_valid_cancel_rollback();
    ESP_LOGI(TAG, "app marked valid, rollback cancelled");
  }
}

static esp_err_t ota_update_post_handler(httpd_req_t *req) {
  const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
  if (update_partition == NULL) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                        "no OTA partition available");
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "starting OTA to partition '%s', %d bytes incoming",
           update_partition->label, req->content_len);

  esp_ota_handle_t ota_handle = 0;
  esp_err_t err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &ota_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                        "esp_ota_begin failed");
    return ESP_FAIL;
  }

  char buf[1024];
  int remaining = req->content_len;

  while (remaining > 0) {
    int to_read = remaining < (int)sizeof(buf) ? remaining : (int)sizeof(buf);
    int received = httpd_req_recv(req, buf, to_read);

    if (received == HTTPD_SOCK_ERR_TIMEOUT) {
      continue;
    }

    if (received <= 0) {
      ESP_LOGE(TAG, "OTA recv failed");
      esp_ota_abort(ota_handle);
      httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "recv failed");
      return ESP_FAIL;
    }

    err = esp_ota_write(ota_handle, buf, received);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
      esp_ota_abort(ota_handle);
      httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                          "esp_ota_write failed");
      return ESP_FAIL;
    }

    remaining -= received;
  }

  err = esp_ota_end(ota_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                        err == ESP_ERR_OTA_VALIDATE_FAILED
                            ? "image validation failed"
                            : "esp_ota_end failed");
    return ESP_FAIL;
  }

  err = esp_ota_set_boot_partition(update_partition);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s",
             esp_err_to_name(err));
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                        "set_boot_partition failed");
    return ESP_FAIL;
  }

  httpd_resp_sendstr(req, "OTA update OK, rebooting\n");

  ESP_LOGI(TAG, "OTA complete, rebooting into '%s'", update_partition->label);
  vTaskDelay(pdMS_TO_TICKS(500));
  esp_restart();

  return ESP_OK;
}

esp_err_t kota_register(httpd_handle_t server) {
  httpd_uri_t ota_uri = {
      .uri = "/ota",
      .method = HTTP_POST,
      .handler = ota_update_post_handler,
      .user_ctx = NULL,
  };

  return httpd_register_uri_handler(server, &ota_uri);
}
