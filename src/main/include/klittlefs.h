#ifndef MAIN_INCLUDE_KLITTLEFS_H_
#define MAIN_INCLUDE_KLITTLEFS_H_

#include "esp_err.h"

/* Mounts the "storage" data partition as LittleFS at CONFIG_KLITTLEFS_BASE_PATH.
 * Formats it on first boot / mount failure. */
esp_err_t klittlefs_init(void);

#endif // MAIN_INCLUDE_KLITTLEFS_H_
