#ifndef _MAIN_INCLUDE_KAUART_H_
#define _MAIN_INCLUDE_KAUART_H_

esp_err_t k_uart_init(void);
esp_err_t lora_info_get_handler(httpd_req_t *req);
esp_err_t lora_info_post_handler(httpd_req_t *req);
esp_err_t lora_rxtx_get_handler(httpd_req_t *req);
esp_err_t lora_rxtx_post_handler(httpd_req_t *req);

#endif