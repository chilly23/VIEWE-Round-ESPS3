idf_component_register(SRCS "main.c" "board_viewe.c" "display_viewe.c" "imu_viewe.c" "sim.c" "render.c" INCLUDE_DIRS "." REQUIRES esp_driver_gpio esp_driver_i2c esp_driver_spi esp_lcd esp_timer espressif__esp_lcd_co5300)
target_compile_options(${COMPONENT_LIB} PRIVATE -O2)
