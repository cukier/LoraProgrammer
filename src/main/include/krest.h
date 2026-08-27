#ifndef MAIN_INCLUDE_KREST_H_
#define MAIN_INCLUDE_KREST_H_

#include "esp_err.h"

/* Starts the HTTP REST server (CONFIG_KREST_SERVER_PORT) with:
 *   GET  /            - minimal firmware-upload page
 *   GET  /info        - free RAM, target, firmware version, IP, uptime, local time
 *   GET  /lora        - LoRa radio configuration (see klora.h)
 *   POST /lora        - reconfigure/program the radio (JSON body, see klora.h)
 *   GET  /lora/rxtx   - hex dump of the last payload received off air
 *   POST /lora/rxtx   - transmit a payload ({"message": "..."})
 *   POST /ota         - firmware update (see kota.h)
 *   POST /reboot      - restart the device after a 5s delay
 * Call after kwifi_init() and klittlefs_init(). */
esp_err_t krest_init(void);

#endif // MAIN_INCLUDE_KREST_H_
