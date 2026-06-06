idf_component_register(SRCS "main.c" "app_logic.c" "app_ui.c" "hal_key.c" "hal_led.c" "hal_display.c" "led_strip_encoder.c"
                       PRIV_REQUIRES lvgl esp_lvgl_port esp_lcd_touch_gt1151 esp_driver_rmt esp_adc esp_timer esp_driver_tsens
                       INCLUDE_DIRS "")

#
# 嵌入 GIF 二进制文件到固件
#
# TODO: 将下方 "animation.gif" 替换为您的实际 GIF 文件路径
# 使用方法：
#   1. 在 main/ 目录下创建 gif/ 文件夹
#   2. 将动画 GIF 放入 main/gif/ 目录
#   3. 将下方文件名改为实际文件名（如 "my_animation.gif"）
#   4. 取消下方行的注释
#
target_add_binary_data(${COMPONENT_LIB} "gif/dance_1.gif" BINARY)
target_add_binary_data(${COMPONENT_LIB} "gif/dance_2.gif" BINARY)
target_add_binary_data(${COMPONENT_LIB} "gif/dance_3.gif" BINARY)
