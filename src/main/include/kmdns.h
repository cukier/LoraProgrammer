#ifndef MAIN_INCLUDE_KMDNS_H_
#define MAIN_INCLUDE_KMDNS_H_

#include "esp_err.h"

/* Advertises the device as http://<KMDNS_HOSTNAME>.local/. Call after kwifi_init(). */
esp_err_t kmdns_init(void);

#endif // MAIN_INCLUDE_KMDNS_H_
