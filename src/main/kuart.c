#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/uart.h"

#include "esp_http_server.h"
#include "esp_log.h"

#include "RF1276.h"
#include "loraprogrammer.h"

#include "cJSON.h"

#include <string.h>

#define BUF_SIZE (1024)
#define PATTERN_CHR_NUM (3)

static const gpio_num_t tx2_pin = GPIO_NUM_25;
static const gpio_num_t tx_pin = GPIO_NUM_26;
static const gpio_num_t rx_pin = GPIO_NUM_14;
static const gpio_num_t en_pin = GPIO_NUM_27;
static const uart_port_t uart_port = UART_NUM_1;
static const char *TAG = "kauart";

static QueueHandle_t uart0_queue = NULL;
static SemaphoreHandle_t uart_semphr = NULL;
static radio_data_t radio = {0};
static char buffer_rx[BUF_SIZE] = {0};
static size_t buffer_rx_len = 0;

static void enRadio() {
  gpio_set_level(en_pin, 1);
  ESP_LOGI(TAG, "Ligado");
}

static void disableRaidio() {
  gpio_set_level(en_pin, 0);
  ESP_LOGI(TAG, "Desligado");
}

static esp_err_t change_uart(uint32_t new_baudrate, char new_parity,
                             int new_data, int new_stop) {
  esp_err_t err = ESP_ERR_NOT_SUPPORTED;

  uart_wait_tx_done(uart_port, 100 / portTICK_PERIOD_MS);
  err = uart_set_baudrate(uart_port, new_baudrate);

  if (err != ESP_OK)
    return err;

  if ((new_parity == 'n') || (new_parity == 'N'))
    err = uart_set_parity(uart_port, UART_PARITY_DISABLE);
  else if ((new_parity == 'e') || (new_parity == 'E'))
    err = uart_set_parity(uart_port, UART_PARITY_EVEN);
  else if ((new_parity == 'o') || (new_parity == 'O'))
    err = uart_set_parity(uart_port, UART_PARITY_ODD);
  else
    return ESP_ERR_INVALID_ARG;

  if (err != ESP_OK)
    return err;

  if (new_data == 5)
    err = uart_set_word_length(uart_port, UART_DATA_5_BITS);
  else if (new_data == 6)
    err = uart_set_word_length(uart_port, UART_DATA_6_BITS);
  else if (new_data == 7)
    err = uart_set_word_length(uart_port, UART_DATA_7_BITS);
  else if (new_data == 8)
    err = uart_set_word_length(uart_port, UART_DATA_8_BITS);
  else
    return ESP_ERR_INVALID_ARG;

  if (err != ESP_OK)
    return err;

  if (new_stop == 1)
    err = uart_set_stop_bits(uart_port, UART_STOP_BITS_1);
  else if (new_stop == 2)
    err = uart_set_stop_bits(uart_port, UART_STOP_BITS_2);
  else
    return ESP_ERR_INVALID_ARG;

  if (err != ESP_OK)
    return err;

  return ESP_OK;
}

void k_uart_rx_task(void *pvParameters) {
  uart_event_t uartEvent;
  uint8_t *dtmp = NULL;

  (void)pvParameters;

  vTaskDelay(pdMS_TO_TICKS(500));
  enRadio();

  for (;;) {
    if (xQueueReceive(uart0_queue, (void *)&uartEvent, (TickType_t)100) ==
        pdTRUE) {
      if (uartEvent.type == UART_DATA) {
        dtmp = (uint8_t *)malloc(uartEvent.size + 1);
        uart_read_bytes(uart_port, (void *)dtmp, uartEvent.size, portMAX_DELAY);
      }
    }

    if (dtmp != NULL) {
      // ESP_LOG_BUFFER_HEXDUMP(TAG, dtmp, uartEvent.size, ESP_LOG_INFO);

      if (strstr((char *)dtmp, "YL_800IL") != NULL) {
        int baud = 0;
        char parity = '0';
        int data = 0;
        int stop = 0;

        sscanf((char *)dtmp, "%d %c %d %d YL_800IL", &baud, &parity, &data,
               &stop);

        ESP_LOGI(TAG, "Radio encontrado em baud %d parity %c data %d stop %d",
                 baud, parity, data, stop);

        esp_err_t err = change_uart(baud, parity, data, stop);

        if (err != ESP_OK)
          ESP_LOGW(TAG, "Erro ao configurar uart %d", uart_port);
        else {
          int len = -1;
          uint8_t *request = RF1276_make_radio_read_command(&len);

          configASSERT(request != NULL);
          vTaskDelay(pdMS_TO_TICKS(100));

          if (len > 0) {
            ESP_ERROR_CHECK(uart_flush_input(uart_port));
            uart_write_bytes(uart_port, (const void *)request, len);
          }
        }
      } else if (strstr((char *)dtmp, "\xaf\xaf\x00\x00\xaf") != NULL) {
        if (uartEvent.size == RF1276_COMMAND_SIZE) {
          uart_word_length_t word_length = UART_DATA_BITS_MAX;
          uart_stop_bits_t stop_bits = UART_STOP_BITS_MAX;
          esp_err_t err = ESP_OK;
          radio_data_t *ptr_radio = NULL;

          ptr_radio = RF1276_parse_radio(&dtmp[8], RF1276_DATA_SIZE);

          err = uart_get_word_length(uart_port, &word_length);
          err |= uart_get_stop_bits(uart_port, &stop_bits);

          if (err == ESP_OK) {
            ptr_radio->serial.length = word_length + 5;
            ptr_radio->serial.stop = stop_bits;
          }

          if (ptr_radio != NULL) {
            if (xSemaphoreTake(uart_semphr, pdMS_TO_TICKS(200)) == pdTRUE) {
              memcpy(&radio, ptr_radio, sizeof(radio_data_t));
              xSemaphoreGive(uart_semphr);
            }

            ESP_LOGI(TAG, "Radio detectado");
            free(ptr_radio);
          }
        }
      } else {
        if (xSemaphoreTake(uart_semphr, pdMS_TO_TICKS(200)) == pdTRUE) {
          buffer_rx_len =
              (uartEvent.size > BUF_SIZE) ? BUF_SIZE : uartEvent.size;
          memcpy(buffer_rx, dtmp, buffer_rx_len);
          xSemaphoreGive(uart_semphr);
        }
      }

      free(dtmp);
      dtmp = NULL;
    }
  }

  vTaskDelete(NULL);
}

esp_err_t k_uart_init(void) {

  uart_config_t uart_config = {
      .baud_rate = 9600,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .source_clk = UART_SCLK_DEFAULT,
  };
  gpio_config_t configEn = {.pin_bit_mask =
                                (1ULL << en_pin) | (1ULL << tx2_pin),
                            .mode = GPIO_MODE_OUTPUT,
                            .pull_up_en = GPIO_PULLUP_DISABLE,
                            .pull_down_en = GPIO_PULLDOWN_DISABLE,
                            .intr_type = GPIO_INTR_DISABLE};

  uart_semphr = xSemaphoreCreateMutex();
  configASSERT(uart_semphr != NULL);
  xSemaphoreGive(uart_semphr);

  ESP_ERROR_CHECK(uart_driver_install(uart_port, BUF_SIZE * 2, BUF_SIZE * 2, 20,
                                      &uart0_queue, 0));
  configASSERT(uart0_queue != NULL);
  ESP_ERROR_CHECK(uart_param_config(uart_port, &uart_config));
  ESP_ERROR_CHECK(uart_set_pin(uart_port, tx_pin, rx_pin, UART_PIN_NO_CHANGE,
                               UART_PIN_NO_CHANGE));
  ESP_ERROR_CHECK(uart_enable_pattern_det_baud_intr(uart_port, '\n',
                                                    PATTERN_CHR_NUM, 9, 0, 0));
  ESP_ERROR_CHECK(uart_pattern_queue_reset(uart_port, 20));
  ESP_ERROR_CHECK(gpio_config(&configEn));
  gpio_set_level(tx2_pin, 1);

  disableRaidio();

  ESP_LOGI(TAG, "Init...");

  xTaskCreate(k_uart_rx_task, "k_uart_rx_task", 2048, NULL, 5, NULL);

  return ESP_OK;
}

esp_err_t lora_info_get_handler(httpd_req_t *req) {
  cJSON *root = cJSON_CreateObject();

  if (root == NULL) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  if (xSemaphoreTake(uart_semphr, pdMS_TO_TICKS(200)) == pdTRUE) {
    memset(&radio, 0, sizeof(radio_data_t));
    xSemaphoreGive(uart_semphr);
  }

  disableRaidio();
  change_uart(9600, 'N', 8, 1);
  vTaskDelay(pdMS_TO_TICKS(500));
  enRadio();

  float freq = -1.0f;
  radio_data_t mRadio = {0};

  while (freq <= 0.0f) {
    vTaskDelay(pdMS_TO_TICKS(200));

    if (xSemaphoreTake(uart_semphr, pdMS_TO_TICKS(200)) == pdTRUE) {
      freq = radio.frequency;

      if (freq > 0.0f) {
        memcpy(&mRadio, &radio, sizeof(radio_data_t));
      }

      xSemaphoreGive(uart_semphr);
    }
  }

  char *json_str = RF1276_toJson(&mRadio);

  if (json_str == NULL) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, json_str);

  free((void *)json_str);
  return ESP_OK;
}

static baud_rate_t parse_baud_rate(const char *str) {
  if (strcmp(str, "1200") == 0)
    return B1200BPS;
  if (strcmp(str, "2400") == 0)
    return B2400BPS;
  if (strcmp(str, "4800") == 0)
    return B4800BPS;
  if (strcmp(str, "9600") == 0)
    return B9600BPS;
  if (strcmp(str, "19200") == 0)
    return B19200BPS;
  if (strcmp(str, "38400") == 0)
    return B38400BPS;
  if (strcmp(str, "57600") == 0)
    return B57600BPS;
  if (strcmp(str, "115200") == 0)
    return B115200PS;
  return BINVPS;
}

static parity_t parse_parity(const char *str) {
  if (strcasecmp(str, "Even") == 0)
    return EVEN_PARITY;
  if (strcasecmp(str, "Odd") == 0)
    return ODD_PARITY;
  return NO_PARITY;
}

static rf_factor_t parse_rf_factor(const char *str) {
  int val = atoi(str);
  if (val == 256)
    return RF_256;
  if (val == 512)
    return RF_512;
  if (val == 1024)
    return RF_1024;
  if (val == 2048)
    return RF_2048;
  if (val == 4096)
    return RF_4096;
  return RF_128; // Default fallback
}

static radio_mode_t parse_radio_mode(const char *str) {
  if (strcasecmp(str, "LowPower") == 0 || strcasecmp(str, "Low Power") == 0)
    return MODE_LOW_POWER;
  if (strcasecmp(str, "Sleep") == 0)
    return MODE_SLEEP;
  return MODE_STANDARD;
}

static rf_bw_t parse_rf_bw(const char *str) {
  if (strcasecmp(str, "62.5K") == 0)
    return BW_62_5K;
  if (strcasecmp(str, "125K") == 0)
    return BW_125K;
  if (strcasecmp(str, "250K") == 0)
    return BW_250K;
  return BW_500K; // Default fallback
}

static rf_power_t parse_rf_power(const char *str) {
  if (strcasecmp(str, "4dBm") == 0)
    return P_4DBM;
  if (strcasecmp(str, "7dBm") == 0)
    return P_7DBM;
  if (strcasecmp(str, "10dBm") == 0)
    return P_10DBM;
  if (strcasecmp(str, "13dBm") == 0)
    return P_13DBM;
  if (strcasecmp(str, "14dBm") == 0)
    return P_14DBM;
  if (strcasecmp(str, "17dBm") == 0)
    return P_17DBM;
  return P_20DBM; // Default fallback
}

esp_err_t lora_info_post_handler(httpd_req_t *req) {
  if (req->content_len <= 0) {
    ESP_LOGE(TAG, "Content-Length is missing or zero");
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing JSON payload");
    return ESP_FAIL;
  }

  char *json_buf = malloc(req->content_len + 1);

  if (!json_buf) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                        "Heap allocation failed");
    return ESP_FAIL;
  }

  int received = httpd_req_recv(req, json_buf, req->content_len);

  if (received <= 0) {
    free(json_buf);

    if (received == HTTPD_SOCK_ERR_TIMEOUT) {
      httpd_resp_send_err(req, HTTPD_408_REQ_TIMEOUT, "Socket timeout");
      return ESP_ERR_TIMEOUT;
    }

    return ESP_FAIL;
  }

  json_buf[received] = '\0';
  cJSON *root = cJSON_Parse(json_buf);
  free(json_buf);

  if (!root) {
    ESP_LOGE(TAG, "JSON Syntax Error");
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                        "Invalid JSON formatting syntax");
    return ESP_FAIL;
  }

  radio_data_t updated_cfg;
  bool updated = false;

  cJSON *freq = cJSON_GetObjectItemCaseSensitive(root, "frequencia_mhz");

  if (cJSON_IsNumber(freq)) {
    updated_cfg.frequency = (float)freq->valuedouble;
    updated = true;
  }

  cJSON *node = cJSON_GetObjectItemCaseSensitive(root, "node_id");

  if (cJSON_IsNumber(node)) {
    updated_cfg.id = (uint16_t)node->valueint;
    updated = true;
  }

  cJSON *net = cJSON_GetObjectItemCaseSensitive(root, "net_id");

  if (cJSON_IsNumber(net)) {
    updated_cfg.net_id = (uint8_t)net->valueint;
    updated = true;
  }

  cJSON *factor = cJSON_GetObjectItemCaseSensitive(root, "rf_factor");

  if (cJSON_IsString(factor) && factor->valuestring) {
    updated_cfg.rf_factor = parse_rf_factor(factor->valuestring);
    updated = true;
  }

  cJSON *mode = cJSON_GetObjectItemCaseSensitive(root, "rf_mode");

  if (cJSON_IsString(mode) && mode->valuestring) {
    updated_cfg.mode = parse_radio_mode(mode->valuestring);
    updated = true;
  }

  cJSON *bw = cJSON_GetObjectItemCaseSensitive(root, "rf_bw_khz");

  if (cJSON_IsString(bw) && bw->valuestring) {
    updated_cfg.rf_bw = parse_rf_bw(bw->valuestring);
    updated = true;
  }

  cJSON *power = cJSON_GetObjectItemCaseSensitive(root, "power");

  if (cJSON_IsString(power) && power->valuestring) {
    updated_cfg.rf_power = parse_rf_power(power->valuestring);
    updated = true;
  }

  cJSON *serial_obj = cJSON_GetObjectItemCaseSensitive(root, "Serial");

  if (cJSON_IsObject(serial_obj)) {
    cJSON *baud = cJSON_GetObjectItemCaseSensitive(serial_obj, "Baud Rate");

    if (cJSON_IsString(baud) && baud->valuestring) {
      updated_cfg.serial.baudrate = parse_baud_rate(baud->valuestring);
      updated = true;
    }

    cJSON *length = cJSON_GetObjectItemCaseSensitive(serial_obj, "Length");

    if (cJSON_IsNumber(length)) {
      updated_cfg.serial.length = length->valueint;
      updated = true;
    }

    cJSON *stop = cJSON_GetObjectItemCaseSensitive(serial_obj, "Stop");

    if (cJSON_IsNumber(stop)) {
      updated_cfg.serial.stop = stop->valueint;
      updated = true;
    }

    cJSON *parity = cJSON_GetObjectItemCaseSensitive(serial_obj, "Parity");

    if (cJSON_IsString(parity) && parity->valuestring) {
      updated_cfg.serial.parity = parse_parity(parity->valuestring);
      updated = true;
    }
  }

  cJSON_Delete(root);

  if (updated) {
    if (xSemaphoreTake(uart_semphr, pdMS_TO_TICKS(200)) == pdTRUE) {
      memcpy(&radio, &updated_cfg, sizeof(radio_data_t));
      xSemaphoreGive(uart_semphr);
    }

    char *novo_radio = RF1276_toJson(&updated_cfg);
    ESP_LOGI(TAG, "%s", novo_radio);
    free(novo_radio);
  } else {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                        "Payload contained zero valid fields");
    return ESP_FAIL;
  }

  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_status(req, "200 OK");
  const char *success_resp =
      "{\"status\":\"success\",\"message\":\"Enums updated\"}";
  httpd_resp_send(req, success_resp, strlen(success_resp));

  return ESP_OK;
}

static char *hexdump_to_string(const void *addr, size_t len) {
  const unsigned char *pc = (const unsigned char *)addr;
  size_t i, j;
  size_t num_rows = (len + 15) / 16;
  size_t buffer_size = (num_rows * 79) + 1;

  char *buffer = (char *)malloc(buffer_size);

  if (!buffer)
    return NULL;

  char *out = buffer;

  for (i = 0; i < len; i += 16) {
    out += sprintf(out, "%08zx  ", i);

    for (j = 0; j < 16; j++) {
      if (i + j < len) {
        out += sprintf(out, "%02x ", pc[i + j]);
      } else {
        out += sprintf(out, "   ");
      }
      if (j == 7) {
        out += sprintf(out, " ");
      }
    }

    out += sprintf(out, " |");

    for (j = 0; j < 16; j++) {
      if (i + j < len) {
        out += sprintf(out, "%c", isprint(pc[i + j]) ? pc[i + j] : '.');
      } else {
        out += sprintf(out, " "); // Pad with spaces if data ends early
      }
    }

    out += sprintf(out, "|\n");
  }

  return buffer;
}

esp_err_t lora_rx_get_handler(httpd_req_t *req) {
  char *buffer_rx_str = NULL;
  size_t buffer_rx_str_len = 0;

  if (xSemaphoreTake(uart_semphr, pdMS_TO_TICKS(200)) == pdTRUE) {
    buffer_rx_str = hexdump_to_string(buffer_rx, buffer_rx_len);
    buffer_rx_str_len = buffer_rx_len;
    xSemaphoreGive(uart_semphr);
  }

  if (buffer_rx_str == NULL) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                        "Failed to allocate memory for hexdump");
    return ESP_FAIL;
  }

  httpd_resp_set_type(req, "text/plain");
  httpd_resp_set_status(req, "200 OK");

  esp_err_t err = httpd_resp_send(req, buffer_rx_str, buffer_rx_str_len);

  free(buffer_rx_str);

  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to send HTTP response payload: %s",
             esp_err_to_name(err));
    return err;
  }

  return ESP_OK;
}