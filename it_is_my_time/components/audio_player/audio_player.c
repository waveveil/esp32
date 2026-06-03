#include "audio_player.h"
#include "display.h"

#include <ctype.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include "driver/gpio.h"
#include "driver/i2s_std.h"

#include "esp_audio_dec_default.h"
#include "esp_audio_simple_dec.h"
#include "esp_audio_simple_dec_default.h"

#define WIFI_SSID "IQOO 12"
#define WIFI_PASS "00000000"

#define STREAM_URL "https://music.solmount.top/rest/stream?u=passer&t=3ee1b8b59dd748b2f2ef4ecc1ef84b93&s=20a4c7&f=json&v=1.8.0&c=NavidromeUI&id=2bBtji5aoGPvnWHjnEF0Wy&_=1780511617491"

#define I2S_BCLK_GPIO 5
#define I2S_LRCK_GPIO 4
#define I2S_DOUT_GPIO 6

#define BTN_UP_GPIO  7   // upper button: vol+
#define BTN_DN_GPIO  13  // lower button: vol-

#define BTN_DEBOUNCE_MS  30
#define BTN_HOLD_MS      400   // long press threshold
#define BTN_REPEAT_MS    80    // repeat rate while held

#define HTTP_READ_SIZE 2048
#define OUT_PCM_INIT_SIZE 8192

static const char *TAG = "audio";

static EventGroupHandle_t s_wifi_event_group;
static const int WIFI_CONNECTED_BIT = BIT0;

static i2s_chan_handle_t s_i2s_tx;
static int s_i2s_sample_rate;
static int16_t *s_stereo_buf;
static size_t s_stereo_buf_samples;
static int s_volume = 5;   // 0 - 100
static char s_disp_status[17] = "";
static char s_disp_wifi[17] = "";
static TaskHandle_t s_disp_task;

static void notify_display(void);
static void set_disp_status(const char *msg);
static void set_disp_wifi(const char *msg);

typedef struct {
    char content_type[64];
} http_ctx_t;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    if (evt == NULL || evt->user_data == NULL) {
        return ESP_OK;
    }

    if (evt->event_id == HTTP_EVENT_ON_HEADER && evt->header_key && evt->header_value) {
        http_ctx_t *ctx = (http_ctx_t *)evt->user_data;
        if (strcasecmp(evt->header_key, "Content-Type") == 0) {
            strncpy(ctx->content_type, evt->header_value, sizeof(ctx->content_type) - 1);
            ctx->content_type[sizeof(ctx->content_type) - 1] = '\0';
        }
    }

    return ESP_OK;
}

static bool string_contains_ci(const char *haystack, const char *needle)
{
    if (haystack == NULL || needle == NULL) {
        return false;
    }

    size_t needle_len = strlen(needle);
    if (needle_len == 0) {
        return false;
    }

    for (const char *p = haystack; *p; p++) {
        if (strncasecmp(p, needle, needle_len) == 0) {
            return true;
        }
    }

    return false;
}

static esp_audio_simple_dec_type_t detect_decoder_type(const char *content_type,
                                                       const uint8_t *data, size_t len)
{
    if (content_type && content_type[0] != '\0') {
        if (string_contains_ci(content_type, "application/json") ||
            string_contains_ci(content_type, "text/")) {
            return ESP_AUDIO_SIMPLE_DEC_TYPE_NONE;
        }
        if (string_contains_ci(content_type, "audio/mpeg") ||
            string_contains_ci(content_type, "audio/mp3")) {
            return ESP_AUDIO_SIMPLE_DEC_TYPE_MP3;
        }
        if (string_contains_ci(content_type, "audio/aac") ||
            string_contains_ci(content_type, "audio/aacp")) {
            return ESP_AUDIO_SIMPLE_DEC_TYPE_AAC;
        }
        if (string_contains_ci(content_type, "audio/flac")) {
            return ESP_AUDIO_SIMPLE_DEC_TYPE_FLAC;
        }
        if (string_contains_ci(content_type, "audio/ogg") ||
            string_contains_ci(content_type, "application/ogg")) {
            return ESP_AUDIO_SIMPLE_DEC_TYPE_OGG;
        }
        if (string_contains_ci(content_type, "audio/wav") ||
            string_contains_ci(content_type, "audio/x-wav")) {
            return ESP_AUDIO_SIMPLE_DEC_TYPE_WAV;
        }
        if (string_contains_ci(content_type, "audio/mp4") ||
            string_contains_ci(content_type, "audio/m4a") ||
            string_contains_ci(content_type, "video/mp4")) {
            return ESP_AUDIO_SIMPLE_DEC_TYPE_M4A;
        }
    }

    for (size_t i = 0; i < len; i++) {
        if (!isspace((int)data[i])) {
            if (data[i] == '{' || data[i] == '[') {
                return ESP_AUDIO_SIMPLE_DEC_TYPE_NONE;
            }
            break;
        }
    }

    if (len >= 3 && memcmp(data, "ID3", 3) == 0) {
        return ESP_AUDIO_SIMPLE_DEC_TYPE_MP3;
    }
    if (len >= 4 && memcmp(data, "fLaC", 4) == 0) {
        return ESP_AUDIO_SIMPLE_DEC_TYPE_FLAC;
    }
    if (len >= 4 && memcmp(data, "OggS", 4) == 0) {
        return ESP_AUDIO_SIMPLE_DEC_TYPE_OGG;
    }
    if (len >= 12 && memcmp(data, "RIFF", 4) == 0 && memcmp(data + 8, "WAVE", 4) == 0) {
        return ESP_AUDIO_SIMPLE_DEC_TYPE_WAV;
    }
    if (len >= 8 && memcmp(data + 4, "ftyp", 4) == 0) {
        return ESP_AUDIO_SIMPLE_DEC_TYPE_M4A;
    }
    if (len >= 2 && data[0] == 0xFF && (data[1] & 0xE0) == 0xE0) {
        return ESP_AUDIO_SIMPLE_DEC_TYPE_MP3;
    }

    return ESP_AUDIO_SIMPLE_DEC_TYPE_NONE;
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static esp_err_t wifi_init_sta(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        return ret;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    s_wifi_event_group = xEventGroupCreate();
    if (s_wifi_event_group == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler, NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler, NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid));
    strncpy((char *)wifi_config.sta.password, WIFI_PASS, sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    return ESP_OK;
}

static esp_err_t i2s_setup(uint32_t sample_rate)
{
    if (s_i2s_tx == NULL) {
        i2s_chan_config_t chan_cfg = {
            .id = I2S_NUM_0,
            .role = I2S_ROLE_MASTER,
            .dma_desc_num = 8,
            .dma_frame_num = 480,
            .auto_clear = false,
        };
        ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &s_i2s_tx, NULL));

        i2s_std_config_t std_cfg = {
            .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate),
            .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
            .gpio_cfg = {
                .mclk = I2S_GPIO_UNUSED,
                .bclk = I2S_BCLK_GPIO,
                .ws = I2S_LRCK_GPIO,
                .dout = I2S_DOUT_GPIO,
                .din = I2S_GPIO_UNUSED,
                .invert_flags = {
                    .mclk_inv = false,
                    .bclk_inv = false,
                    .ws_inv = false,
                },
            },
        };

        ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_i2s_tx, &std_cfg));
        ESP_ERROR_CHECK(i2s_channel_enable(s_i2s_tx));
        s_i2s_sample_rate = (int)sample_rate;
        return ESP_OK;
    }

    if (s_i2s_sample_rate != (int)sample_rate) {
        i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate);
        ESP_ERROR_CHECK(i2s_channel_reconfig_std_clock(s_i2s_tx, &clk_cfg));
        s_i2s_sample_rate = (int)sample_rate;
    }

    return ESP_OK;
}

static void i2s_write_pcm(uint8_t *pcm, size_t size, uint8_t channels)
{
    size_t bytes_written = 0;
    size_t samples = size / sizeof(int16_t);
    int16_t *buf = (int16_t *)pcm;

    if (channels == 1) {
        // Mono: expand to stereo in separate buffer
        if (samples * 2 > s_stereo_buf_samples) {
            int16_t *new_buf = realloc(s_stereo_buf, samples * 2 * sizeof(int16_t));
            if (new_buf == NULL) return;
            s_stereo_buf = new_buf;
            s_stereo_buf_samples = samples * 2;
        }

        int vol = s_volume;
        for (size_t i = 0; i < samples; i++) {
            int16_t s = (int16_t)((int32_t)buf[i] * vol / 100);
            s_stereo_buf[i * 2] = s;
            s_stereo_buf[i * 2 + 1] = s;
        }
        i2s_channel_write(s_i2s_tx, s_stereo_buf,
                          samples * 2 * sizeof(int16_t),
                          &bytes_written, portMAX_DELAY);
        return;
    }

    // Stereo: scale in-place, no extra copy
    int vol = s_volume;
    for (size_t i = 0; i < samples; i++) {
        buf[i] = (int16_t)((int32_t)buf[i] * vol / 100);
    }
    i2s_channel_write(s_i2s_tx, buf, size, &bytes_written, portMAX_DELAY);
}

static void audio_stream_task(void *arg)
{
    ESP_LOGI(TAG, "Registering decoders");
    ESP_ERROR_CHECK(esp_audio_dec_register_default());
    ESP_ERROR_CHECK(esp_audio_simple_dec_register_default());

    uint8_t *in_buf = malloc(HTTP_READ_SIZE);
    uint8_t *out_buf = malloc(OUT_PCM_INIT_SIZE);
    size_t out_size = OUT_PCM_INIT_SIZE;

    if (in_buf == NULL || out_buf == NULL) {
        ESP_LOGE(TAG, "Buffer alloc failed");
        free(in_buf);
        free(out_buf);
        vTaskDelete(NULL);
        return;
    }

    for (;;) {
        http_ctx_t http_ctx = {0};
        esp_http_client_config_t config = {
            .url = STREAM_URL,
            .timeout_ms = 10000,
            .buffer_size = HTTP_READ_SIZE,
            .user_agent = "esp32-mp3",
            .crt_bundle_attach = esp_crt_bundle_attach,
            .event_handler = http_event_handler,
            .user_data = &http_ctx,
        };

        esp_http_client_handle_t client = esp_http_client_init(&config);
        if (client == NULL) {
            ESP_LOGE(TAG, "HTTP client init failed");
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        esp_err_t err = esp_http_client_open(client, 0);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "HTTP open failed: %s", esp_err_to_name(err));
            esp_http_client_cleanup(client);
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        esp_http_client_fetch_headers(client);
        ESP_LOGI(TAG, "Streaming from %s", STREAM_URL);
        set_disp_status("  Streaming...");

        int status = esp_http_client_get_status_code(client);
        if (status < 200 || status >= 300) {
            ESP_LOGE(TAG, "HTTP status %d", status);
            goto stream_end;
        }

        ESP_LOGI(TAG, "Content-Type: %s", http_ctx.content_type[0] ? http_ctx.content_type : "unknown");

        int read = esp_http_client_read(client, (char *)in_buf, HTTP_READ_SIZE);
        if (read <= 0) {
            goto stream_end;
        }

        esp_audio_simple_dec_type_t dec_type = detect_decoder_type(http_ctx.content_type, in_buf, (size_t)read);
        if (dec_type == ESP_AUDIO_SIMPLE_DEC_TYPE_NONE) {
            ESP_LOGE(TAG, "Unsupported or non-audio response");
            goto stream_end;
        }

        ESP_LOGI(TAG, "Decoder: %s", esp_audio_simple_dec_get_name(dec_type));

        esp_audio_simple_dec_cfg_t dec_cfg = {
            .dec_type = dec_type,
            .dec_cfg = NULL,
            .cfg_size = 0,
            .use_frame_dec = false,
        };

        esp_audio_simple_dec_handle_t dec_handle = NULL;
        esp_audio_err_t dec_ret = esp_audio_simple_dec_open(&dec_cfg, &dec_handle);
        if (dec_ret != ESP_AUDIO_ERR_OK) {
            ESP_LOGE(TAG, "Decoder open failed: %d", dec_ret);
            goto stream_end;
        }

        esp_audio_simple_dec_info_t dec_info = {0};
        bool info_ready = false;
        bool use_prefetch = true;

        while (1) {
            if (!use_prefetch) {
                read = esp_http_client_read(client, (char *)in_buf, HTTP_READ_SIZE);
            } else {
                use_prefetch = false;
            }
            if (read <= 0) {
                break;
            }

            esp_audio_simple_dec_raw_t raw = {
                .buffer = in_buf,
                .len = (uint32_t)read,
                .eos = false,
            };

            while (raw.len > 0) {
                esp_audio_simple_dec_out_t out = {
                    .buffer = out_buf,
                    .len = (uint32_t)out_size,
                };

                dec_ret = esp_audio_simple_dec_process(dec_handle, &raw, &out);
                if (dec_ret == ESP_AUDIO_ERR_BUFF_NOT_ENOUGH) {
                    uint8_t *new_buf = realloc(out_buf, out.needed_size);
                    if (new_buf == NULL) {
                        ESP_LOGE(TAG, "PCM buffer realloc failed");
                        goto stream_end;
                    }
                    out_buf = new_buf;
                    out_size = out.needed_size;
                    continue;
                }

                if (dec_ret != ESP_AUDIO_ERR_OK) {
                    ESP_LOGE(TAG, "Decode error: %d", dec_ret);
                    goto stream_end;
                }

                if (out.decoded_size > 0) {
                    if (!info_ready) {
                        if (esp_audio_simple_dec_get_info(dec_handle, &dec_info) == ESP_AUDIO_ERR_OK) {
                            if (dec_info.bits_per_sample != 16) {
                                ESP_LOGE(TAG, "Unsupported bits per sample: %u", dec_info.bits_per_sample);
                                goto stream_end;
                            }
                            ESP_ERROR_CHECK(i2s_setup(dec_info.sample_rate));
                            info_ready = true;
                            ESP_LOGI(TAG, "Audio info: %u Hz, %u ch", dec_info.sample_rate, dec_info.channel);
                        }
                    }

                    if (info_ready) {
                        i2s_write_pcm(out_buf, out.decoded_size, dec_info.channel);
                    }
                }

                raw.len -= raw.consumed;
                raw.buffer += raw.consumed;
            }
        }

stream_end:
        if (dec_handle != NULL) {
            esp_audio_simple_dec_close(dec_handle);
            dec_handle = NULL;
        }
        esp_http_client_close(client);
        esp_http_client_cleanup(client);

        ESP_LOGI(TAG, "Stream ended, reconnecting...");
        set_disp_status("  Reconnecting...");
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

// Display task: sole owner of I2C/display operations, avoids concurrency issues
static void display_update_task(void *arg)
{
    while (1) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));

        int vol = s_volume;

        char buf[17];
        snprintf(buf, sizeof(buf), "Vol: %3d%%", vol);
        display_set_line(0, buf);

        // text-based volume bar (16 chars wide)
        char bar_text[17];
        int filled = vol * 16 / 100;
        for (int i = 0; i < 16; i++) bar_text[i] = (i < filled) ? '#' : '-';
        bar_text[16] = '\0';
        display_set_line(1, bar_text);

        display_set_line(3, s_disp_status);
        display_set_line(6, s_disp_wifi);
    }
}

static void notify_display(void)
{
    if (s_disp_task) {
        xTaskNotifyGive(s_disp_task);
    }
}

// Update status from any task — thread-safe, no I2C blocking
static void set_disp_status(const char *msg)
{
    strncpy(s_disp_status, msg, 16);
    s_disp_status[16] = '\0';
    notify_display();
}

static void set_disp_wifi(const char *msg)
{
    strncpy(s_disp_wifi, msg, 16);
    s_disp_wifi[16] = '\0';
    notify_display();
}

static void button_task(void *arg)
{
    int last_up = 1, last_dn = 1;
    int hold_up = 0, hold_dn = 0;

    gpio_config_t io_cfg = {
        .pin_bit_mask = (1ULL << BTN_UP_GPIO) | (1ULL << BTN_DN_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_cfg);

    while (1) {
        int up = gpio_get_level(BTN_UP_GPIO);
        int dn = gpio_get_level(BTN_DN_GPIO);

        if (up == 0) {
            if (last_up == 1) {
                s_volume = (s_volume + 5 > 100) ? 100 : s_volume + 5;
                notify_display();
                ESP_LOGI(TAG, "Volume: %d%%", s_volume);
                hold_up = 0;
            } else if (hold_up >= BTN_HOLD_MS / BTN_DEBOUNCE_MS) {
                if (hold_up % (BTN_REPEAT_MS / BTN_DEBOUNCE_MS) == 0) {
                    s_volume = (s_volume + 5 > 100) ? 100 : s_volume + 5;
                    notify_display();
                    ESP_LOGI(TAG, "Volume: %d%%", s_volume);
                }
            }
            hold_up++;
        } else {
            hold_up = 0;
        }

        if (dn == 0) {
            if (last_dn == 1) {
                s_volume = (s_volume - 5 < 0) ? 0 : s_volume - 5;
                notify_display();
                ESP_LOGI(TAG, "Volume: %d%%", s_volume);
                hold_dn = 0;
            } else if (hold_dn >= BTN_HOLD_MS / BTN_DEBOUNCE_MS) {
                if (hold_dn % (BTN_REPEAT_MS / BTN_DEBOUNCE_MS) == 0) {
                    s_volume = (s_volume - 5 < 0) ? 0 : s_volume - 5;
                    notify_display();
                    ESP_LOGI(TAG, "Volume: %d%%", s_volume);
                }
            }
            hold_dn++;
        } else {
            hold_dn = 0;
        }

        last_up = up;
        last_dn = dn;
        vTaskDelay(pdMS_TO_TICKS(BTN_DEBOUNCE_MS));
    }
}

void audio_player_set_volume(int volume)
{
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;
    s_volume = volume;
    notify_display();
    ESP_LOGI(TAG, "Volume: %d%%", volume);
}

void audio_player_start(void)
{
    esp_err_t ret = wifi_init_sta();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi init failed: %s", esp_err_to_name(ret));
        return;
    }

    display_init();
    set_disp_wifi("  WiFi: Connecting");

    // Start display task before it's notified
    xTaskCreate(display_update_task, "display_upd", 2560, NULL, 5, &s_disp_task);

    ESP_LOGI(TAG, "Connecting Wi-Fi...");
    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
    ESP_LOGI(TAG, "Wi-Fi connected");
    set_disp_wifi("  WiFi: Connected ");

    xTaskCreate(audio_stream_task, "audio_stream", 16384, NULL, 20, NULL);
    xTaskCreate(button_task, "button", 2560, NULL, 10, NULL);
}
