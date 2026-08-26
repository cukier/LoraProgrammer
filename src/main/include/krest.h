#ifndef MAIN_INCLUDE_KREST_H_
#define MAIN_INCLUDE_KREST_H_

#include "esp_err.h"

/* Starts the HTTP REST server (CONFIG_KREST_SERVER_PORT) with:
 *   GET  /            - minimal firmware-upload page
 *   GET  /info        - free RAM, target, firmware version, IP, uptime, local time
 *   GET  /lora        - LoRa radio config + TX/RX stats (see klora.h)
 *   POST /lora        - reconfigure the radio at runtime (JSON body, see klora.c)
 *   POST /lora/send   - transmit an arbitrary packet ({"text": "..."})
 *   GET  /gps         - latest NMEA GPS fix (see kgps.h)
 *   POST /ota         - firmware update (see kota.h)
 *   POST /reboot      - restart the device after a 5s delay
 * Call after kwifi_init(), klittlefs_init() and kgps_init(). */
esp_err_t krest_init(void);

#endif // MAIN_INCLUDE_KREST_H_
