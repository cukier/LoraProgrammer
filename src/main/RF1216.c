#include "RF1276.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

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

  ret->baudrate = (baud_rate_t)data[0];
  ret->parity = (parity_t)data[1];
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

static int getRfFactor(const rf_factor_t rfFactor, char *str) {
  if ((rfFactor < RF_128) || (rfFactor > RF_4096) || (strlen(str) < 4))
    return -1;

  switch (rfFactor) {
  case RF_128:
    strcpy(str, "128");
    break;
  case RF_256:
    strcpy(str, "256");
    break;
  case RF_512:
    strcpy(str, "512");
    break;
  case RF_1024:
    strcpy(str, "1024");
    break;
  case RF_2048:
    strcpy(str, "2048");
    break;
  case RF_4096:
    strcpy(str, "4096");
    break;
  default:
    strcpy(str, "???");
    break;
  }

  return 0;
}

static int getMode(const radio_mode_t rfMode, char *str) {
  if ((rfMode < MODE_STANDARD) || (rfMode > MODE_SLEEP) || (strlen(str) < 9))
    return -1;

  switch (rfMode) {
  case MODE_STANDARD:
    strcpy(str, "STANDARD");
    break;
  case MODE_LOW_POWER:
    strcpy(str, "LOW POWER");
    break;
  case MODE_SLEEP:
    strcpy(str, "SLEEP");
    break;
  default:
    strcpy(str, "???");
    break;
  }

  return 0;
}

static int getRfBw(const rf_bw_t rfBw, char *str) {
  if ((rfBw < BW_62_5K) || (rfBw > BW_500K) || (strlen(str) < 4))
    return -1;

  switch (rfBw) {
  case BW_62_5K:
    strcpy(str, "62.5");
    break;
  case BW_125K:
    strcpy(str, "125");
    break;
  case BW_250K:
    strcpy(str, "250");
    break;
  case BW_500K:
    strcpy(str, "500");
    break;
  default:
    strcpy(str, "???");
    break;
  }

  return 0;
}

static int getRfPower(const rf_power_t rfPwr, char *str) {
  if ((rfPwr < P_4DBM) || (rfPwr > P_20DBM) || (strlen(str) < 9))
    return -1;

  switch (rfPwr) {
  case P_4DBM:
    strcpy(str, "1 (4dBm)");
    break;
  case P_7DBM:
    strcpy(str, "2 (7dBm)");
    break;
  case P_10DBM:
    strcpy(str, "3 (10dBm)");
    break;
  case P_13DBM:
    strcpy(str, "4 (13dBm)");
    break;
  case P_14DBM:
    strcpy(str, "5 (14dBm)");
    break;
  case P_17DBM:
    strcpy(str, "6 (17dBm)");
    break;
  case P_20DBM:
    strcpy(str, "7 (20dBm)");
    break;
  default:
    strcpy(str, "???");
    break;
  }

  return 0;
}

static int getRfBaud(const baud_rate_t rfBaud, char *str) {
  if ((rfBaud < B1200BPS) || (rfBaud > B115200PS) || (strlen(str) < 6))
    return -1;

  switch (rfBaud) {
  case B1200BPS:
    strcpy(str, "1200");
    break;
  case B2400BPS:
    strcpy(str, "2400");
    break;
  case B4800BPS:
    strcpy(str, "4800");
    break;
  case B9600BPS:
    strcpy(str, "9600");
    break;
  case B19200BPS:
    strcpy(str, "19200");
    break;
  case B38400BPS:
    strcpy(str, "38400");
    break;
  case B57600BPS:
    strcpy(str, "57600");
    break;
  case B115200PS:
    strcpy(str, "115200");
    break;
  default:
    break;
  }

  return 0;
}

char *RF1276_toString(radio_data_t *data) {
  if (data == NULL)
    return NULL;

  char *ret = (char *)calloc(512, sizeof(char));

  if (ret != NULL) {
    char rfFactor[5] = {0};
    char rfMode[10] = {0};
    char rfBw[5] = {0};
    char rfPwr[10] = {0};
    char rfBaud[7] = {0};
    int err = 0;

    err = getRfFactor(data->rf_factor, rfFactor);
    err |= getMode(data->mode, rfMode);
    err |= getRfBw(data->rf_bw, rfBw);
    err |= getRfBw(data->rf_power, rfPwr);
    err |= getRfBaud(data->baudrate, rfBaud);

    if (err != 0) {
      free(ret);
      return NULL;
    }

    sprintf(ret, "Frequencia: %3.2f MHz\n", data->frequency / 1000000.0);
    sprintf(ret + strlen(ret), "rf_factor: %s\n", rfFactor);
    sprintf(ret + strlen(ret), "RF_Mode: %s\n", rfMode);
    sprintf(ret + strlen(ret), "RF_BW: %sk\n", rfBw);
    sprintf(ret + strlen(ret), "Node ID: %u\n", data->id);
    sprintf(ret + strlen(ret), "Net ID: %u\n", data->net_id);
    sprintf(ret + strlen(ret), "Power: %s\n", rfPwr);
    sprintf(ret + strlen(ret), "Baud rate: %s", rfBaud);

    switch (data->parity) {
    case NO_PARITY:
      sprintf(ret + strlen(ret), " 8N1");
      break;
    case ODD_PARITY:
      sprintf(ret + strlen(ret), "8O1");
      break;
    case EVEN_PARITY:
      sprintf(ret + strlen(ret), "8E1");
      break;
    }
  }

  return ret;
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
  ret = (uint8_t *)calloc(3, sizeof(uint8_t));

  if (ret == NULL) {
    return NULL;
  }

  aux = (uint32_t)(frequencie / 61.035);

  for (cont = 0; cont < 3; ++cont)
    ret[cont] = RF1276_touchar(aux, cont);

  return ret;
}

uint8_t *RF1276_make_radio_write_command(radio_data_t *data, int *lengh) {
  uint8_t *aux, *m_freq;

  aux = NULL;
  aux = (uint8_t *)malloc(RF1276_DATA_SIZE * sizeof(uint8_t));

  if (aux == NULL) {
    return NULL;
  }

  m_freq = NULL;
  m_freq = RF1276_freqtouchar(data->frequency);

  aux[0] = (uint8_t)data->baudrate;
  aux[1] = (uint8_t)data->parity;
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

uint8_t *RF1276_make_radio_rssi_command(int *lengh) {
  uint8_t *aux = (uint8_t *)calloc(2, sizeof(uint8_t));
  return RF1276_make_radio_request(CMD_RSSI, aux, RF1276_DATA_SIZE_RSSI, lengh);
}

#if 0
char *RF1276_toJson(const radio_data_t *data) {
  if (data == NULL)
    return NULL;

  cJSON *root = cJSON_CreateObject();

  if (root == NULL)
    return NULL;

  cJSON_AddNumberToObject(root, "baudrate", get_baudrate_val(data->baudrate));
  cJSON_AddStringToObject(root, "parity", get_parity_str(data->parity));
  cJSON_AddNumberToObject(root, "frequency", data->frequency);
  cJSON_AddNumberToObject(root, "rf_factor", data->rf_factor);
  cJSON_AddNumberToObject(root, "mode", data->mode);
  cJSON_AddNumberToObject(root, "rf_bw", data->rf_bw);
  cJSON_AddNumberToObject(root, "id", data->id);
  cJSON_AddNumberToObject(root, "net_id", data->net_id);
  cJSON_AddNumberToObject(root, "rf_power", data->rf_power);

  char *json_string = cJSON_Print(root);

  cJSON_Delete(root);

  return json_string;
}
#endif