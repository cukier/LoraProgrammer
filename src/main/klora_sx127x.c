/* NiceRF LoRa127X-C1 (Semtech SX1276) back end for klora.h.
 *
 * Selected by CONFIG_KLORA_RADIO_SX127X. The chip is driven directly over native
 * SPI with the dernasherbrezon/sx127x component - there is no on-module config
 * store, so the settings under "KLORA Configuration" are applied at every boot
 * and can be re-applied at runtime through POST /lora.
 *
 * REST surface (same paths as the RF1276 back end in kuart.c):
 *   GET  /lora        - current radio configuration + TX/RX counters as JSON
 *   POST /lora        - apply any subset of the config fields (JSON body)
 *   GET  /lora/rxtx   - hex dump (text/plain) of the last received payload
 *   POST /lora/rxtx   - transmit {"message": "..."} as one LoRa packet
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"

#include <sx127x.h>

#include "cJSON.h"

#include "include/klora.h"

static const char *TAG = "klora";

#define KLORA_CHECK(expr)                                                       \
  do {                                                                          \
    int _err = (expr);                                                          \
    if (_err != SX127X_OK) {                                                    \
      ESP_LOGE(TAG, #expr " failed: 0x%x", _err);                               \
      return ESP_FAIL;                                                          \
    }                                                                          \
  } while (0)

/* Guards s_device (every sx127x_*() call, since the library isn't reentrant)
 * and s_info. Recursive because klora_interrupt_task holds it across
 * sx127x_handle_interrupt(), which synchronously calls the rx/tx callbacks on
 * the same task - a plain mutex would deadlock there. */
static SemaphoreHandle_t s_lock;
static SemaphoreHandle_t s_tx_done_sem;
static sx127x s_device;
static TaskHandle_t s_interrupt_task;

static struct klora_state {
  uint32_t frequency_hz;
  uint32_t bandwidth_hz;
  int spreading_factor;  /* 6-12 */
  int coding_rate_denom; /* 5-8, meaning 4/5..4/8 */
  uint8_t sync_word;
  uint16_t preamble_length;
  int tx_power_dbm;
  bool crc_enabled;

  const char *opmode_str;

  uint32_t tx_count;
  uint32_t tx_bytes;
  uint32_t tx_errors;
  uint32_t last_tx_ms;

  uint32_t rx_count;
  uint16_t last_rx_len;
  int16_t last_rx_rssi;
  float last_rx_snr;
  uint32_t last_rx_ms;
  uint8_t last_rx_payload[255];
} s_info;

static sx127x_sf_t klora_sf_from_int(int sf) {
  switch (sf) {
  case 6:
    return SX127X_SF_6;
  case 7:
    return SX127X_SF_7;
  case 8:
    return SX127X_SF_8;
  case 9:
    return SX127X_SF_9;
  case 10:
    return SX127X_SF_10;
  case 11:
    return SX127X_SF_11;
  default:
    return SX127X_SF_12;
  }
}

static sx127x_cr_t klora_cr_from_denom(int denom) {
  switch (denom) {
  case 5:
    return SX127X_CR_4_5;
  case 6:
    return SX127X_CR_4_6;
  case 7:
    return SX127X_CR_4_7;
  default:
    return SX127X_CR_4_8;
  }
}

static void klora_rx_callback(void *ctx, uint8_t *data, uint16_t data_length) {
  sx127x *device = (sx127x *)ctx;
  int16_t rssi = 0;
  float snr = 0;
  sx127x_rx_get_packet_rssi(device, &rssi);
  sx127x_lora_rx_get_packet_snr(device, &snr);

  xSemaphoreTakeRecursive(s_lock, portMAX_DELAY);
  s_info.rx_count++;
  s_info.last_rx_len = data_length;
  s_info.last_rx_rssi = rssi;
  s_info.last_rx_snr = snr;
  s_info.last_rx_ms = (uint32_t)(esp_timer_get_time() / 1000);
  memcpy(s_info.last_rx_payload, data,
         data_length > sizeof(s_info.last_rx_payload)
             ? sizeof(s_info.last_rx_payload)
             : data_length);
  xSemaphoreGiveRecursive(s_lock);

  ESP_LOGI(TAG, "rx: %u bytes, rssi=%d snr=%.1f", (unsigned)data_length, rssi,
           (double)snr);
}

static void klora_tx_callback(void *ctx) {
  (void)ctx;
  xSemaphoreGive(s_tx_done_sem);
}

static void IRAM_ATTR klora_isr(void *arg) {
  (void)arg;
  BaseType_t woken = pdFALSE;
  vTaskNotifyGiveFromISR(s_interrupt_task, &woken);
  portYIELD_FROM_ISR(woken);
}

static void klora_interrupt_task(void *arg) {
  (void)arg;
  for (;;) {
    if (ulTaskNotifyTake(pdTRUE, portMAX_DELAY) > 0) {
      xSemaphoreTakeRecursive(s_lock, portMAX_DELAY);
      sx127x_handle_interrupt(&s_device);
      xSemaphoreGiveRecursive(s_lock);
    }
  }
}

static void klora_reset_chip(void) {
  gpio_config_t io_conf = {
      .pin_bit_mask = 1ULL << CONFIG_KLORA_RESET_GPIO,
      .mode = GPIO_MODE_OUTPUT,
  };
  gpio_config(&io_conf);
  gpio_set_level(CONFIG_KLORA_RESET_GPIO, 0);
  vTaskDelay(pdMS_TO_TICKS(5));
  gpio_set_level(CONFIG_KLORA_RESET_GPIO, 1);
  vTaskDelay(pdMS_TO_TICKS(10));
}

static esp_err_t klora_init_spi(spi_device_handle_t *out_handle) {
  spi_bus_config_t bus_cfg = {
      .mosi_io_num = CONFIG_KLORA_MOSI_GPIO,
      .miso_io_num = CONFIG_KLORA_MISO_GPIO,
      .sclk_io_num = CONFIG_KLORA_SCK_GPIO,
      .quadwp_io_num = -1,
      .quadhd_io_num = -1,
      .max_transfer_sz = 0,
  };
  /* DMA required: without it, ESP32 SPI transactions cap at the 64-byte
   * hardware FIFO (SPI_DMA_DISABLED), which silently fails any LoRa payload
   * above that. */
  esp_err_t err = spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
  if (err != ESP_OK) {
    return err;
  }

  spi_device_interface_config_t dev_cfg = {
      .clock_speed_hz = 2 * 1000 * 1000,
      .spics_io_num = CONFIG_KLORA_SS_GPIO,
      .queue_size = 16,
      .command_bits = 0,
      .address_bits = 8,
      .dummy_bits = 0,
      .mode = 0,
  };
  return spi_bus_add_device(SPI2_HOST, &dev_cfg, out_handle);
}

esp_err_t klora_init(void) {
  s_lock = xSemaphoreCreateRecursiveMutex();
  s_tx_done_sem = xSemaphoreCreateBinary();

  if (s_lock == NULL || s_tx_done_sem == NULL) {
    return ESP_ERR_NO_MEM;
  }

  klora_reset_chip();

  spi_device_handle_t spi_device;
  esp_err_t err = klora_init_spi(&spi_device);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "spi bus init failed: %s", esp_err_to_name(err));
    return err;
  }

  KLORA_CHECK(sx127x_create(spi_device, &s_device));
  KLORA_CHECK(
      sx127x_set_opmod(SX127X_MODE_STANDBY, SX127X_MODULATION_LORA, &s_device));
  KLORA_CHECK(sx127x_set_frequency(CONFIG_KLORA_FREQUENCY_HZ, &s_device));
  KLORA_CHECK(sx127x_lora_reset_fifo(&s_device));
  KLORA_CHECK(sx127x_lora_set_bandwidth(
      sx127x_hz_to_bandwidth(CONFIG_KLORA_BANDWIDTH_HZ), &s_device));
  KLORA_CHECK(
      sx127x_lora_set_implicit_header(NULL, &s_device)); /* explicit header */
  KLORA_CHECK(sx127x_lora_set_spreading_factor(
      klora_sf_from_int(CONFIG_KLORA_SPREADING_FACTOR), &s_device));
  KLORA_CHECK(sx127x_lora_set_syncword(CONFIG_KLORA_SYNC_WORD, &s_device));
  KLORA_CHECK(
      sx127x_set_preamble_length(CONFIG_KLORA_PREAMBLE_LENGTH, &s_device));
  KLORA_CHECK(sx127x_tx_set_pa_config(SX127X_PA_PIN_BOOST,
                                      CONFIG_KLORA_TX_POWER_DBM, &s_device));

  sx127x_tx_header_t header = {
      .enable_crc = CONFIG_KLORA_ENABLE_CRC,
      .coding_rate = klora_cr_from_denom(CONFIG_KLORA_CODING_RATE_DENOM),
  };
  KLORA_CHECK(sx127x_lora_tx_set_explicit_header(&header, &s_device));

  sx127x_rx_set_callback(klora_rx_callback, &s_device, &s_device);
  sx127x_tx_set_callback(klora_tx_callback, &s_device, &s_device);

  s_info.frequency_hz = CONFIG_KLORA_FREQUENCY_HZ;
  s_info.bandwidth_hz = CONFIG_KLORA_BANDWIDTH_HZ;
  s_info.spreading_factor = CONFIG_KLORA_SPREADING_FACTOR;
  s_info.coding_rate_denom = CONFIG_KLORA_CODING_RATE_DENOM;
  s_info.sync_word = CONFIG_KLORA_SYNC_WORD;
  s_info.preamble_length = CONFIG_KLORA_PREAMBLE_LENGTH;
  s_info.tx_power_dbm = CONFIG_KLORA_TX_POWER_DBM;
  s_info.crc_enabled = CONFIG_KLORA_ENABLE_CRC;
  s_info.opmode_str = "rx_cont";

  if (xTaskCreatePinnedToCore(klora_interrupt_task, "klora_irq", 4096, NULL, 5,
                              &s_interrupt_task, tskNO_AFFINITY) != pdPASS) {
    ESP_LOGE(TAG, "failed to create klora_irq task");
    return ESP_FAIL;
  }

  esp_err_t isr_err = gpio_install_isr_service(0);
  if (isr_err != ESP_OK && isr_err != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG, "gpio_install_isr_service failed: %s",
             esp_err_to_name(isr_err));
    return isr_err;
  }

  gpio_config_t dio0_conf = {
      .pin_bit_mask = 1ULL << CONFIG_KLORA_DIO0_GPIO,
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_ENABLE,
      .intr_type = GPIO_INTR_POSEDGE,
  };
  gpio_config(&dio0_conf);
  ESP_ERROR_CHECK(gpio_isr_handler_add(CONFIG_KLORA_DIO0_GPIO, klora_isr, NULL));

  KLORA_CHECK(
      sx127x_set_opmod(SX127X_MODE_RX_CONT, SX127X_MODULATION_LORA, &s_device));

  ESP_LOGI(TAG, "ready: %u Hz, SF%d, BW %u Hz, TX %d dBm",
           (unsigned)CONFIG_KLORA_FREQUENCY_HZ, CONFIG_KLORA_SPREADING_FACTOR,
           (unsigned)CONFIG_KLORA_BANDWIDTH_HZ, CONFIG_KLORA_TX_POWER_DBM);

  return ESP_OK;
}

static esp_err_t klora_send(const uint8_t *data, size_t len) {
  if (data == NULL || len == 0 || len > 255) {
    return ESP_ERR_INVALID_ARG;
  }

  xSemaphoreTakeRecursive(s_lock, portMAX_DELAY);
  int code = sx127x_lora_tx_set_for_transmission(data, (uint8_t)len, &s_device);
  if (code == SX127X_OK) {
    code = sx127x_set_opmod(SX127X_MODE_TX, SX127X_MODULATION_LORA, &s_device);
  }
  if (code == SX127X_OK) {
    s_info.opmode_str = "tx";
  }
  xSemaphoreGiveRecursive(s_lock);

  esp_err_t err;
  if (code != SX127X_OK) {
    ESP_LOGW(TAG, "tx setup failed: 0x%x", code);
    err = ESP_FAIL;
  } else if (xSemaphoreTake(s_tx_done_sem,
                            pdMS_TO_TICKS(CONFIG_KLORA_TX_TIMEOUT_MS)) !=
             pdTRUE) {
    ESP_LOGW(TAG, "tx timed out after %d ms", CONFIG_KLORA_TX_TIMEOUT_MS);
    err = ESP_ERR_TIMEOUT;
  } else {
    err = ESP_OK;
  }

  /* Always resume listening, whether the send succeeded, failed outright, or
   * timed out - a half-configured TX mode left dangling would deafen the radio.
   */
  xSemaphoreTakeRecursive(s_lock, portMAX_DELAY);
  sx127x_set_opmod(SX127X_MODE_RX_CONT, SX127X_MODULATION_LORA, &s_device);
  s_info.opmode_str = "rx_cont";
  if (err == ESP_OK) {
    s_info.tx_count++;
    s_info.tx_bytes += len;
    s_info.last_tx_ms = (uint32_t)(esp_timer_get_time() / 1000);
  } else {
    s_info.tx_errors++;
  }
  xSemaphoreGiveRecursive(s_lock);

  return err;
}

static esp_err_t klora_set_frequency(uint32_t hz) {
  xSemaphoreTakeRecursive(s_lock, portMAX_DELAY);
  int code = sx127x_set_frequency(hz, &s_device);
  if (code == SX127X_OK) {
    s_info.frequency_hz = hz;
  }
  xSemaphoreGiveRecursive(s_lock);
  return code == SX127X_OK ? ESP_OK : ESP_FAIL;
}

static esp_err_t klora_set_bandwidth(uint32_t hz) {
  xSemaphoreTakeRecursive(s_lock, portMAX_DELAY);
  sx127x_bw_t bw = sx127x_hz_to_bandwidth(hz);
  int code = sx127x_lora_set_bandwidth(bw, &s_device);
  if (code == SX127X_OK) {
    s_info.bandwidth_hz = sx127x_bandwidth_to_hz(bw);
  }
  xSemaphoreGiveRecursive(s_lock);
  return code == SX127X_OK ? ESP_OK : ESP_FAIL;
}

static esp_err_t klora_set_spreading_factor(int sf) {
  if (sf < 6 || sf > 12) {
    return ESP_ERR_INVALID_ARG;
  }
  xSemaphoreTakeRecursive(s_lock, portMAX_DELAY);
  int code = sx127x_lora_set_spreading_factor(klora_sf_from_int(sf), &s_device);
  if (code == SX127X_OK) {
    s_info.spreading_factor = sf;
  }
  xSemaphoreGiveRecursive(s_lock);
  return code == SX127X_OK ? ESP_OK : ESP_FAIL;
}

static esp_err_t klora_set_coding_rate(int denom) {
  if (denom < 5 || denom > 8) {
    return ESP_ERR_INVALID_ARG;
  }
  xSemaphoreTakeRecursive(s_lock, portMAX_DELAY);
  sx127x_tx_header_t header = {
      .enable_crc = s_info.crc_enabled,
      .coding_rate = klora_cr_from_denom(denom),
  };
  int code = sx127x_lora_tx_set_explicit_header(&header, &s_device);
  if (code == SX127X_OK) {
    s_info.coding_rate_denom = denom;
  }
  xSemaphoreGiveRecursive(s_lock);
  return code == SX127X_OK ? ESP_OK : ESP_FAIL;
}

static esp_err_t klora_set_sync_word(uint8_t sync_word) {
  xSemaphoreTakeRecursive(s_lock, portMAX_DELAY);
  int code = sx127x_lora_set_syncword(sync_word, &s_device);
  if (code == SX127X_OK) {
    s_info.sync_word = sync_word;
  }
  xSemaphoreGiveRecursive(s_lock);
  return code == SX127X_OK ? ESP_OK : ESP_FAIL;
}

static esp_err_t klora_set_preamble_length(uint16_t length) {
  xSemaphoreTakeRecursive(s_lock, portMAX_DELAY);
  int code = sx127x_set_preamble_length(length, &s_device);
  if (code == SX127X_OK) {
    s_info.preamble_length = length;
  }
  xSemaphoreGiveRecursive(s_lock);
  return code == SX127X_OK ? ESP_OK : ESP_FAIL;
}

static esp_err_t klora_set_tx_power(int dbm) {
  xSemaphoreTakeRecursive(s_lock, portMAX_DELAY);
  int code = sx127x_tx_set_pa_config(SX127X_PA_PIN_BOOST, dbm, &s_device);
  if (code == SX127X_OK) {
    s_info.tx_power_dbm = dbm;
  }
  xSemaphoreGiveRecursive(s_lock);
  return code == SX127X_OK ? ESP_OK : ESP_FAIL;
}

static esp_err_t klora_set_crc(bool enable) {
  xSemaphoreTakeRecursive(s_lock, portMAX_DELAY);
  sx127x_tx_header_t header = {
      .enable_crc = enable,
      .coding_rate = klora_cr_from_denom(s_info.coding_rate_denom),
  };
  int code = sx127x_lora_tx_set_explicit_header(&header, &s_device);
  if (code == SX127X_OK) {
    s_info.crc_enabled = enable;
  }
  xSemaphoreGiveRecursive(s_lock);
  return code == SX127X_OK ? ESP_OK : ESP_FAIL;
}

static void klora_add_age_field(cJSON *obj, const char *key, uint32_t last_ms,
                                uint32_t now_ms) {
  if (last_ms == 0) {
    cJSON_AddNullToObject(obj, key);
  } else {
    cJSON_AddNumberToObject(obj, key, (double)(now_ms - last_ms));
  }
}

esp_err_t lora_info_get_handler(httpd_req_t *req) {
  xSemaphoreTakeRecursive(s_lock, portMAX_DELAY);
  struct klora_state info = s_info;
  xSemaphoreGiveRecursive(s_lock);
  uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);

  cJSON *root = cJSON_CreateObject();

  cJSON *config = cJSON_CreateObject();
  cJSON_AddNumberToObject(config, "frequency_hz", info.frequency_hz);
  cJSON_AddNumberToObject(config, "bandwidth_hz", info.bandwidth_hz);
  cJSON_AddNumberToObject(config, "spreading_factor", info.spreading_factor);
  cJSON_AddNumberToObject(config, "coding_rate_denom", info.coding_rate_denom);
  cJSON_AddNumberToObject(config, "sync_word", info.sync_word);
  cJSON_AddNumberToObject(config, "preamble_length", info.preamble_length);
  cJSON_AddNumberToObject(config, "tx_power_dbm", info.tx_power_dbm);
  cJSON_AddBoolToObject(config, "crc_enabled", info.crc_enabled);
  cJSON_AddItemToObject(root, "config", config);

  cJSON_AddStringToObject(root, "opmode", info.opmode_str);

  cJSON *tx = cJSON_CreateObject();
  cJSON_AddNumberToObject(tx, "count", info.tx_count);
  cJSON_AddNumberToObject(tx, "bytes", info.tx_bytes);
  cJSON_AddNumberToObject(tx, "errors", info.tx_errors);
  klora_add_age_field(tx, "last_tx_ms_ago", info.last_tx_ms, now_ms);
  cJSON_AddItemToObject(root, "tx", tx);

  cJSON *rx = cJSON_CreateObject();
  cJSON_AddNumberToObject(rx, "count", info.rx_count);
  cJSON_AddNumberToObject(rx, "last_length", info.last_rx_len);
  cJSON_AddNumberToObject(rx, "last_rssi", info.last_rx_rssi);
  cJSON_AddNumberToObject(rx, "last_snr", info.last_rx_snr);
  klora_add_age_field(rx, "last_rx_ms_ago", info.last_rx_ms, now_ms);
  cJSON_AddItemToObject(root, "rx", rx);

  char *json = cJSON_PrintUnformatted(root);

  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, json);

  free(json);
  cJSON_Delete(root);

  return ESP_OK;
}

esp_err_t lora_info_post_handler(httpd_req_t *req) {
  if (req->content_len <= 0 || req->content_len >= 2048) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                        "JSON body required, max 2047 bytes");
    return ESP_FAIL;
  }

  char buf[2048];
  int received = 0;
  while (received < (int)req->content_len) {
    int r = httpd_req_recv(req, buf + received, req->content_len - received);
    if (r <= 0) {
      httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "failed to read body");
      return ESP_FAIL;
    }
    received += r;
  }
  buf[received] = '\0';

  cJSON *body = cJSON_Parse(buf);
  if (!body) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid JSON");
    return ESP_FAIL;
  }

  cJSON *applied = cJSON_CreateArray();
  cJSON *errors = cJSON_CreateArray();
  cJSON *item;

#define KLORA_APPLY(key, is_check, setter_call)                                 \
  item = cJSON_GetObjectItemCaseSensitive(body, key);                           \
  if (is_check(item)) {                                                         \
    if ((setter_call) == ESP_OK) {                                              \
      cJSON_AddItemToArray(applied, cJSON_CreateString(key));                   \
    } else {                                                                    \
      cJSON_AddItemToArray(errors, cJSON_CreateString(key));                    \
    }                                                                           \
  }

  KLORA_APPLY("frequency_hz", cJSON_IsNumber,
              klora_set_frequency((uint32_t)item->valuedouble));
  KLORA_APPLY("bandwidth_hz", cJSON_IsNumber,
              klora_set_bandwidth((uint32_t)item->valuedouble));
  KLORA_APPLY("spreading_factor", cJSON_IsNumber,
              klora_set_spreading_factor((int)item->valuedouble));
  KLORA_APPLY("coding_rate_denom", cJSON_IsNumber,
              klora_set_coding_rate((int)item->valuedouble));
  KLORA_APPLY("sync_word", cJSON_IsNumber,
              klora_set_sync_word((uint8_t)item->valuedouble));
  KLORA_APPLY("preamble_length", cJSON_IsNumber,
              klora_set_preamble_length((uint16_t)item->valuedouble));
  KLORA_APPLY("tx_power_dbm", cJSON_IsNumber,
              klora_set_tx_power((int)item->valuedouble));
  KLORA_APPLY("crc_enabled", cJSON_IsBool, klora_set_crc(cJSON_IsTrue(item)));

#undef KLORA_APPLY

  cJSON_Delete(body);

  cJSON *root = cJSON_CreateObject();
  cJSON_AddItemToObject(root, "applied", applied);
  cJSON_AddItemToObject(root, "errors", errors);
  char *json = cJSON_PrintUnformatted(root);

  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, json);

  free(json);
  cJSON_Delete(root);

  return ESP_OK;
}

/* Canonical xxd-style hex dump, matching the RF1276 back end's /lora/rxtx GET. */
static char *hexdump_to_string(const void *addr, size_t len) {
  const unsigned char *pc = (const unsigned char *)addr;
  size_t num_rows = (len + 15) / 16;
  size_t buffer_size = (num_rows * 79) + 1;

  char *buffer = (char *)malloc(buffer_size);
  if (!buffer) {
    return NULL;
  }

  char *out = buffer;
  for (size_t i = 0; i < len; i += 16) {
    out += sprintf(out, "%08zx  ", i);
    for (size_t j = 0; j < 16; j++) {
      if (i + j < len) {
        out += sprintf(out, "%02x ", pc[i + j]);
      } else {
        out += sprintf(out, "   ");
      }
      if (j == 7) {
        out += sprintf(out, " ");
      }
    }
    out += sprintf(out, " |");
    for (size_t j = 0; j < 16; j++) {
      if (i + j < len) {
        out += sprintf(out, "%c", isprint(pc[i + j]) ? pc[i + j] : '.');
      } else {
        out += sprintf(out, " ");
      }
    }
    out += sprintf(out, "|\n");
  }

  return buffer;
}

esp_err_t lora_rxtx_get_handler(httpd_req_t *req) {
  uint8_t payload[sizeof(s_info.last_rx_payload)];
  size_t len;

  xSemaphoreTakeRecursive(s_lock, portMAX_DELAY);
  len = s_info.last_rx_len;
  memcpy(payload, s_info.last_rx_payload, len);
  xSemaphoreGiveRecursive(s_lock);

  char *dump = hexdump_to_string(payload, len);
  if (dump == NULL) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                        "Failed to allocate memory for hexdump");
    return ESP_FAIL;
  }

  httpd_resp_set_type(req, "text/plain");
  esp_err_t err = httpd_resp_send(req, dump, strlen(dump));
  free(dump);
  return err;
}

esp_err_t lora_rxtx_post_handler(httpd_req_t *req) {
  if (req->content_len <= 0 || req->content_len >= 512) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                        "JSON body required, max 511 bytes");
    return ESP_FAIL;
  }

  char buf[512];
  int received = 0;
  while (received < (int)req->content_len) {
    int r = httpd_req_recv(req, buf + received, req->content_len - received);
    if (r <= 0) {
      httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "failed to read body");
      return ESP_FAIL;
    }
    received += r;
  }
  buf[received] = '\0';

  cJSON *body = cJSON_Parse(buf);
  if (!body) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid JSON");
    return ESP_FAIL;
  }

  cJSON *msg = cJSON_GetObjectItemCaseSensitive(body, "message");
  if (!cJSON_IsString(msg) || msg->valuestring[0] == '\0') {
    cJSON_Delete(body);
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                        "\"message\" (non-empty string, max 255 bytes) required");
    return ESP_FAIL;
  }

  size_t len = strlen(msg->valuestring);
  if (len > 255) {
    len = 255; /* LoRa payload limit */
  }
  esp_err_t err = klora_send((const uint8_t *)msg->valuestring, len);
  cJSON_Delete(body);

  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "status", err == ESP_OK ? "success" : "fail");
  cJSON_AddNumberToObject(root, "bytes", len);
  if (err != ESP_OK) {
    cJSON_AddStringToObject(root, "error", esp_err_to_name(err));
  }
  char *json = cJSON_PrintUnformatted(root);

  httpd_resp_set_type(req, "application/json");
  if (err != ESP_OK) {
    httpd_resp_set_status(req, "500 Internal Server Error");
  }
  httpd_resp_sendstr(req, json);

  free(json);
  cJSON_Delete(root);

  return ESP_OK;
}
