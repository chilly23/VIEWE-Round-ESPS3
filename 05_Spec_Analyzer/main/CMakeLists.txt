idf_component_register(SRCS "main.c" INCLUDE_DIRS "." PRIV_REQUIRES driver nvs_flash viewe_bsp bsp_extra espressif__esp-dsp)

idf_component_get_property(LVGL_LIB lvgl__lvgl COMPONENT_LIB)
target_compile_options(${LVGL_LIB} PRIVATE -Wno-error=attributes)
