# ESP32-S31-Korvo GIF + RGB LED 控制项目
# 硬件: ESP32-S31-Korvo (4.3寸 800x480 RGB屏 + GT911触摸, 16MB Flash + 16MB PSRAM)
# 框架: ESP-IDF (Master) + ESP-BSP + esp_lvgl_port v2.8.0 + LVGL v8.4.0
#
cmake_minimum_required(VERSION 3.22)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# 注意: MINIMAL_BUILD 设置为 OFF, 以确保 BSP 及其依赖组件被正确加载。
# 如果设置为 ON, 只编译明确 REQUIRES 的组件, BSP 的间接依赖可能丢失。
idf_build_set_property(MINIMAL_BUILD OFF)

project(01_basic_demo)
