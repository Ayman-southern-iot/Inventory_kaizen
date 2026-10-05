set(WEB_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../web/index.html")
set(WEB_GZ  "${CMAKE_CURRENT_BINARY_DIR}/index.html.gz")
set(GZIP_PY "${CMAKE_CURRENT_SOURCE_DIR}/../tools/gzip_file.py")

# Generate once at configure time so EMBED_FILES always has a real file to point at,
# even on a clean first build. The custom command below keeps it up to date afterwards.
execute_process(COMMAND "${python}" "${GZIP_PY}" "${WEB_SRC}" "${WEB_GZ}")

idf_component_register(SRCS "voice_inventory_main.c"
                            "net_wifi.c"
                            "secrets.c"
                            "app_time.c"
                            "inventory_api.c"
                            "event_bus.c"
                            "web_server.c"
                            "led_action.c"
                            "voice_svc.c"
                            "board_audio.c"
                            "query_parse.c"
                       INCLUDE_DIRS "."
                       EMBED_FILES "${WEB_GZ}" "${CMAKE_CURRENT_SOURCE_DIR}/../test/sample.wav"
                       PRIV_REQUIRES esp_driver_gpio esp_driver_i2s esp_driver_i2c esp32_p4_platform esp_codec_dev nvs_flash esp_netif esp_event esp_timer esp_wifi
                                     esp_http_client esp_http_server lwip esp-tls
                                     mbedtls json)

# Re-gzip whenever the page changes, so editing index.html is picked up by a plain
# `idf.py build` without needing a reconfigure.
add_custom_command(
    OUTPUT "${WEB_GZ}"
    COMMAND "${python}" "${GZIP_PY}" "${WEB_SRC}" "${WEB_GZ}"
    DEPENDS "${WEB_SRC}" "${GZIP_PY}"
    COMMENT "Gzipping dashboard page for embedding"
    VERBATIM)
add_custom_target(dashboard_gz DEPENDS "${WEB_GZ}")
add_dependencies(${COMPONENT_LIB} dashboard_gz)
