/**
 * @file hal_key.c
 * @brief ADC 按键阵列硬件抽象
 *
 * 扫描 → 消抖 → 分类 → 队列发送。不含业务逻辑。
 * 业务处理由 app_logic 从队列读取事件后完成。
 */
#include "bsp_board.h"
#include "hal_key.h"

#include "esp_log.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "hal_key";

/* ---- ADC 硬件资源 ---- */
static adc_oneshot_unit_handle_t s_adc_handle        = NULL;
static adc_cali_handle_t         s_adc_cali_handle   = NULL;
static bool                      s_adc_calibrated    = false;
static adc_channel_t             s_adc_channel       = ADC_CHANNEL_0;
static adc_atten_t               s_adc_atten         = ADC_ATTEN_DB_0;
static int                       s_adc_vmax_mv       = 1100;

/* ---- 按键事件队列（由 hal_key_init 传入） ---- */
static QueueHandle_t s_key_queue = NULL;

/* ================================================================
 *  内部按键分类
 * ================================================================ */

/**
 * 原理图分压关系：
 *   R77=10K 上拉到 3.3V；按键按下后分别接不同下拉电阻。
 *
 * 用户按键丝印实测：
 *   SET  : raw 约 315
 *   MODE : raw 约 848
 *   VOL- : raw 约 1371
 *   VOL+ : raw 约 1815
 *
 * 未按下时 BT_ARRAY_ADC 被 10K 上拉到 3.3V，超出原理图标注的
 * ADC 0~2V 范围；当前 S31 ADC 读数表现为 raw=0。因此 idle 既
 * 接受 raw 很低，也保留 raw 很高的兜底判断。
 */
typedef enum {
    KEY_ARRAY_NONE    = 0,
    KEY_ARRAY_SET,
    KEY_ARRAY_MODE,
    KEY_ARRAY_VOL_MINUS,
    KEY_ARRAY_VOL_PLUS,
    KEY_ARRAY_UNKNOWN,
} key_array_raw_t;

static key_array_raw_t key_classify_raw(int raw)
{
    if (raw <= 150 || raw >= 2300) return KEY_ARRAY_NONE;
    if (raw < 600)   return KEY_ARRAY_SET;
    if (raw < 1150)  return KEY_ARRAY_MODE;
    if (raw < 1600)  return KEY_ARRAY_VOL_MINUS;
    if (raw < 2300)  return KEY_ARRAY_VOL_PLUS;
    return KEY_ARRAY_UNKNOWN;
}

/* ================================================================
 *  ADC 衰减档位辅助
 * ================================================================ */

static const char *atten_name(adc_atten_t atten)
{
    switch (atten) {
    case ADC_ATTEN_DB_0:    return "0 dB";
    case ADC_ATTEN_DB_2_5:  return "2.5 dB";
    case ADC_ATTEN_DB_6:    return "6 dB";
    case ADC_ATTEN_DB_12:   return "12 dB";
    default:                return "unknown";
    }
}

static int atten_vmax_mv(adc_atten_t atten)
{
    switch (atten) {
    case ADC_ATTEN_DB_0:    return 1100;
    case ADC_ATTEN_DB_2_5:  return 1500;
    case ADC_ATTEN_DB_6:    return 2200;
    case ADC_ATTEN_DB_12:   return 3300;
    default:                return 1100;
    }
}

/* ================================================================
 *  ADC 硬件初始化
 * ================================================================ */

static esp_err_t key_adc_init(void)
{
    adc_unit_t unit_id = ADC_UNIT_1;
    esp_err_t ret = adc_oneshot_io_to_channel(KEY_ARRAY_ADC_GPIO, &unit_id, &s_adc_channel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GPIO%d 无法映射到 ADC 通道", KEY_ARRAY_ADC_GPIO);
        return ret;
    }

    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = unit_id,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_RETURN_ON_ERROR(
        adc_oneshot_new_unit(&unit_cfg, &s_adc_handle),
        TAG, "ADC unit 创建失败"
    );

    /* 尝试衰减档位，从高到低 */
    const adc_atten_t candidates[] = {
        ADC_ATTEN_DB_12, ADC_ATTEN_DB_6,
        ADC_ATTEN_DB_2_5, ADC_ATTEN_DB_0,
    };
    bool atten_ok = false;
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        adc_oneshot_chan_cfg_t chan_cfg = {
            .bitwidth = ADC_BITWIDTH_DEFAULT,
            .atten = candidates[i],
        };
        ret = adc_oneshot_config_channel(s_adc_handle, s_adc_channel, &chan_cfg);
        if (ret == ESP_OK) {
            s_adc_atten = candidates[i];
            s_adc_vmax_mv = atten_vmax_mv(s_adc_atten);
            atten_ok = true;
            ESP_LOGI(TAG, "ADC 衰减档位: %s", atten_name(s_adc_atten));
            break;
        }
        ESP_LOGW(TAG, "衰减 %s 不可用: %s", atten_name(candidates[i]), esp_err_to_name(ret));
    }
    if (!atten_ok) {
        return ESP_ERR_INVALID_ARG;
    }

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = unit_id,
        .chan = s_adc_channel,
        .atten = s_adc_atten,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ret = adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_adc_cali_handle);
    if (ret == ESP_OK) {
        s_adc_calibrated = true;
        ESP_LOGI(TAG, "ADC 校准: curve fitting");
    } else {
        ESP_LOGW(TAG, "ADC 校准不可用 (使用 raw 估算): %s", esp_err_to_name(ret));
    }
#else
    ESP_LOGW(TAG, "ADC curve fitting 未启用，使用 raw 估算");
#endif

    ESP_LOGI(TAG, "ADC 初始化完成: GPIO%d", KEY_ARRAY_ADC_GPIO);
    return ESP_OK;
}

/* ================================================================
 *  ADC 按键扫描任务
 * ================================================================ */

static void key_scan_task(void *arg)
{
    (void)arg;

    key_array_raw_t stable_key      = KEY_ARRAY_NONE;
    key_array_raw_t candidate_key   = KEY_ARRAY_NONE;
    int             candidate_count = 0;
    int64_t         press_start_ms  = 0;
    bool            long_sent       = false;

    while (true) {
        int raw = 0;
        esp_err_t ret = adc_oneshot_read(s_adc_handle, s_adc_channel, &raw);

        if (ret == ESP_OK) {
            key_array_raw_t sampled = key_classify_raw(raw);

            /* 消抖 */
            if (sampled != candidate_key) {
                candidate_key   = sampled;
                candidate_count = 1;
            } else if (candidate_count < KEY_DEBOUNCE_SAMPLES) {
                candidate_count++;
            }

            /* 状态切换 */
            if (candidate_count >= KEY_DEBOUNCE_SAMPLES &&
                candidate_key != stable_key) {
                key_array_raw_t old = stable_key;
                stable_key = candidate_key;

                if (old == KEY_ARRAY_NONE &&
                    stable_key != KEY_ARRAY_NONE &&
                    stable_key != KEY_ARRAY_UNKNOWN) {
                    press_start_ms = esp_timer_get_time() / 1000;
                    long_sent = false;
                } else if (old != KEY_ARRAY_NONE &&
                           stable_key == KEY_ARRAY_NONE) {
                    if (!long_sent && old != KEY_ARRAY_UNKNOWN) {
                        key_event_msg_t msg = {
                            .key        = (key_event_t)old,
                            .long_press = false,
                        };
                        xQueueSend(s_key_queue, &msg, 0);
                    }
                    press_start_ms = 0;
                    long_sent = false;
                }
            }

            /* 长按检测 */
            if (stable_key != KEY_ARRAY_NONE &&
                stable_key != KEY_ARRAY_UNKNOWN &&
                !long_sent && press_start_ms > 0) {
                int64_t now_ms = esp_timer_get_time() / 1000;
                if (now_ms - press_start_ms >= KEY_LONG_PRESS_MS) {
                    key_event_msg_t msg = {
                        .key        = (key_event_t)stable_key,
                        .long_press = true,
                    };
                    xQueueSend(s_key_queue, &msg, 0);
                    long_sent = true;
                }
            }
        } else {
            ESP_LOGW(TAG, "ADC 读取失败: %s", esp_err_to_name(ret));
        }

        vTaskDelay(pdMS_TO_TICKS(KEY_SCAN_PERIOD_MS));
    }
}

/* ================================================================
 *  对外接口
 * ================================================================ */

esp_err_t hal_key_init(QueueHandle_t notify_queue)
{
    if (notify_queue == NULL) return ESP_ERR_INVALID_ARG;
    s_key_queue = notify_queue;

    esp_err_t ret = key_adc_init();
    if (ret != ESP_OK) return ret;

    xTaskCreate(key_scan_task, "hal_key", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "按键扫描任务已启动");
    return ESP_OK;
}
