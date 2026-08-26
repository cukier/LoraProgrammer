#ifndef _MAIN_INCLUDE_LORAPROGRAMMER_H_
#define _MAIN_INCLUDE_LORAPROGRAMMER_H_

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "RF1276.h"

#define k_take_semphr(X) (xSemaphoreTake(X, portMAX_DELAY) == pdTRUE)

typedef struct loraprogrammerStr {
  char *lastMensage;
  radio_data_t* radio;
  SemaphoreHandle_t semphr;
  int update;
  int rssi;
} loraprogrammer_t;

#endif