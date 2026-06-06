idf_component_register(
    SRCS "audio_player.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_wifi esp_netif esp_http_client esp_event nvs_flash driver esp_audio_codec esp-tls display esp_timer
)
