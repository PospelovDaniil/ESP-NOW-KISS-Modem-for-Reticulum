idf_component_register(SRCS "main.cpp"
                           "kiss_codec.cpp"
                           "radio.cpp"
                           "uart_bridge.cpp"
                           "debug.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES esp8266 freertos nvs_flash esp_event tcpip_adapter pthread)
