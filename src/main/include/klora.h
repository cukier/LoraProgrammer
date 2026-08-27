#ifndef _MAIN_INCLUDE_KLORA_H_
#define _MAIN_INCLUDE_KLORA_H_

#include "esp_err.h"
#include "esp_http_server.h"

/* Radio abstraction with two interchangeable back ends, picked at build time in
 * menuconfig ("KLORA Configuration" -> "LoRa radio module"):
 *
 *   CONFIG_KLORA_RADIO_RF1276  - Appcon Wireless RF1276 / YL_800IL, driven over
 *                                UART with the 0xAF-framed command protocol
 *                                (kuart.c + RF1216.c).
 *   CONFIG_KLORA_RADIO_SX127X  - NiceRF LoRa127X-C1 (Semtech SX1276) driven
 *                                directly over native SPI (klora_sx127x.c, using
 *                                the dernasherbrezon/sx127x component).
 *
 * Exactly one back end is compiled (see main/CMakeLists.txt). Both implement the
 * symbols below, so krest.c and LoraProgrammer.c never need to know which radio
 * is fitted. */

/* Brings up whichever radio the build selected and starts a background RX task.
 * Call once at boot. */
esp_err_t klora_init(void);

/* GET  /lora        - current radio configuration as JSON.
 * POST /lora        - apply a JSON body of configuration fields (any subset).
 *
 * The JSON shape differs per back end because the two modules expose different
 * parameters (RF1276: rf_factor/net_id/serial..., SX127X: spreading_factor/
 * bandwidth_hz/sync_word...). GET's output round-trips into POST for the same
 * back end. */
esp_err_t lora_info_get_handler(httpd_req_t *req);
esp_err_t lora_info_post_handler(httpd_req_t *req);

/* GET  /lora/rxtx   - hex dump (text/plain) of the last payload received off air.
 * POST /lora/rxtx   - transmit a payload: JSON body {"message": "..."}. */
esp_err_t lora_rxtx_get_handler(httpd_req_t *req);
esp_err_t lora_rxtx_post_handler(httpd_req_t *req);

#endif
