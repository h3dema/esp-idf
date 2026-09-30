idf_component_register(SRCS "simple_wifi_connect.c"
                    INCLUDE_DIRS "."
                    REQUIRES
                        esp_wifi
                        esp_netif
                        esp_event
                        nvs_flash
                        lwip
)
