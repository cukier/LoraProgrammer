#ifndef MAIN_INCLUDE_KDATETIME_H_
#define MAIN_INCLUDE_KDATETIME_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

/* Sets the local timezone and starts SNTP against CONFIG_KDATETIME_NTP_SERVER.
 * Blocks up to CONFIG_KDATETIME_SYNC_TIMEOUT_SEC for the first sync; sync keeps
 * retrying in the background afterwards regardless of the return value. */
esp_err_t kdatetime_init(void);

bool kdatetime_is_synced(void);

int64_t kdatetime_uptime_seconds(void);

/* Writes uptime formatted as "<days>d <hh>h <mm>m <ss>s". */
void kdatetime_uptime_str(char *buf, size_t len);

/* Writes local time as "YYYY-MM-DD HH:MM:SS". Writes "unsynced" and returns
 * ESP_ERR_INVALID_STATE if SNTP has not completed its first sync yet. */
esp_err_t kdatetime_local_iso8601(char *buf, size_t len);

#endif // MAIN_INCLUDE_KDATETIME_H_
