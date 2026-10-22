set(LV_DEMO_DIR "${CMAKE_CURRENT_LIST_DIR}/../../VIEWE-Demos/shared-components/lvgl__lvgl/demos")
file(GLOB_RECURSE LV_DEMOS_SOURCES ${LV_DEMO_DIR}/*.c)

idf_component_register(
    SRCS main.c ${LV_DEMOS_SOURCES}
    INCLUDE_DIRS . ${LV_DEMO_DIR}
    PRIV_REQUIRES viewe_bsp nvs_flash)

idf_component_get_property(LVGL_LIB lvgl__lvgl COMPONENT_LIB)
target_compile_options(
    ${LVGL_LIB} 
    PRIVATE
        -DLV_LVGL_H_INCLUDE_SIMPLE
        -DLV_USE_DEMO_MUSIC
        -Wno-error=attributes
)
