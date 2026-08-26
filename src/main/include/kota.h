#ifndef MAIN_INCLUDE_KOTA_H_
#define MAIN_INCLUDE_KOTA_H_

#include "esp_err.h"
#include "esp_http_server.h"

/* Call once at boot, after a successful start, to cancel the rollback timer
 * left pending by the previous OTA update. */
void kota_mark_valid(void);

/* Registers POST /ota on an already-running httpd server. Body is the raw
 * firmware .bin; on success the device flashes the next OTA slot and reboots. */
esp_err_t kota_register(httpd_handle_t server);

#endif // MAIN_INCLUDE_KOTA_H_
