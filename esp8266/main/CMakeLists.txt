idf_component_register(
    SRCS "main.cpp"
         "kiss_codec.cpp"
         "radio.cpp"
         "uart_bridge.cpp"
    INCLUDE_DIRS "."
    REQUIRES driver esp_wifi esp_event nvs_flash log
)
