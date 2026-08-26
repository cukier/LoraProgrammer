#ifndef _MAIN_INCLUDE_KAUART_H_
#define _MAIN_INCLUDE_KAUART_H_

esp_err_t k_uart_init(void *pvParameters);
void k_uart_task(void *pvParameters);
void k_uart_rx_task(void *pvParameters);

#endif