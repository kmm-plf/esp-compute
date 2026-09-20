#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_idf_version.h"

#if CONFIG_SPIRAM
#include "esp_psram.h"
#endif

#include "driver/gpio.h"
#include "driver/temperature_sensor.h"
#include "driver/touch_sensor.h"
#include "driver/ledc.h"
#include "driver/rmt_tx.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "driver/uart.h"
#include "driver/twai.h"

#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_wifi.h"

static const char *TAG = "SELFTEST";

static volatile uint32_t g_sink;

static void sec(const char *name)
{
    ESP_LOGI(TAG, " ");
    ESP_LOGI(TAG, "========== %s ==========", name);
}

static int64_t now_us(void) { return esp_timer_get_time(); }

static void report_result(const char *what, int64_t us, double units, const char *unit)
{
    double per_sec = (us > 0) ? (units * 1000000.0 / (double)us) : 0.0;
    ESP_LOGI(TAG, "RESULT|%s|time_us=%" PRId64 "|value=%.2f|unit=%s", what, us, per_sec, unit);
}

/* ============================ 1. 芯片信息 ============================ */

static void test_chip_info(void)
{
    sec("1. 芯片信息");
    esp_chip_info_t ci;
    esp_chip_info(&ci);

    const char *model = "unknown";
    switch (ci.model) {
    case CHIP_ESP32:   model = "ESP32";    break;
    case CHIP_ESP32S2: model = "ESP32-S2"; break;
    case CHIP_ESP32S3: model = "ESP32-S3"; break;
    case CHIP_ESP32C3: model = "ESP32-C3"; break;
    default: break;
    }

    ESP_LOGI(TAG, "型号        : %s", model);
    ESP_LOGI(TAG, "核心数      : %d", ci.cores);
    ESP_LOGI(TAG, "芯片版本    : v%d.%d", ci.revision / 100, ci.revision % 100);
    ESP_LOGI(TAG, "WiFi        : %s", (ci.features & CHIP_FEATURE_WIFI_BGN) ? "有" : "无");
    ESP_LOGI(TAG, "BT          : %s", (ci.features & CHIP_FEATURE_BT) ? "有" : "无");
    ESP_LOGI(TAG, "BLE         : %s", (ci.features & CHIP_FEATURE_BLE) ? "有" : "无");
    ESP_LOGI(TAG, "IDF 版本    : %s", esp_get_idf_version());
    ESP_LOGI(TAG, "CPU 频率    : %d MHz", CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);

    uint32_t flash_size = 0;
    if (esp_flash_get_size(NULL, &flash_size) == ESP_OK) {
        ESP_LOGI(TAG, "Flash       : %" PRIu32 " MB", flash_size / (1024U * 1024U));
    } else {
        ESP_LOGI(TAG, "Flash       : 读取失败");
    }

#if CONFIG_SPIRAM
    ESP_LOGI(TAG, "PSRAM       : %u MB", (unsigned)(esp_psram_get_size() / (1024U * 1024U)));
    ESP_LOGI(TAG, "PSRAM 模式  : %s",
             (CONFIG_SPIRAM_MODE_OCT ? "Octal (八线)" : "Quad (四线)"));
#else
    ESP_LOGI(TAG, "PSRAM       : 未启用");
#endif

    uint8_t mac[6];
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK) {
        ESP_LOGI(TAG, "WiFi MAC    : %02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }
    if (esp_read_mac(mac, ESP_MAC_BT) == ESP_OK) {
        ESP_LOGI(TAG, "BT MAC      : %02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }
}

/* ============================ 2. 内存 ============================ */

static void test_memory(void)
{
    sec("2. 内存");
    size_t in_total = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
    size_t in_free  = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t in_max   = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    ESP_LOGI(TAG, "内部 SRAM   : 总 %u KB / 可用 %u KB / 最大连续块 %u KB",
             (unsigned)(in_total / 1024), (unsigned)(in_free / 1024), (unsigned)(in_max / 1024));
    ESP_LOGI(TAG, "RESULT|sram_free|time_us=0|value=%u|unit=KB", (unsigned)(in_free / 1024));

#if CONFIG_SPIRAM
    size_t ps_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    size_t ps_free  = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t ps_max   = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    ESP_LOGI(TAG, "PSRAM       : 总 %u KB / 可用 %u KB / 最大连续块 %u KB",
             (unsigned)(ps_total / 1024), (unsigned)(ps_free / 1024), (unsigned)(ps_max / 1024));
    ESP_LOGI(TAG, "RESULT|psram_free|time_us=0|value=%u|unit=KB", (unsigned)(ps_free / 1024));
#endif

    ESP_LOGI(TAG, "最小历史可用堆: %u KB", (unsigned)(esp_get_minimum_free_heap_size() / 1024));
}

/* ============================ 3. 触摸通道 ============================ */

static void test_touch(void)
{
    sec("3. 触摸通道 (T1-T14 = GPIO1-GPIO14)");

    if (touch_pad_init() != ESP_OK) {
        ESP_LOGE(TAG, "触摸驱动初始化失败");
        return;
    }
    touch_pad_set_fsm_mode(TOUCH_FSM_MODE_TIMER);

    uint32_t base[15] = {0};
    uint32_t lo[15], hi[15];
    bool ok[15] = {false};

    for (int ch = 1; ch <= 14; ch++) {
        if (touch_pad_config((touch_pad_t)ch) != ESP_OK) {
            ESP_LOGW(TAG, "T%-2d (GPIO%-2d) 配置失败", ch, ch);
            continue;
        }
    }
    vTaskDelay(pdMS_TO_TICKS(300));

    for (int ch = 1; ch <= 14; ch++) {
        uint32_t v = 0;
        if (touch_pad_read_raw_data((touch_pad_t)ch, &v) == ESP_OK) {
            base[ch] = v; lo[ch] = v; hi[ch] = v; ok[ch] = true;
            ESP_LOGI(TAG, "T%-2d (GPIO%-2d) 基线 = %" PRIu32, ch, ch, v);
            ESP_LOGI(TAG, "RESULT|touch_T%d_baseline|time_us=0|value=%" PRIu32 "|unit=raw", ch, v);
        } else {
            ESP_LOGW(TAG, "T%-2d (GPIO%-2d) 读取失败", ch, ch);
        }
    }

    ESP_LOGI(TAG, ">>> 请用手指依次触摸排针上的 GPIO1-GPIO14，持续 30 秒 <<<");
    int64_t t0 = now_us();
    while (now_us() - t0 < 30000000LL) {
        for (int ch = 1; ch <= 14; ch++) {
            if (!ok[ch]) continue;
            uint32_t v = 0;
            if (touch_pad_read_raw_data((touch_pad_t)ch, &v) == ESP_OK) {
                if (v < lo[ch]) lo[ch] = v;
                if (v > hi[ch]) hi[ch] = v;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    ESP_LOGI(TAG, "---- 触摸期间变化量（变化越大说明该引脚被摸到 / 越灵敏）----");
    for (int ch = 1; ch <= 14; ch++) {
        if (!ok[ch]) continue;
        int32_t delta = (int32_t)hi[ch] - (int32_t)base[ch];
        ESP_LOGI(TAG, "T%-2d (GPIO%-2d) 基线 %-8" PRIu32 " 最小 %-8" PRIu32 " 最大 %-8" PRIu32 " 变化 %+" PRId32,
                 ch, ch, base[ch], lo[ch], hi[ch], delta);
        ESP_LOGI(TAG, "RESULT|touch_T%d_delta|time_us=0|value=%" PRId32 "|unit=raw", ch, delta);
    }
    touch_pad_deinit();
}

/* ============================ 4. 外设探测 ============================ */

static void test_peripherals(void)
{
    sec("4. 片上外设可用性探测");

    esp_err_t e;

    /* GPIO */
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << 1,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    e = gpio_config(&io);
    ESP_LOGI(TAG, "%-22s %s", "GPIO", (e == ESP_OK) ? "OK" : esp_err_to_name(e));
    gpio_reset_pin(GPIO_NUM_1);

    /* LEDC (PWM) */
    ledc_timer_config_t lt = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_0,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    e = ledc_timer_config(&lt);
    if (e == ESP_OK) {
        ledc_channel_config_t lc = {
            .gpio_num = 2,
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .channel = LEDC_CHANNEL_0,
            .timer_sel = LEDC_TIMER_0,
            .duty = 128,
            .hpoint = 0,
        };
        e = ledc_channel_config(&lc);
    }
    ESP_LOGI(TAG, "%-22s %s", "LEDC (PWM)", (e == ESP_OK) ? "OK" : esp_err_to_name(e));

    /* RMT */
    rmt_tx_channel_config_t rc = {
        .gpio_num = GPIO_NUM_4,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 1000000,
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
    };
    rmt_channel_handle_t rmt_ch = NULL;
    e = rmt_new_tx_channel(&rc, &rmt_ch);
    ESP_LOGI(TAG, "%-22s %s", "RMT", (e == ESP_OK) ? "OK" : esp_err_to_name(e));
    if (rmt_ch) rmt_del_channel(rmt_ch);

    /* I2C master */
    i2c_master_bus_config_t ic = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = GPIO_NUM_8,
        .scl_io_num = GPIO_NUM_9,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t i2c_bus = NULL;
    e = i2c_new_master_bus(&ic, &i2c_bus);
    ESP_LOGI(TAG, "%-22s %s", "I2C (master)", (e == ESP_OK) ? "OK" : esp_err_to_name(e));
    if (i2c_bus) i2c_del_master_bus(i2c_bus);

    /* SPI */
    spi_bus_config_t sc = {
        .mosi_io_num = GPIO_NUM_11,
        .miso_io_num = GPIO_NUM_13,
        .sclk_io_num = GPIO_NUM_12,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };
    e = spi_bus_initialize(SPI2_HOST, &sc, SPI_DMA_CH_AUTO);
    ESP_LOGI(TAG, "%-22s %s", "SPI2 (master)", (e == ESP_OK) ? "OK" : esp_err_to_name(e));
    if (e == ESP_OK) spi_bus_free(SPI2_HOST);

    /* UART1 */
    e = uart_driver_install(UART_NUM_1, 256, 0, 0, NULL, 0);
    if (e == ESP_OK) {
        uart_config_t uc = {
            .baud_rate = 115200,
            .data_bits = UART_DATA_8_BITS,
            .parity = UART_PARITY_DISABLE,
            .stop_bits = UART_STOP_BITS_1,
            .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
            .source_clk = UART_SCLK_DEFAULT,
        };
        e = uart_param_config(UART_NUM_1, &uc);
        if (e == ESP_OK) e = uart_set_pin(UART_NUM_1, 17, 18, -1, -1);
    }
    ESP_LOGI(TAG, "%-22s %s", "UART1", (e == ESP_OK) ? "OK" : esp_err_to_name(e));
    if (e == ESP_OK) uart_driver_delete(UART_NUM_1);

    /* TWAI (CAN) */
    twai_general_config_t tg = TWAI_GENERAL_CONFIG_DEFAULT(GPIO_NUM_5, GPIO_NUM_6, TWAI_MODE_NO_ACK);
    twai_timing_config_t tt = TWAI_TIMING_CONFIG_500KBITS();
    twai_filter_config_t tf = TWAI_FILTER_CONFIG_ACCEPT_ALL();
    e = twai_driver_install(&tg, &tt, &tf);
    ESP_LOGI(TAG, "%-22s %s", "TWAI (CAN)", (e == ESP_OK) ? "OK" : esp_err_to_name(e));
    if (e == ESP_OK) twai_driver_uninstall();

    /* ADC */
    ESP_LOGI(TAG, "%-22s %s", "ADC", "见温度/触摸章节（需外接信号源）");

    /* 定时器 */
    ESP_LOGI(TAG, "%-22s %s", "esp_timer", (esp_timer_early_init() == ESP_OK) ? "OK" : "FAIL");
    ESP_LOGI(TAG, "%-22s %s", "FreeRTOS 双核", (portNUM_PROCESSORS >= 2) ? "OK (2 核)" : "单核");

    /* 随机数 */
    uint32_t r = esp_random();
    ESP_LOGI(TAG, "%-22s OK (示例 %" PRIu32 ")", "硬件随机数", r);
}

/* ============================ 5. CPU 算力 ============================ */

#define INT_ITERS   2000000
#define MAT_N       40
#define FLOAT_ITERS 500000

static int32_t mA[MAT_N][MAT_N];
static int32_t mB[MAT_N][MAT_N];
static int32_t mC[MAT_N][MAT_N];

static uint32_t crc32_sw(const uint8_t *d, size_t n)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        crc ^= d[i];
        for (int k = 0; k < 8; k++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1u)));
        }
    }
    return ~crc;
}

static void bench_int(void)
{
    volatile uint32_t a = 1, b = 3, c = 5;
    for (uint32_t i = 0; i < INT_ITERS; i++) {
        a = a * 1664525u + 1013904223u;
        b = b ^ (a >> 7);
        c = c + (b * 3u) - (a & 0xFFFFu);
        if (c > 0x7FFFFFFFu) c -= 0x7FFFFFFFu;
    }
    g_sink = a + b + c;
}

static void bench_matrix(void)
{
    for (int i = 0; i < MAT_N; i++)
        for (int j = 0; j < MAT_N; j++) {
            int32_t s = 0;
            for (int k = 0; k < MAT_N; k++) s += mA[i][k] * mB[k][j];
            mC[i][j] = s;
        }
    g_sink = (uint32_t)mC[0][0];
}

static void bench_crc(void)
{
    static uint8_t buf[16384];
    for (size_t i = 0; i < sizeof(buf); i++) buf[i] = (uint8_t)(i * 31 + 7);
    uint32_t r = 0;
    for (int rp = 0; rp < 32; rp++) r ^= crc32_sw(buf, sizeof(buf));
    g_sink = r;
}

typedef struct { void (*fn)(void); int64_t us; } bench_t;

static void run_bench(const char *name, void (*fn)(void), double units, const char *unit)
{
    int64_t t0 = now_us();
    fn();
    int64_t dt = now_us() - t0;
    double per_sec = (dt > 0) ? (units * 1000000.0 / (double)dt) : 0.0;
    ESP_LOGI(TAG, "%-18s %8" PRId64 " us   ->  %.2f %s", name, dt, per_sec, unit);
    ESP_LOGI(TAG, "RESULT|cpu_%s|time_us=%" PRId64 "|value=%.2f|unit=%s", name, dt, per_sec, unit);
}

static SemaphoreHandle_t s_sync;
static int64_t s_dual_us;

static void dual_task(void *arg)
{
    (void)arg;
    xSemaphoreTake(s_sync, portMAX_DELAY);
    int64_t t0 = now_us();
    bench_int();
    s_dual_us = now_us() - t0;
    vTaskDelete(NULL);
}

static void test_cpu(void)
{
    sec("5. CPU 算力（单核）");
    run_bench("整数循环", bench_int, (double)INT_ITERS, "次运算/秒");
    run_bench("矩阵乘法", bench_matrix, 2.0 * MAT_N * MAT_N * MAT_N, "次乘加/秒");
    run_bench("CRC32", bench_crc, 32.0 * 16384.0, "字节/秒");

    sec("6. CPU 算力（双核并行）");
    s_sync = xSemaphoreCreateCounting(2, 0);
    if (s_sync) {
        xTaskCreatePinnedToCore(dual_task, "dual", 4096, NULL, 5, NULL, 1);
        vTaskDelay(pdMS_TO_TICKS(50));
        xSemaphoreGive(s_sync);
        xSemaphoreGive(s_sync);
        int64_t t0 = now_us();
        bench_int();
        int64_t dt = now_us() - t0;
        vTaskDelay(pdMS_TO_TICKS(200));
        double combined = (2.0 * INT_ITERS * 1000000.0) / (double)((dt > s_dual_us) ? dt : s_dual_us);
        ESP_LOGI(TAG, "核0 %" PRId64 " us / 核1 %" PRId64 " us", dt, s_dual_us);
        ESP_LOGI(TAG, "双核合计 %.2f 次运算/秒", combined);
        ESP_LOGI(TAG, "RESULT|cpu_dual_int|time_us=%" PRId64 "|value=%.2f|unit=次运算/秒",
                 (dt > s_dual_us) ? dt : s_dual_us, combined);
        vSemaphoreDelete(s_sync);
        s_sync = NULL;
    }
}

static void test_float(void)
{
    sec("7. 浮点算力");
    volatile float x = 1.0f, y = 0.999999f, z = 0.5f;
    int64_t t0 = now_us();
    for (int i = 0; i < FLOAT_ITERS; i++) {
        x = x * y + 0.0001f;
        z = z * 0.99999f - 0.000001f;
        x = x - z * 0.5f;
    }
    int64_t dt = now_us() - t0;
    g_sink = (uint32_t)x;
    double mflops = (3.0 * FLOAT_ITERS * 1000000.0) / (double)dt / 1000000.0;
    ESP_LOGI(TAG, "%d 次迭代耗时 %" PRId64 " us", FLOAT_ITERS, dt);
    ESP_LOGI(TAG, "单精度浮点 %.2f MFLOPS", mflops);
    ESP_LOGI(TAG, "RESULT|float_mflops|time_us=%" PRId64 "|value=%.2f|unit=MFLOPS", dt, mflops);
}

/* ============================ 8. 内存带宽 ============================ */

static void bench_mem(const char *name, void *dst, void *src, size_t size, int reps)
{
    int64_t t0 = now_us();
    for (int i = 0; i < reps; i++) memset(dst, 0xA5, size);
    int64_t t1 = now_us();
    for (int i = 0; i < reps; i++) memcpy(dst, src, size);
    int64_t t2 = now_us();

    double wr = (double)size * reps / ((double)(t1 - t0) / 1000000.0) / 1048576.0;
    double cp = (double)size * reps / ((double)(t2 - t1) / 1000000.0) / 1048576.0;
    ESP_LOGI(TAG, "%-8s %4u KB  memset %8" PRId64 " us -> %7.1f MB/s | memcpy %8" PRId64 " us -> %7.1f MB/s",
             name, (unsigned)(size / 1024), t1 - t0, wr, t2 - t1, cp);
    ESP_LOGI(TAG, "RESULT|mem_%s_memset|time_us=%" PRId64 "|value=%.1f|unit=MB/s", name, t1 - t0, wr);
    ESP_LOGI(TAG, "RESULT|mem_%s_memcpy|time_us=%" PRId64 "|value=%.1f|unit=MB/s", name, t2 - t1, cp);
}

static void test_membw(void)
{
    sec("8. 内存带宽（SRAM vs PSRAM）");

    const size_t sram_sz = 48 * 1024;
    uint8_t *s1 = heap_caps_malloc(sram_sz, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    uint8_t *s2 = heap_caps_malloc(sram_sz, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (s1 && s2) {
        memset(s2, 0x5A, sram_sz);
        bench_mem("SRAM", s1, s2, sram_sz, 200);
    } else {
        ESP_LOGW(TAG, "SRAM 缓冲区分配失败");
    }
    if (s1) heap_caps_free(s1);
    if (s2) heap_caps_free(s2);

#if CONFIG_SPIRAM
    const size_t ps_sz = 512 * 1024;
    uint8_t *p1 = heap_caps_malloc(ps_sz, MALLOC_CAP_SPIRAM);
    uint8_t *p2 = heap_caps_malloc(ps_sz, MALLOC_CAP_SPIRAM);
    if (p1 && p2) {
        memset(p2, 0x5A, ps_sz);
        bench_mem("PSRAM", p1, p2, ps_sz, 20);
    } else {
        ESP_LOGW(TAG, "PSRAM 缓冲区分配失败");
    }
    if (p1) heap_caps_free(p1);
    if (p2) heap_caps_free(p2);
#endif
}

/* ============================ 9. 温度与满载 ============================ */

static float read_temp(temperature_sensor_handle_t h)
{
    float c = 0.0f;
    if (temperature_sensor_get_celsius(h, &c) != ESP_OK) return -999.0f;
    return c;
}

static volatile bool s_load_run;

static void load_task(void *arg)
{
    (void)arg;
    volatile uint32_t a = 1;
    while (s_load_run) {
        for (int i = 0; i < 200000; i++) a = a * 1664525u + 1013904223u;
        g_sink = a;
    }
    vTaskDelete(NULL);
}

static void test_temperature(void)
{
    sec("9. 温度与满载压测");

    temperature_sensor_handle_t ts = NULL;
    temperature_sensor_config_t cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
    if (temperature_sensor_install(&cfg, &ts) != ESP_OK) {
        ESP_LOGE(TAG, "温度传感器安装失败");
        return;
    }
    temperature_sensor_enable(ts);
    vTaskDelay(pdMS_TO_TICKS(200));

    float idle = read_temp(ts);
    ESP_LOGI(TAG, "空载温度    : %.2f C", idle);
    ESP_LOGI(TAG, "RESULT|temp_idle|time_us=0|value=%.2f|unit=C", idle);

    ESP_LOGI(TAG, ">>> 双核满载 45 秒，每 5 秒采样一次 <<<");
    s_load_run = true;
    xTaskCreatePinnedToCore(load_task, "load0", 2048, NULL, 3, NULL, 0);
    xTaskCreatePinnedToCore(load_task, "load1", 2048, NULL, 3, NULL, 1);

    float peak = idle;
    for (int s = 1; s <= 9; s++) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        float c = read_temp(ts);
        if (c > peak) peak = c;
        ESP_LOGI(TAG, "满载 %2d 秒  : %.2f C", s * 5, c);
        ESP_LOGI(TAG, "RESULT|temp_load_%ds|time_us=0|value=%.2f|unit=C", s * 5, c);
    }
    s_load_run = false;
    vTaskDelay(pdMS_TO_TICKS(300));

    ESP_LOGI(TAG, "峰值温度    : %.2f C （比空载高 %.2f C）", peak, peak - idle);
    ESP_LOGI(TAG, "RESULT|temp_peak|time_us=0|value=%.2f|unit=C", peak);
    ESP_LOGI(TAG, "RESULT|temp_rise|time_us=0|value=%.2f|unit=C", peak - idle);

    temperature_sensor_disable(ts);
    temperature_sensor_uninstall(ts);
}

/* ============================ 10. WiFi 扫描 ============================ */

static void test_wifi(void)
{
    sec("10. WiFi 能力（仅扫描，不连接）");

    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        e = nvs_flash_init();
    }
    ESP_LOGI(TAG, "%-18s %s", "NVS", (e == ESP_OK) ? "OK" : esp_err_to_name(e));

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t wcfg = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&wcfg) != ESP_OK) {
        ESP_LOGE(TAG, "WiFi 初始化失败");
        return;
    }
    ESP_LOGI(TAG, "%-18s OK", "WiFi 驱动");

    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK || esp_wifi_start() != ESP_OK) {
        ESP_LOGE(TAG, "WiFi 启动失败");
        esp_wifi_deinit();
        return;
    }
    ESP_LOGI(TAG, "%-18s OK (STA)", "WiFi 启动");

    wifi_scan_config_t sc = { .show_hidden = true };
    if (esp_wifi_scan_start(&sc, true) != ESP_OK) {
        ESP_LOGE(TAG, "扫描失败");
        esp_wifi_stop();
        esp_wifi_deinit();
        return;
    }

    uint16_t ap_num = 0;
    esp_wifi_scan_get_ap_num(&ap_num);
    ESP_LOGI(TAG, "扫描到 %u 个热点", ap_num);
    ESP_LOGI(TAG, "RESULT|wifi_ap_count|time_us=0|value=%u|unit=个", ap_num);

    if (ap_num > 0) {
        if (ap_num > 20) ap_num = 20;
        wifi_ap_record_t *recs = calloc(ap_num, sizeof(wifi_ap_record_t));
        if (recs) {
            if (esp_wifi_scan_get_ap_records(&ap_num, recs) == ESP_OK) {
                ESP_LOGI(TAG, "%-4s %-32s %-6s %-6s %s", "序", "SSID", "信道", "RSSI", "加密");
                for (int i = 0; i < ap_num; i++) {
                    ESP_LOGI(TAG, "%-4d %-32s %-6d %-6d %d",
                             i + 1, (const char *)recs[i].ssid,
                             recs[i].primary, recs[i].rssi, recs[i].authmode);
                }
            }
            free(recs);
        }
    }

    esp_wifi_stop();
    esp_wifi_deinit();
}

/* ============================ main ============================ */

void app_main(void)
{
    vTaskDelay(pdMS_TO_TICKS(2000));

    ESP_LOGI(TAG, " ");
    ESP_LOGI(TAG, "##################################################");
    ESP_LOGI(TAG, "#  ESP32-S3 板级能力自测  (selftest v1.0)        #");
    ESP_LOGI(TAG, "##################################################");
    int64_t boot = now_us();

    test_chip_info();
    test_memory();
    test_touch();
    test_peripherals();
    test_cpu();
    test_float();
    test_membw();
    test_temperature();
    test_wifi();

    sec("测试完成");
    ESP_LOGI(TAG, "总耗时 %.1f 秒", (double)(now_us() - boot) / 1000000.0);
    ESP_LOGI(TAG, "RESULT|total_time|time_us=%" PRId64 "|value=%.1f|unit=秒", now_us() - boot,
             (double)(now_us() - boot) / 1000000.0);
    ESP_LOGI(TAG, "=== ALL DONE ===");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
