#ifndef MAIN_INCLUDE_KWIFI_H_
#define MAIN_INCLUDE_KWIFI_H_

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

/* Brings up WiFi in station or AP mode per Kconfig (KWIFI_MODE).
 * In station mode this blocks until connected or retries are exhausted. */
esp_err_t kwifi_init(void);

bool kwifi_is_connected(void);

/* Writes the current IPv4 address as a dotted string ("0.0.0.0" if not connected). */
void kwifi_get_ip_str(char *buf, size_t len);

/* Station-mode only: signal strength (dBm) of the link to the AP we're connected
 * to, via esp_wifi_sta_get_ap_info(). Returns ESP_ERR_NOT_SUPPORTED in AP-mode
 * builds, ESP_ERR_INVALID_STATE if not currently connected. */
esp_err_t kwifi_get_rssi(int8_t *out_rssi);

#endif // MAIN_INCLUDE_KWIFI_H_
