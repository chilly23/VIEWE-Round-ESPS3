# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

set(IDF_TARGET esp32s3)
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../VIEWE-Demos/viewe_bsp" "${CMAKE_CURRENT_LIST_DIR}/../VIEWE-Demos/shared-components")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

list(APPEND EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/components/bsp_extra")

add_compile_options(-Wno-format)
idf_build_set_property(MINIMAL_BUILD ON)
project(lvgl_demo_v9)
