idf_component_register(
    SRCS "main.cpp"
         "kiss_codec.cpp"
         "radio.cpp"
         "uart_bridge.cpp"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_gpio esp_driver_uart esp_wifi esp_netif esp_event nvs_flash
)
