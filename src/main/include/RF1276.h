/*
 * RF1276.h
 *
 *  Created on: Sep 8, 2015
 *      Author: cuki
 */

#ifndef _MAIN_INCLUDE_RF1276_H_
#define _MAIN_INCLUDE_RF1276_H_

#include <stdint.h>

#define RF1276_DATA_SIZE 12
#define RF1276_DATA_SIZE_RSSI 2
#define RF1276_COMMAND_SIZE 23
#define RF1276_COMMAND_SIZE_RSSI 12
#define RF1276_HEADER_SIZE 8

typedef enum command_yy_enum {
  CMD_WRITE = 1,
  CMD_READ,
  CMD_STANDARD,
  CMD_CENTRAL,
  CMD_NODE,
  CMD_RSSI
} command_yy_t;

typedef enum command_xx_enum {
  CMD_XX_RESPONSE = 0x00,
  CMD_XX_SENDING = 0x80
} command_xx_t;

typedef enum baud_rate_enum {
  B1200BPS = 1,
  B2400BPS,
  B4800BPS,
  B9600BPS,
  B19200BPS,
  B38400BPS,
  B57600BPS,
  B115200PS,
  BINVPS
} baud_rate_t;

typedef enum parity_enum { NO_PARITY, ODD_PARITY, EVEN_PARITY } parity_t;

typedef enum rf_factor_enum {
  RF_128 = 7,
  RF_256,
  RF_512,
  RF_1024,
  RF_2048,
  RF_4096
} rf_factor_t;

typedef enum mode_enum {
  MODE_STANDARD,
  MODE_LOW_POWER,
  MODE_SLEEP
} radio_mode_t;

typedef enum rf_bw_enum { BW_62_5K = 6, BW_125K, BW_250K, BW_500K } rf_bw_t;

typedef enum rf_power_enum {
  P_4DBM = 1,
  P_7DBM,
  P_10DBM,
  P_13DBM,
  P_14DBM,
  P_17DBM,
  P_20DBM
} rf_power_t;

typedef struct radio_data_str {
  baud_rate_t baudrate;
  parity_t parity;
  float frequency;
  rf_factor_t rf_factor;
  radio_mode_t mode;
  rf_bw_t rf_bw;
  uint16_t id;
  uint8_t net_id;
  rf_power_t rf_power;
} radio_data_t;

uint8_t *RF1276_make_radio_read_command(int *lengh);
radio_data_t *RF1276_parse_radio(uint8_t *data, int len);
char *RF1276_toString(radio_data_t *data);
uint8_t *RF1276_make_radio_write_command(radio_data_t *data, int *lengh);
uint8_t *RF1276_make_radio_rssi_command(int *lengh);
char *RF1276_toJson(const radio_data_t *data);

#endif /* RF1279_H_ */
