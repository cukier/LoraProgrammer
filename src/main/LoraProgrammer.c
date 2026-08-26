#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "nvs_flash.h"

#include "kuart.h"
#include "loraprogrammer.h"
#include "softap_sta.h"

#define TASK_SIZE (4092)

void app_main(void) {
  static TaskHandle_t KUartHandle = NULL;
  static TaskHandle_t KUartRxHandle = NULL;

  loraprogrammer_t* loraprogrammer =
      (loraprogrammer_t*)calloc(1, sizeof(loraprogrammer_t));
  configASSERT(loraprogrammer != NULL);
  loraprogrammer->semphr = xSemaphoreCreateMutex();
  loraprogrammer->radio = (radio_data_t*)calloc(1, sizeof(radio_data_t));
  configASSERT(loraprogrammer->radio != NULL);
  loraprogrammer->lastMensage = (char *) calloc(1024, sizeof(char));
  configASSERT(loraprogrammer->lastMensage != NULL);

  ESP_ERROR_CHECK(nvs_flash_init());
  ESP_ERROR_CHECK(softap_sta_init(loraprogrammer));
  ESP_ERROR_CHECK(k_uart_init(loraprogrammer));

  xTaskCreatePinnedToCore(k_uart_task, "uart_task", TASK_SIZE, NULL, 5,
                          &KUartHandle, 0);
  xTaskCreatePinnedToCore(k_uart_rx_task, "k_uart_rx_task", TASK_SIZE, NULL, 5,
                          &KUartRxHandle, 0);
}
