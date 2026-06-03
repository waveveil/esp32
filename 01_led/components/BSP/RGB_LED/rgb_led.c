#include "rgb_led.h"
#include "driver/rmt_tx.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <string.h>

#define TAG "RGB"

/* WS2812 timing in RMT ticks @ 10MHz (1 tick = 100ns) */
#define T0H  4   /* 400ns */
#define T0L  9   /* 900ns (~850ns spec) */
#define T1H  8   /* 800ns */
#define T1L  5   /* 500ns (~450ns spec) */
#define WS2812_RESET_US 60

/* Pre-computed RMT symbols for every byte value (256 * 8 = 2048 entries) */
static rmt_symbol_word_t s_rmt_table[256 * 8];

static rmt_channel_handle_t s_tx_chan = NULL;
static rmt_encoder_handle_t s_copy_encoder = NULL;

static void ws2812_build_table(void)
{
    for (int byte = 0; byte < 256; byte++) {
        for (int bit = 0; bit < 8; bit++) {
            if (byte & (1 << (7 - bit))) {
                s_rmt_table[byte * 8 + bit] = (rmt_symbol_word_t){
                    .duration0 = T1H, .level0 = 1,
                    .duration1 = T1L, .level1 = 0,
                };
            } else {
                s_rmt_table[byte * 8 + bit] = (rmt_symbol_word_t){
                    .duration0 = T0H, .level0 = 1,
                    .duration1 = T0L, .level1 = 0,
                };
            }
        }
    }
}

static void ws2812_write(uint8_t r, uint8_t g, uint8_t b)
{
    rmt_symbol_word_t buf[24]; /* 3 colors * 8 bits */
    uint8_t grb[3] = {g, r, b};

    for (int i = 0; i < 3; i++) {
        memcpy(&buf[i * 8], &s_rmt_table[grb[i] * 8], 8 * sizeof(rmt_symbol_word_t));
    }

    rmt_transmit_config_t cfg = { .loop_count = 0 };
    ESP_ERROR_CHECK(rmt_transmit(s_tx_chan, s_copy_encoder, buf, sizeof(buf), &cfg));
    ESP_ERROR_CHECK(rmt_tx_wait_all_done(s_tx_chan, portMAX_DELAY));
}

void rgb_led_init(void)
{
    ws2812_build_table();

    /* RMT TX channel at 10MHz resolution */
    rmt_tx_channel_config_t tx_cfg = {
        .gpio_num = RGB_LED_GPIO,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
    };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_cfg, &s_tx_chan));

    rmt_copy_encoder_config_t enc_cfg = {};
    ESP_ERROR_CHECK(rmt_new_copy_encoder(&enc_cfg, &s_copy_encoder));

    ESP_ERROR_CHECK(rmt_enable(s_tx_chan));

    ESP_LOGI(TAG, "WS2812 initialized on GPIO %d", RGB_LED_GPIO);
}

/* ================================================================
   Color & Effect Utilities
   ================================================================ */

typedef struct {
    uint8_t r, g, b;
} rgb_t;

static rgb_t hsv_to_rgb(float h, float s, float v)
{
    h = fmodf(h, 360.0f);
    float c = v * s;
    float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
    float m = v - c;
    float r, g, b;

    if (h < 60)       { r = c; g = x; b = 0; }
    else if (h < 120) { r = x; g = c; b = 0; }
    else if (h < 180) { r = 0; g = c; b = x; }
    else if (h < 240) { r = 0; g = x; b = c; }
    else if (h < 300) { r = x; g = 0; b = c; }
    else              { r = c; g = 0; b = x; }

    return (rgb_t){
        .r = (uint8_t)((r + m) * 255.0f),
        .g = (uint8_t)((g + m) * 255.0f),
        .b = (uint8_t)((b + m) * 255.0f),
    };
}

/* ================================================================
   Effects
   ================================================================ */

static void effect_rainbow_wave(void)
{
    /* Smooth full-spectrum rainbow that sweeps continuously */
    for (int i = 0; i < 720; i++) {
        float hue = (float)i * 0.5f;
        float brightness = 0.6f + 0.4f * sinf((float)i * 0.05f);
        rgb_t c = hsv_to_rgb(hue, 1.0f, brightness);
        ws2812_write(c.r, c.g, c.b);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void effect_breathing(void)
{
    /* Color-shifting breath: fades in/out while hue slowly rotates */
    float base_hue = 0.0f;
    for (int cycle = 0; cycle < 3; cycle++) {
        for (int i = 0; i < 360; i++) {
            float brightness = (sinf((float)i * M_PI / 180.0f) + 1.0f) * 0.5f;
            float hue = base_hue + sinf((float)i * 0.02f) * 15.0f;
            rgb_t c = hsv_to_rgb(hue, 0.9f, brightness * 1.0f);
            ws2812_write(c.r, c.g, c.b);
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        base_hue += 60.0f;
    }
}

static void effect_strobe(void)
{
    /* Multi-color strobe flash */
    rgb_t colors[] = {
        {255, 0, 0}, {0, 255, 0}, {0, 0, 255},
        {255, 255, 0}, {0, 255, 255}, {255, 0, 255},
        {255, 255, 255},
    };
    for (int round = 0; round < 5; round++) {
        for (int i = 0; i < sizeof(colors) / sizeof(colors[0]); i++) {
            for (int flash = 0; flash < 3; flash++) {
                ws2812_write(colors[i].r, colors[i].g, colors[i].b);
                vTaskDelay(pdMS_TO_TICKS(40));
                ws2812_write(0, 0, 0);
                vTaskDelay(pdMS_TO_TICKS(40));
            }
        }
    }
}

static void effect_fire(void)
{
    /* Fire-like flickering effect: warm colors with random intensity */
    for (int i = 0; i < 300; i++) {
        float flicker = 0.5f + 0.5f * ((float)(rand() % 100) / 100.0f);
        float hue = 10.0f + ((float)(rand() % 30));
        rgb_t c = hsv_to_rgb(hue, 1.0f, flicker);
        ws2812_write(c.r, c.g, c.b);
        vTaskDelay(pdMS_TO_TICKS(15 + rand() % 30));
    }
}

static void effect_police(void)
{
    /* Red/blue alternating police-style flash */
    for (int i = 0; i < 8; i++) {
        ws2812_write(255, 0, 0);
        vTaskDelay(pdMS_TO_TICKS(60));
        ws2812_write(0, 0, 0);
        vTaskDelay(pdMS_TO_TICKS(30));
        ws2812_write(0, 0, 255);
        vTaskDelay(pdMS_TO_TICKS(60));
        ws2812_write(0, 0, 0);
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}

static void effect_pulse_sweep(void)
{
    /* Fast pulse that sweeps through the spectrum */
    for (int i = 0; i < 360; i++) {
        float hue = (float)i;
        float pulse = (sinf((float)i * 0.3f) > 0.7f) ? 1.0f : 0.1f;
        rgb_t c = hsv_to_rgb(hue, 1.0f, pulse);
        ws2812_write(c.r, c.g, c.b);
        vTaskDelay(pdMS_TO_TICKS(15));
    }
}

static void effect_sunset(void)
{
    /* Slow sunset transition: orange → pink → purple → dark */
    for (int i = 0; i < 360; i++) {
        float hue = 30.0f - (float)i * 0.3f;
        if (hue < -60.0f) hue = -60.0f;
        float brightness = 1.0f - (float)i / 360.0f;
        if (brightness < 0.0f) brightness = 0.0f;
        rgb_t c = hsv_to_rgb(fmodf(hue + 360.0f, 360.0f), 1.0f, brightness);
        ws2812_write(c.r, c.g, c.b);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    /* Fade back */
    for (int i = 0; i < 180; i++) {
        float brightness = (float)i / 180.0f;
        rgb_t c = hsv_to_rgb(200.0f, 0.8f, brightness * 0.3f);
        ws2812_write(c.r, c.g, c.b);
        vTaskDelay(pdMS_TO_TICKS(15));
    }
}

/* ================================================================
   Effect Task
   ================================================================ */

static void rgb_led_task(void *arg)
{
    while (1) {
        effect_rainbow_wave();
        effect_breathing();
        effect_strobe();
        effect_police();
        effect_fire();
        effect_pulse_sweep();
        effect_sunset();
    }
}

void rgb_led_effect_start(void)
{
    xTaskCreate(rgb_led_task, "rgb_effect", 4096, NULL, 5, NULL);
}
