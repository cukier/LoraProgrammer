set(srcs
    "LoraProgrammer.c"
    "kwifi.c"
    "kmdns.c"
    "klittlefs.c"
    "kdatetime.c"
    "kota.c"
    "krest.c"
)

set(requires
    cjson
    esp_driver_gpio
    esp_http_server
    esp_wifi
    esp_timer
    nvs_flash
    app_update
)

# Only one LoRa back end is compiled, picked by "KLORA Configuration" in
# menuconfig. See main/include/klora.h.
if(CONFIG_KLORA_RADIO_SX127X)
    list(APPEND srcs "klora_sx127x.c")
    list(APPEND requires esp_driver_spi sx127x)
else()
    list(APPEND srcs "kuart.c" "RF1216.c")
    list(APPEND requires esp_driver_uart)
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS
      "."
      "include"
    REQUIRES ${requires}
)
