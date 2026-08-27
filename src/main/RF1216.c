#include "RF1276.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

static const char *const baud_strings[] = {"1200",  "2400",  "4800",  "9600",
                                           "19200", "38400", "57600", "115200"};
static const char *const parity_strings[] = {"None", "Odd", "Even"};
static const char *const factor_strings[] = {"128",  "256",  "512",
                                             "1024", "2048", "4096"};
static const char *const mode_strings[] = {"Standard", "Low Power", "Sleep"};
static const char *const bw_strings[] = {"62.5K", "125K", "250K", "500K"};
static const char *const power_strings[] = {"4dBm",  "7dBm",  "10dBm", "13dBm",
                                            "14dBm", "17dBm", "20dBm"};

static uint8_t RF1276_crc(uint8_t *data, uint16_t size) {
  uint16_t acumulo, cont;

  acumulo = 0;
  for (cont = 0; cont < size; ++cont)
    acumulo += data[cont];

  return (uint8_t)acumulo % 256;
}

static uint8_t *RF1276_make_radio_request(command_yy_t cmd_type, uint8_t *data,
                                          uint16_t size, int *lengh) {
  uint8_t *ret;
  uint8_t aux[size + 8];
  uint16_t cont;

  ret = NULL;
  ret = (uint8_t *)calloc((size + 11), sizeof(uint8_t));

  if (lengh != NULL)
    *lengh = size + 11;

  if (ret == NULL) {
    free(data);
    return NULL;
  }

  ret[0] = 0xAF;
  ret[1] = 0xAF;
  ret[2] = 0x00;
  ret[3] = 0x00;
  ret[4] = 0xAF;
  ret[5] = (uint8_t)CMD_XX_SENDING;
  ret[6] = (uint8_t)cmd_type;
  ret[7] = (uint8_t)size;

  for (cont = 0; cont < size; ++cont)
    ret[cont + 8] = data[cont];

  free(data);

  for (cont = 0; cont < sizeof(aux); ++cont)
    aux[cont] = ret[cont];

  ret[size + 8] = RF1276_crc(aux, sizeof(aux));
  ret[size + 9] = 0x0D;
  ret[size + 10] = 0x0A;

  return ret;
}

uint8_t *RF1276_make_radio_read_command(int *lengh) {
  uint8_t *data = (uint8_t *)calloc(RF1276_DATA_SIZE, sizeof(uint8_t));

  if ((data == NULL) || (lengh == NULL)) {
    if (lengh != NULL)
      *lengh = -1;

    return NULL;
  }

  return RF1276_make_radio_request(CMD_READ, data, RF1276_DATA_SIZE, lengh);
}

radio_data_t *RF1276_parse_radio(uint8_t *data, int len) {
  if (data == NULL || len != RF1276_DATA_SIZE)
    return NULL;

  radio_data_t *ret = calloc(1, sizeof(radio_data_t));

  if (ret == NULL)
    return NULL;

  ret->serial.baudrate = (baud_rate_t)data[0];
  ret->serial.parity = (parity_t)data[1];
  ret->frequency = (float)(((data[2] << 16) & 0xFF0000) |
                           ((data[3] << 8) & 0xFF00) | (data[4] & 0xFF)) *
                   61.035;
  ret->rf_factor = (rf_factor_t)data[5];
  ret->mode = (radio_mode_t)data[6];
  ret->rf_bw = (rf_bw_t)data[7];
  memcpy(&(ret->id), &data[8], 2);
  ret->net_id = (uint8_t)data[10];
  ret->rf_power = (rf_power_t)data[11];

  return ret;
}

static const char *getRfFactorStr(const rf_factor_t factor) {
  return (factor >= RF_128 && factor <= RF_4096)
             ? factor_strings[factor - RF_128]
             : "Unknown";
}

static const char *getRadioModeStr(const radio_mode_t mode) {
  return (mode >= MODE_STANDARD && mode <= MODE_SLEEP) ? mode_strings[mode]
                                                       : "Unknown";
}

static const char *getRfBwStr(const rf_bw_t bw) {
  return (bw >= BW_62_5K && bw <= BW_500K) ? bw_strings[bw - BW_62_5K]
                                           : "Unknown";
}

static const char *getRfPowerStr(const rf_power_t power) {
  return (power >= P_4DBM && power <= P_20DBM) ? power_strings[power - P_4DBM]
                                               : "Unknown";
}

static const char *getRfBaudStr(const baud_rate_t rfBaud) {
  return (rfBaud >= B1200BPS && rfBaud <= B115200PS)
             ? baud_strings[rfBaud - B1200BPS]
             : "Unknown";
}

static const char *getParityConfigStr(const parity_t parity) {
  return (parity >= NO_PARITY && parity <= EVEN_PARITY)
             ? parity_strings[parity - NO_PARITY]
             : "Unknown";
}

char *RF1276_toJson(const radio_data_t *data) {
  char *json_string = NULL;
  cJSON *root = cJSON_CreateObject();

  if (root == NULL)
    return NULL;

  cJSON_AddNumberToObject(root, "frequencia_mhz", data->frequency / 1000000.0);
  cJSON_AddNumberToObject(root, "node_id", data->id);
  cJSON_AddNumberToObject(root, "net_id", data->net_id);
  cJSON_AddStringToObject(root, "rf_factor", getRfFactorStr(data->rf_factor));
  cJSON_AddStringToObject(root, "rf_mode", getRadioModeStr(data->mode));
  cJSON_AddStringToObject(root, "rf_bw_khz", getRfBwStr(data->rf_bw));
  cJSON_AddStringToObject(root, "power", getRfPowerStr(data->rf_power));

  cJSON *serial_radio = cJSON_CreateObject();

  cJSON_AddStringToObject(serial_radio, "Baud Rate",
                          getRfBaudStr(data->serial.baudrate));
  cJSON_AddNumberToObject(serial_radio, "Length", data->serial.length);
  cJSON_AddStringToObject(serial_radio, "Parity",
                          getParityConfigStr(data->serial.parity));
  cJSON_AddNumberToObject(serial_radio, "Stop", data->serial.stop);
  cJSON_AddItemToObject(root, "Serial", serial_radio);
  json_string = cJSON_Print(root);
  cJSON_Delete(root);

  return json_string;
}

static uint8_t RF1276_touchar(int in, int index) {
  int mask, aux;

  mask = 0xFF << index * 8;
  aux = in & mask;
  aux >>= index * 8;
  aux &= 0xFF;

  return (uint8_t)aux;
}

static uint8_t *RF1276_freqtouchar(float frequencie) {
  uint8_t *ret, cont;
  uint32_t aux;

  ret = NULL;
  ret = (uint8_t *)malloc(3 * sizeof(uint8_t));

  if (ret == NULL) {
    fprintf(stderr, "Out of memory\n");
    return NULL;
  }

  aux = (uint32_t)((float)frequencie / 61.035);

  for (cont = 0; cont < 3; ++cont)
    ret[cont] = RF1276_touchar(aux, cont);

  return ret;
}

uint8_t *RF1276_make_radio_write_command(radio_data_t *data, int *lengh) {
  uint8_t *aux, *m_freq;

  aux = NULL;
  aux = (uint8_t *)malloc(RF1276_DATA_SIZE * sizeof(uint8_t));

  if (aux == NULL) {
    fprintf(stderr, "Out of memory\n");
    return NULL;
  }

  m_freq = NULL;
  m_freq = RF1276_freqtouchar(data->frequency);

  aux[0] = (uint8_t)data->serial.baudrate;
  aux[1] = (uint8_t)data->serial.parity;
  aux[2] = m_freq[2];
  aux[3] = m_freq[1];
  aux[4] = m_freq[0];
  aux[5] = (uint8_t)data->rf_factor;
  aux[6] = (uint8_t)data->mode;
  aux[7] = (uint8_t)data->rf_bw;
  aux[8] = ((data->id & 0xFF00) >> 8) & 0xFF;
  aux[9] = data->id & 0xFF;
  aux[10] = data->net_id;
  aux[11] = (uint8_t)data->rf_power;

  free(m_freq);

  return RF1276_make_radio_request(CMD_WRITE, aux, RF1276_DATA_SIZE, lengh);
}