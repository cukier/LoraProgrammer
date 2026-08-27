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

  int data = 0;
  int stop = 0;

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
      ESP_LOG_BUFFER_HEXDUMP(TAG, dtmp, uartEvent.size, ESP_LOG_INFO);

      if (strstr((char *)dtmp, "YL_800IL") != NULL) {
        int baud = 0;
        char parity = '0';

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
          radio_data_t *ptr_radio =
              RF1276_parse_radio(&dtmp[8], RF1276_DATA_SIZE);

          ptr_radio->serial.length = data;
          ptr_radio->serial.stop = stop;

          if (ptr_radio != NULL) {
            if (xSemaphoreTake(uart_semphr, pdMS_TO_TICKS(200)) == pdTRUE) {
              memcpy(&radio, ptr_radio, sizeof(radio_data_t));
              xSemaphoreGive(uart_semphr);
            }

            char *parse = RF1276_toJson(ptr_radio);
            free(ptr_radio);

            if (parse != NULL) {
              ESP_LOGI(TAG, "%s", parse);
              free(parse);
            }
          }
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

esp_err_t lora_get_handler(httpd_req_t *req) {
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

  const char *json_str = RF1276_toJson(&mRadio);

  if (json_str == NULL) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, json_str);

  free((void *)json_str);
  return ESP_OK;
}