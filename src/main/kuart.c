#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/uart.h"

#include "esp_log.h"

#include "RF1276.h"
#include "loraprogrammer.h"

#include <string.h>

#define BUF_SIZE (1024)
#define PATTERN_CHR_NUM (3)

typedef enum RadioRoleEnum {
  No_Role,
  Find_Radio_Role,
  Get_Radio_Data,
  Write_Radio_Data,
  Read_RSSI
} radio_role_t;

static const gpio_num_t tx2_pin = GPIO_NUM_25;
static const gpio_num_t tx_pin = GPIO_NUM_26;
static const gpio_num_t rx_pin = GPIO_NUM_14;
static const gpio_num_t en_pin = GPIO_NUM_27;
static const uart_port_t uart_port = UART_NUM_1;
static const char* TAG = "kauart";
static const char* Roles[] = {
    "No_Role",          "Find_Radio_Role", "Get_Radio_Data",
    "Write_Radio_Data", "Read_RSSI",
};

static QueueHandle_t uart0_queue = NULL;
static SemaphoreHandle_t uart_semphr = NULL;
static radio_role_t role = No_Role;
static loraprogrammer_t* loraprogrammer = NULL;
static SemaphoreHandle_t semphr = NULL;
static int radioFound = 0;

static void enRadio() {
  gpio_set_level(en_pin, 1);
  ESP_LOGI(TAG, "Ligado");
}

static void disableRaidio() {
  gpio_set_level(en_pin, 0);
  ESP_LOGI(TAG, "Desligado");
}

esp_err_t k_uart_init(void* pvParameters) {
  loraprogrammer = (loraprogrammer_t*)pvParameters;
  semphr = loraprogrammer->semphr;

  uart_config_t uart_config = {
      .baud_rate = 9600,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .source_clk = UART_SCLK_DEFAULT,
  };
  gpio_config_t configEn = {
      .pin_bit_mask = (1ULL << en_pin) | (1ULL << tx2_pin),
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE};

  role = Find_Radio_Role;
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

  return ESP_OK;
}

static esp_err_t change_uart_speed(uint32_t new_baudrate) {
  uart_wait_tx_done(uart_port, 100 / portTICK_PERIOD_MS);
  return uart_set_baudrate(uart_port, new_baudrate);
}

void k_uart_task(void* pvParameters) {
  ESP_LOGI(TAG, "k_uart_task Init...");

  for (;;) {
    radio_role_t tx_role = No_Role;

    vTaskDelay(100);

    if (k_take_semphr(uart_semphr)) {
      tx_role = role;
      xSemaphoreGive(uart_semphr);
    }

    switch (tx_role) {
      case Find_Radio_Role:
        disableRaidio();
        vTaskDelay(pdMS_TO_TICKS(1000));
        enRadio();
        vTaskDelay(pdMS_TO_TICKS(1000));
        break;

      case Write_Radio_Data:
        vTaskDelay(pdMS_TO_TICKS(1000));

        if (k_take_semphr(uart_semphr)) {
          role = Find_Radio_Role;
          xSemaphoreGive(uart_semphr);
        }
        break;

      default:
        break;
    }

    taskYIELD();
  }

  vTaskDelete(NULL);
}

void k_uart_rx_task(void* pvParameters) {
  ESP_LOGI(TAG, "k_uart_queue_task Init...");

  for (;;) {
    uart_event_t uartEvent;
    uint8_t* dtmp = NULL;
    radio_role_t rx_role = No_Role;

    vTaskDelay(100);

    if (k_take_semphr(uart_semphr)) {
      rx_role = role;
      xSemaphoreGive(uart_semphr);
    }

    if (xQueueReceive(uart0_queue, (void*)&uartEvent, (TickType_t)100) ==
        pdTRUE) {
      switch (uartEvent.type) {
        case UART_DATA: {
          dtmp = (uint8_t*)malloc(uartEvent.size + 1);
          uart_read_bytes(uart_port, (void*)dtmp, uartEvent.size,
                          portMAX_DELAY);
        } break;
        case UART_BREAK:
        case UART_BUFFER_FULL:
        case UART_FIFO_OVF:
        case UART_FRAME_ERR:
        case UART_PARITY_ERR:
        case UART_DATA_BREAK:
        case UART_PATTERN_DET:
        case UART_EVENT_MAX:
          break;
      }

      if (dtmp != NULL) {
        ESP_LOGI(TAG, "recebido %d bytes role %s:", uartEvent.size,
                 Roles[rx_role]);
        dtmp[uartEvent.size] = '\0';
        ESP_LOG_BUFFER_HEXDUMP(TAG, dtmp, uartEvent.size, ESP_LOG_INFO);

        switch (rx_role) {
          case Find_Radio_Role: {
            if ((strstr((char*)dtmp, "9600") != NULL) ||
                (strstr((char*)dtmp, "19200") != NULL)) {
              ESP_LOGI(TAG, "Radio encontrado");

              if (strstr((char*)dtmp, "19200") != NULL) {
                ESP_ERROR_CHECK(change_uart_speed(19200));
                ESP_LOGI(TAG, "Alterado baud para 19200");
              }

              if (k_take_semphr(uart_semphr)) {
                role = Get_Radio_Data;
                xSemaphoreGive(uart_semphr);
              }

              int len = -1;
              uint8_t* request = RF1276_make_radio_read_command(&len);
              configASSERT(request != NULL);
              vTaskDelay(pdMS_TO_TICKS(100));

              if (len > 0) {
                ESP_ERROR_CHECK(uart_flush_input(uart_port));
                uart_write_bytes(uart_port, (const void*)request, len);
                // ESP_LOG_BUFFER_HEXDUMP(TAG, request, len, ESP_LOG_INFO);
              }

              free(request);
            }
          } break;

          case Get_Radio_Data: {
            if (uartEvent.size == RF1276_COMMAND_SIZE) {
              radio_data_t* radio =
                  RF1276_parse_radio(&dtmp[8], RF1276_DATA_SIZE);

              if (radio != NULL) {
                radioFound = 1;
                char* parse = RF1276_toString(radio);

                if (k_take_semphr(semphr)) {
                  memcpy(loraprogrammer->radio, radio, sizeof(radio_data_t));
                  xSemaphoreGive(semphr);
                }

                free(radio);

                if (parse != NULL) {
                  ESP_LOGI(TAG, "%s", parse);
                  free(parse);
                }
              }
            } else if (radioFound) {
              if (k_take_semphr(semphr)) {
                role = Read_RSSI;
                xSemaphoreGive(semphr);
              }

              int len = 0;
              uint8_t* rssi = RF1276_make_radio_rssi_command(&len);
              uart_write_bytes(uart_port, (const void*)rssi, len);
              free(rssi);
            } else {
              if (k_take_semphr(semphr)) {
                role = Find_Radio_Role;
                xSemaphoreGive(semphr);
              }
            }
          } break;

          case Read_RSSI: {
            if (uartEvent.size == RF1276_COMMAND_SIZE_RSSI) {
              int rssi = 0;

              rssi = dtmp[8] - 164;

              if (k_take_semphr(semphr)) {
                loraprogrammer->rssi = rssi;
                role = Get_Radio_Data;
                xSemaphoreGive(semphr);
              }
            } else {
              if (k_take_semphr(semphr)) {
                role = Get_Radio_Data;
                xSemaphoreGive(semphr);
              }
            }
          } break;

          default:
            break;
        }

        if (rx_role != Read_RSSI) {
          if (k_take_semphr(semphr)) {
            memset(loraprogrammer->lastMensage, '0', 1024);
            strcpy(loraprogrammer->lastMensage, (char*)dtmp);
            xSemaphoreGive(semphr);
          }
        }

        free(dtmp);
      }
    }

    if (k_take_semphr(semphr)) {
      if (loraprogrammer->update) {
        loraprogrammer->update = 0;
        int len = 0;
        uint8_t* data =
            RF1276_make_radio_write_command(loraprogrammer->radio, &len);

        if (data != NULL) {
          // ESP_LOG_BUFFER_HEXDUMP(TAG, data, len, ESP_LOG_INFO);

          if (k_take_semphr(uart_semphr)) {
            role = Write_Radio_Data;
            xSemaphoreGive(uart_semphr);

            uart_write_bytes(uart_port, (const void*)data, len);
          }

          free(data);
        }
      }

      xSemaphoreGive(semphr);
    }

    taskYIELD();
  }

  vTaskDelete(NULL);
}