#
# ESP32-S31-Korvo 主组件 CMakeLists.txt
#
# 源文件:
#   hello_world_main.c  - 主入口, BSP 初始化, UI 创建
#   gif_player.c        - GIF 动图播放器 (lv_gif + PSRAM)
#   led_control.c       - RGB LED 圆形按钮控制面板
#
# 依赖组件:
#   lvgl__lvgl          - LVGL v8.4.0 图形库
#   esp_lvgl_port       - LVGL 移植层 v2.8.0 (任务/线程安全)
#   esp-bsp             - 官方 BSP (显示/触摸/LED)
#   esp_mm              - 内存管理 (PSRAM)
#   esp_timer           - 定时器
#
idf_component_register(SRCS "hello_world_main.c"
                             "gif_player.c"
                             "led_control.c"
                       PRIV_REQUIRES lvgl__lvgl
                                    esp_lvgl_port
                                    esp-bsp
                                    esp_mm
                                    esp_timer
                       INCLUDE_DIRS "."
                       #
                       # 嵌入 GIF 动图文件到固件
                       # 文件名将转换为链接器符号:
                       #   my_animation.gif -> _binary_my_animation_gif_start
                       #
                       # TODO: 将 my_animation.gif 替换为实际的 GIF 文件名
                       EMBED_FILES "my_animation.gif")
