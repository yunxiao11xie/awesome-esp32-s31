/*
 * GIF动画播放器实现
 *
 * 实现步骤:
 *   1. 通过链接器符号获取嵌入的 GIF 二进制数据地址
 *   2. 从 Flash 复制到 PSRAM (确保解码器可随机访问)
 *   3. 创建 lv_gif 控件并设置数据源
 *   4. 居中放置在左半屏 (0,0)-(399,479)
 *   5. 监听 LV_EVENT_READY 实现自动无限循环
 *
 * lv_gif 继承自 lv_img, 所有 lv_img 变换 API 均可用。
 *
 * lv_gif 内部机制:
 *   - 播放到最后一帧时暂停并发送 LV_EVENT_READY
 *   - 我们在回调中调用 lv_gif_restart() 实现循环
 */
#include "gif_player.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <string.h>

static const char *TAG = "GIF_PLAYER";

/* =============================================================
 * 嵌入的 GIF 文件链接器符号
 *
 * 由 CMakeLists.txt 中的 EMBED_FILES "my_animation.gif" 生成:
 *   _binary_<filename>_start  -> 数据起始地址
 *   _binary_<filename>_end    -> 数据结束地址
 *
 * TODO: 将下面两行中的 "my_animation" 替换为你的实际 GIF 文件名。
 *       文件名中的特殊字符会被转换为下划线:
 *         "demo-anim.gif" -> _binary_demo_anim_gif_start
 *         "my_image.gif"  -> _binary_my_image_gif_start
 * ============================================================= */
extern const uint8_t _binary_my_animation_gif_start[];
extern const uint8_t _binary_my_animation_gif_end[];

/* =============================================================
 * 事件回调: GIF 播放完毕时自动重启 (实现无限循环)
 *
 * lv_gif 播放到最后一帧后, 内部会调用 lv_timer_pause() 暂停,
 * 并发送 LV_EVENT_READY 事件。我们在此重新调用 restart 以继续循环。
 * ============================================================= */
static void gif_ready_cb(lv_event_t *e)
{
    lv_obj_t *gif = lv_event_get_target(e);
    lv_gif_restart(gif);
}

lv_obj_t *gif_player_create(lv_obj_t *parent)
{
    /* -----------------------------------------------------------
     * 计算 GIF 数据大小 (Flash 中嵌入数据的尺寸)
     * ----------------------------------------------------------- */
    const size_t gif_size = (size_t)(_binary_my_animation_gif_end
                                   - _binary_my_animation_gif_start);

    if (gif_size == 0) {
        ESP_LOGE(TAG, "GIF 数据为空! 请检查 EMBED_FILES 路径");
        return NULL;
    }

    ESP_LOGI(TAG, "GIF 文件大小: %zu bytes", gif_size);

    /* -----------------------------------------------------------
     * 步骤1: 分配 PSRAM 缓冲区并复制 GIF 数据
     *
     * lv_gif 底层的 gifdec 库 (gd_open_gif_data) 对数据做随机访问,
     * 放在 PSRAM 中访问速度更快, 且不占用宝贵的内部 SRAM。
     * ----------------------------------------------------------- */
    uint8_t *gif_data = (uint8_t *)heap_caps_malloc(gif_size, MALLOC_CAP_SPIRAM);
    if (gif_data == NULL) {
        ESP_LOGE(TAG, "PSRAM 分配失败! 需要 %zu 字节", gif_size);
        return NULL;
    }
    memcpy(gif_data, _binary_my_animation_gif_start, gif_size);
    ESP_LOGI(TAG, "GIF 数据已加载到 PSRAM [%p], 大小 %zu 字节",
             (void *)gif_data, gif_size);

    /* -----------------------------------------------------------
     * 步骤2: 创建图像描述符 lv_img_dsc_t
     *
     * lv_gif_set_src() 接受 const void *src 参数。
     * 当 src 类型为 LV_IMG_SRC_VARIABLE 时:
     *   - 将 src 解释为 lv_img_dsc_t* 指针
     *   - 提取 img_dsc->data 作为 GIF 二进制数据传给 gifdec
     *
     * cf=LV_IMG_CF_RAW 表示原始编码格式,
     * 解码器(gifdec) 会自行解析帧结构。
     * ----------------------------------------------------------- */
    lv_img_dsc_t *img_dsc = (lv_img_dsc_t *)heap_caps_malloc(
                                sizeof(lv_img_dsc_t), MALLOC_CAP_SPIRAM);
    if (img_dsc == NULL) {
        ESP_LOGE(TAG, "PSRAM 分配失败 (img_dsc)");
        heap_caps_free(gif_data);
        return NULL;
    }

    img_dsc->data = gif_data;
    img_dsc->data_size = gif_size;
    img_dsc->header.always_zero = 0;
    img_dsc->header.cf = LV_IMG_CF_RAW;       /* 原始数据, 解码器处理 */
    img_dsc->header.w = 0;                     /* 解码器会填充实际尺寸 */
    img_dsc->header.h = 0;

    /* -----------------------------------------------------------
     * 步骤3: 创建容器 (占左半屏, 不拦截触摸事件)
     * ----------------------------------------------------------- */
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_size(cont, 400, 480);
    lv_obj_set_pos(cont, 0, 0);
    lv_obj_set_style_bg_color(cont, lv_color_hex(0x111111), 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    /* -----------------------------------------------------------
     * 步骤4: 创建 lv_gif 控件
     * ----------------------------------------------------------- */
    lv_obj_t *gif = lv_gif_create(cont);
    if (gif == NULL) {
        ESP_LOGE(TAG, "lv_gif_create 失败! 请在 menuconfig 中启用: "
                      "Component config → LVGL → Extra widgets → GIF (LV_USE_GIF)");
        heap_caps_free(gif_data);
        heap_caps_free(img_dsc);
        lv_obj_del(cont);
        return NULL;
    }

    /* 设置 GIF 数据源 */
    lv_gif_set_src(gif, img_dsc);

    /* 居中显示在 400x480 容器中 */
    lv_obj_center(gif);

    /* 如果 GIF 尺寸较小需要放大, 可取消注释以下行并调整缩放值:
     *   lv_img_set_zoom(gif, 512);   // 512 = 200%
     *   lv_img_set_pivot(gif, 0, 0);
     *
     * TODO: 根据实际 GIF 尺寸调整缩放比例, 使其适配 400x480 区域
     */

    /* -----------------------------------------------------------
     * 步骤5: 注册循环播放事件
     *
     * 当 lv_gif 播放到最后一帧时发送 LV_EVENT_READY,
     * 我们在回调中调用 lv_gif_restart() 实现无限循环。
     * ----------------------------------------------------------- */
    lv_obj_add_event_cb(gif, gif_ready_cb, LV_EVENT_READY, NULL);

    ESP_LOGI(TAG, "GIF播放器创建完成 (400x480区域居中, 自动循环)");
    return gif;
}
