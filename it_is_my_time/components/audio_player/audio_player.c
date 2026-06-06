#include "audio_player.h"
#include "display.h"

#include <ctype.h>
#include <stdbool.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include "driver/gpio.h"
#include "driver/i2s_std.h"

#include "esp_audio_dec_default.h"
#include "esp_audio_simple_dec.h"
#include "esp_audio_simple_dec_default.h"

#define WIFI_SSID "IQOO 12"
#define WIFI_PASS "00000000"

#define STREAM_URL "https://music.solmount.top/rest/stream?u=passer&t=8f0f94fdd184b26c8a1a6a5cd8502240&s=e3584f&f=json&v=1.8.0&c=NavidromeUI&id=RUbs5jDJOIvKK2GGeftwzt&_=1780762872116"

#define I2S_BCLK_GPIO 5
#define I2S_LRCK_GPIO 4
#define I2S_DOUT_GPIO 6

#define BTN_UP_GPIO  7   // upper button: vol+
#define BTN_DN_GPIO  13  // lower button: vol-
#define BTN_PP_GPIO  0   // boot button: play/pause

#define BTN_DEBOUNCE_MS  30
#define BTN_HOLD_MS      400   // long press threshold
#define BTN_REPEAT_MS    40    // repeat rate while held

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
static bool s_paused = false;
static char s_disp_status[17] = "";
static char s_disp_wifi[17] = "";
static char s_disp_time[24] = "";
static int s_elapsed_sec = 0;
static int s_total_sec = 0;   // 0 = unknown
static int64_t s_play_start_us;
static TaskHandle_t s_disp_task;
static TaskHandle_t s_audio_task;
static volatile int s_audio_heartbeat;       // updated by audio task every ~read cycle
static volatile int s_diag_elapsed_at_log;   // snapshot for display
static volatile int s_diag_stack_free;       // stack bytes free at last health check

// --- resume playback ---
static uint8_t *s_head_buf;          // saved OGG header bytes for decoder re-init
static size_t   s_head_len;          // actual header bytes saved
static bool     s_head_saved;        // true once headers captured
static int64_t  s_bytes_read;        // HTTP bytes read from current connection
static int64_t  s_stream_offset;     // absolute file byte offset where current connection starts
static int64_t  s_resume_byte;       // >=0 = resume from this byte on reconnect
static int      s_resume_elapsed;    // elapsed_sec snapshot at disconnect
static int      s_total_cached;      // original total_sec (206 changes Content-Length)
static int      s_sync_skip;         // >0 = discard chunks during post-resume OGG sync

static void notify_display(void);
static void set_disp_status(const char *msg);
static void set_disp_wifi(const char *msg);

typedef struct {
    char content_type[64];
    int64_t content_length;   // -1 = unknown
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
        } else if (strcasecmp(evt->header_key, "Content-Length") == 0) {
            ctx->content_length = strtoll(evt->header_value, NULL, 10);
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
            .dma_desc_num = 16,
            .dma_frame_num = 480,
            .auto_clear = true,
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

    if (s_paused) {
        // Write silence instead of real PCM
        for (size_t i = 0; i < samples; i++) {
            buf[i] = 0;
        }
    }

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
    s_audio_task = xTaskGetCurrentTaskHandle();

    ESP_LOGI(TAG, "Registering decoders");
    ESP_ERROR_CHECK(esp_audio_dec_register_default());
    ESP_ERROR_CHECK(esp_audio_simple_dec_register_default());

    // Suppress internal decoder noise (expected during resume sync)
    esp_log_level_set("ESP_OPUS_DEC", ESP_LOG_NONE);

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
        http_ctx_t http_ctx = { .content_length = -1 };
        esp_http_client_config_t config = {
            .url = STREAM_URL,
            .timeout_ms = 8000,
            .buffer_size = HTTP_READ_SIZE,
            .user_agent = "esp32-mp3",
            .crt_bundle_attach = esp_crt_bundle_attach,
            .event_handler = http_event_handler,
            .user_data = &http_ctx,
            .keep_alive_enable = true,
            .keep_alive_idle = 15,
            .keep_alive_interval = 5,
            .keep_alive_count = 3,
        };

        esp_http_client_handle_t client = esp_http_client_init(&config);
        if (client == NULL) {
            ESP_LOGE(TAG, "HTTP client init failed");
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        bool is_resume = (s_resume_byte >= 0);
        if (is_resume) {
            char range[48];
            snprintf(range, sizeof(range), "bytes=%lld-", (long long)s_resume_byte);
            esp_http_client_set_header(client, "Range", range);
            s_stream_offset = s_resume_byte;
            ESP_LOGI(TAG, "Range: %s (offset=%lld)", range, (long long)s_stream_offset);
        } else {
            s_stream_offset = 0;
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
        if (is_resume) {
            if (status == 206) {
                ESP_LOGI(TAG, "Range accepted (206), resuming");
                set_disp_status("  Resuming...   ");
            } else if (status == 200) {
                ESP_LOGW(TAG, "Range ignored (got 200), restarting from beginning");
                s_resume_byte = -1;
                s_resume_elapsed = 0;
                s_total_cached = 0;
                is_resume = false;
                s_head_saved = false;
                s_head_len = 0;
            } else if (status == 416) {
                ESP_LOGW(TAG, "Range not satisfiable (416), restarting from beginning");
                s_resume_byte = -1;
                s_resume_elapsed = 0;
                s_total_cached = 0;
                is_resume = false;
                s_head_saved = false;
                s_head_len = 0;
                esp_http_client_close(client);
                esp_http_client_cleanup(client);
                vTaskDelay(pdMS_TO_TICKS(500));
                continue;
            }
        }
        if (status < 200 || status >= 300) {
            ESP_LOGE(TAG, "HTTP status %d", status);
            goto stream_end;
        }

        ESP_LOGI(TAG, "Content-Type: %s", http_ctx.content_type[0] ? http_ctx.content_type : "unknown");

        int read = esp_http_client_read(client, (char *)in_buf, HTTP_READ_SIZE);
        if (read <= 0) {
            goto stream_end;
        }
        s_bytes_read += read;

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

        // --- resume: feed saved headers to initialize decoder ---
        if (is_resume && s_head_buf != NULL && s_head_len > 0) {
            ESP_LOGI(TAG, "Feeding %u bytes of saved headers", (unsigned)s_head_len);
            uint8_t *hp = s_head_buf;
            size_t hr = s_head_len;
            while (hr > 0) {
                esp_audio_simple_dec_raw_t hraw = {
                    .buffer = hp, .len = (uint32_t)hr, .eos = false,
                };
                esp_audio_simple_dec_out_t hout = {
                    .buffer = out_buf, .len = (uint32_t)out_size,
                };
                dec_ret = esp_audio_simple_dec_process(dec_handle, &hraw, &hout);
                if (dec_ret == ESP_AUDIO_ERR_BUFF_NOT_ENOUGH) {
                    uint8_t *nb = realloc(out_buf, hout.needed_size);
                    if (nb == NULL) goto stream_end;
                    out_buf = nb;
                    out_size = hout.needed_size;
                    continue;
                }
                if (dec_ret != ESP_AUDIO_ERR_OK) {
                    ESP_LOGE(TAG, "Header feed error: %d", dec_ret);
                    goto stream_end;
                }
                hp += hraw.consumed;
                hr -= hraw.consumed;
                if (hout.decoded_size > 0 && !info_ready) {
                    if (esp_audio_simple_dec_get_info(dec_handle, &dec_info) == ESP_AUDIO_ERR_OK) {
                        if (dec_info.bits_per_sample != 16) goto stream_end;
                        ESP_ERROR_CHECK(i2s_setup(dec_info.sample_rate));
                        info_ready = true;
                        s_elapsed_sec = s_resume_elapsed;
                        s_play_start_us = esp_timer_get_time() - (int64_t)s_resume_elapsed * 1000000LL;
                        s_total_sec = s_total_cached;
                        s_sync_skip = 20;
                        uint8_t *silence = calloc(1, 1152 * 2 * 2);
                        if (silence) { i2s_write_pcm(silence, 1152 * 2 * 2, 2); free(silence); }
                        ESP_LOGI(TAG, "Resume: decoder ready, elapsed=%d:%02d",
                                 s_resume_elapsed / 60, s_resume_elapsed % 60);
                    }
                }
                // Discard PCM from header replay (don't write to I2S)
            }
            is_resume = false;
            s_resume_byte = -1;      // consumed — will be recalculated on next disconnect
            use_prefetch = true;     // use the prefetch data already in in_buf
            ESP_LOGI(TAG, "Header feed done, switching to ranged stream");
        }

        while (1) {
            if (!use_prefetch) {
                read = esp_http_client_read(client, (char *)in_buf, HTTP_READ_SIZE);
            } else {
                use_prefetch = false;
            }
            if (read <= 0) {
                break;
            }

            s_bytes_read += read;

            // --- save stream header for future resume ---
            if (!s_head_saved && s_head_len < 32768) {
                if (s_head_buf == NULL) {
                    s_head_buf = malloc(32768);
                }
                if (s_head_buf != NULL) {
                    size_t room = 32768 - s_head_len;
                    size_t n = (size_t)read > room ? room : (size_t)read;
                    memcpy(s_head_buf + s_head_len, in_buf, n);
                    s_head_len += n;
                    if (s_head_len >= 32768) {
                        s_head_saved = true;
                        ESP_LOGI(TAG, "Header buffer full: %u bytes", (unsigned)s_head_len);
                    }
                } else {
                    s_head_saved = true;
                }
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
                    if (s_sync_skip > 0) {
                        // Resuming: mid-OGG-page data, discard and scan for next sync
                        s_sync_skip--;
                        raw.buffer += raw.len;
                        raw.len = 0;
                        break;
                    }
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

                            if (!s_head_saved) {
                                ESP_LOGI(TAG, "Decoder init, header so far: %u bytes", (unsigned)s_head_len);
                            }

                            if (s_resume_byte >= 0) {
                                // Resume: continue elapsed time from where we left off
                                s_elapsed_sec = s_resume_elapsed;
                                s_play_start_us = esp_timer_get_time() - (int64_t)s_resume_elapsed * 1000000LL;
                                s_total_sec = s_total_cached;
                                s_sync_skip = 20; // discard chunks until OGG page sync
                                ESP_LOGI(TAG, "Resuming from ~%d:%02d", s_resume_elapsed / 60, s_resume_elapsed % 60);
                            } else {
                                s_elapsed_sec = 0;
                                s_play_start_us = esp_timer_get_time();
                                if (http_ctx.content_length > 0 && dec_info.bitrate > 0) {
                                    s_total_sec = (int)((http_ctx.content_length * 8) / dec_info.bitrate);
                                    if (s_total_sec > 359999) s_total_sec = 0;
                                } else {
                                    s_total_sec = 0;
                                }
                                s_total_cached = s_total_sec;
                            }
                            // Prime I2S pipeline with silence to avoid startup transient
                            {
                                uint8_t *silence = calloc(1, 1152 * 2 * 2);  // stereo 16-bit
                                if (silence) {
                                    i2s_write_pcm(silence, 1152 * 2 * 2, 2);
                                    free(silence);
                                }
                            }
                            ESP_LOGI(TAG, "Audio info: %u Hz, %u ch, %d kbps",
                                     dec_info.sample_rate, dec_info.channel,
                                     (int)(dec_info.bitrate / 1000));
                        }
                    }

                    if (info_ready) {
                        i2s_write_pcm(out_buf, out.decoded_size, dec_info.channel);
                    }
                }

                raw.len -= raw.consumed;
                raw.buffer += raw.consumed;
            }

            // Update elapsed time from timer
            if (info_ready) {
                s_elapsed_sec = (int)((esp_timer_get_time() - s_play_start_us) / 1000000);
            }

            // --- health monitor: log every 15s ---
            s_audio_heartbeat++;
            if ((s_audio_heartbeat & 0x7F) == 0) {  // every ~128 read cycles ≈ 15-20s
                UBaseType_t stack_free = uxTaskGetStackHighWaterMark(NULL);
                int heap_free = esp_get_free_heap_size();
                s_diag_elapsed_at_log = s_elapsed_sec;
                s_diag_stack_free = (int)stack_free;
                ESP_LOGI(TAG, "[health] elapsed=%d:%02d  stack_free=%u  heap_free=%d  heartbeat=%d",
                         s_elapsed_sec / 60, s_elapsed_sec % 60,
                         (unsigned)stack_free, heap_free, s_audio_heartbeat);
            }
        }

        // --- exit reason ---
        ESP_LOGW(TAG, ">>> inner loop exit: read=%d  elapsed=%d:%02d  heartbeat=%d <<<",
                 read, s_elapsed_sec / 60, s_elapsed_sec % 60, s_audio_heartbeat);

stream_end:
        ESP_LOGW(TAG, ">>> stream_end: elapsed=%d:%02d  bytes=%lld  heartbeat=%d <<<",
                 s_elapsed_sec / 60, s_elapsed_sec % 60,
                 (long long)s_bytes_read, s_audio_heartbeat);

        // --- compute resume position ---
        if (s_bytes_read > (int64_t)s_head_len && s_head_buf != NULL && s_head_len > 0) {
            int64_t abs_pos = s_stream_offset + s_bytes_read - 16384;
            if (abs_pos < (int64_t)s_head_len) abs_pos = (int64_t)s_head_len;
            s_resume_byte = abs_pos;
            s_head_saved = true;  // block re-accumulation during resume connection
            s_resume_elapsed = s_elapsed_sec;
            s_total_cached = s_total_sec > 0 ? s_total_sec : s_total_cached;
            ESP_LOGI(TAG, ">>> will resume from byte %lld (elapsed=%d:%02d) <<<",
                     (long long)s_resume_byte,
                     s_resume_elapsed / 60, s_resume_elapsed % 60);
        } else {
            s_resume_byte = -1;
            s_resume_elapsed = 0;
            s_total_cached = 0;
            if (s_head_buf != NULL) {
                free(s_head_buf);
                s_head_buf = NULL;
            }
            s_head_len = 0;
            s_head_saved = false;
        }
        s_sync_skip = 0;
        s_bytes_read = 0;
        s_elapsed_sec = 0;
        s_total_sec = 0;
        s_play_start_us = 0;
        s_paused = false;
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

        // progress bar (line 2)
        char prog_bar[17];
        if (s_total_sec > 0) {
            int p = s_elapsed_sec * 16 / s_total_sec;
            if (p > 16) p = 16;
            for (int i = 0; i < 16; i++) prog_bar[i] = (i < p) ? '=' : '-';
        } else {
            // Moving dot when duration unknown
            int dot = (s_elapsed_sec / 5) % 16;
            for (int i = 0; i < 16; i++) prog_bar[i] = (i == dot) ? '.' : '-';
        }
        prog_bar[16] = '\0';
        display_set_line(2, prog_bar);

        // Update elapsed time string
        int e = s_elapsed_sec;
        if (s_total_sec > 0) {
            int total = s_total_sec;
            if (total > 359999) total = 359999; // clamp to 99:59:59
            snprintf(s_disp_time, sizeof(s_disp_time), "%02d:%02d / %02d:%02d",
                     (e / 60) % 100, e % 60, (total / 60) % 100, total % 60);
        } else {
            snprintf(s_disp_time, sizeof(s_disp_time), "%02d:%02d",
                     (e / 60) % 100, e % 60);
        }
        display_set_line(3, s_disp_time);

        // Status line
        if (s_paused) {
            display_set_line(4, "     Paused     ");
        } else {
            display_set_line(4, s_disp_status);
        }

        // Diagnostic line: stack free + heartbeat
        {
            char diag[17];
            int hb_snapshot = s_audio_heartbeat;
            snprintf(diag, sizeof(diag), "S:%4d H:%04x",
                     s_diag_stack_free, (unsigned)(hb_snapshot & 0xFFFF));
            display_set_line(5, diag);
        }

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
    int last_up = 1, last_dn = 1, last_pp = 1;
    int hold_up = 0, hold_dn = 0;

    gpio_config_t io_cfg = {
        .pin_bit_mask = (1ULL << BTN_UP_GPIO) | (1ULL << BTN_DN_GPIO) | (1ULL << BTN_PP_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_cfg);

    while (1) {
        int up = gpio_get_level(BTN_UP_GPIO);
        int dn = gpio_get_level(BTN_DN_GPIO);
        int pp = gpio_get_level(BTN_PP_GPIO);

        // Play/Pause toggle (rising edge)
        if (pp == 0 && last_pp == 1) {
            s_paused = !s_paused;
            if (s_paused) {
                ESP_LOGI(TAG, "Paused");
            } else {
                ESP_LOGI(TAG, "Resumed");
            }
            notify_display();
        }
        last_pp = pp;

        if (up == 0) {
            if (last_up == 1) {
                s_volume = (s_volume + 1 > 100) ? 100 : s_volume + 1;
                notify_display();
                ESP_LOGI(TAG, "Volume: %d%%", s_volume);
                hold_up = 0;
            } else if (hold_up >= BTN_HOLD_MS / BTN_DEBOUNCE_MS) {
                if (hold_up % (BTN_REPEAT_MS / BTN_DEBOUNCE_MS) == 0) {
                    s_volume = (s_volume + 1 > 100) ? 100 : s_volume + 1;
                    notify_display();
                }
            }
            hold_up++;
        } else {
            hold_up = 0;
        }

        if (dn == 0) {
            if (last_dn == 1) {
                s_volume = (s_volume - 1 < 0) ? 0 : s_volume - 1;
                notify_display();
                ESP_LOGI(TAG, "Volume: %d%%", s_volume);
                hold_dn = 0;
            } else if (hold_dn >= BTN_HOLD_MS / BTN_DEBOUNCE_MS) {
                if (hold_dn % (BTN_REPEAT_MS / BTN_DEBOUNCE_MS) == 0) {
                    s_volume = (s_volume - 1 < 0) ? 0 : s_volume - 1;
                    notify_display();
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
    // --- boot diagnostics: why did we start? ---
    esp_reset_reason_t reason = esp_reset_reason();
    const char *reason_str = "?";
    switch (reason) {
        case ESP_RST_POWERON:  reason_str = "power-on"; break;
        case ESP_RST_EXT:      reason_str = "ext pin"; break;
        case ESP_RST_SW:       reason_str = "sw reset"; break;
        case ESP_RST_PANIC:    reason_str = "PANIC/exception"; break;
        case ESP_RST_INT_WDT:  reason_str = "int WDT"; break;
        case ESP_RST_TASK_WDT: reason_str = "TASK WDT"; break;
        case ESP_RST_WDT:      reason_str = "other WDT"; break;
        case ESP_RST_DEEPSLEEP: reason_str = "deep sleep wake"; break;
        case ESP_RST_BROWNOUT: reason_str = "brownout"; break;
        case ESP_RST_SDIO:     reason_str = "sdio"; break;
        default: break;
    }
    ESP_LOGW(TAG, ">>> BOOT: reset reason = %s (%d) <<<", reason_str, (int)reason);

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
    esp_wifi_set_ps(WIFI_PS_NONE);
    set_disp_wifi("  WiFi: Connected ");

    xTaskCreate(audio_stream_task, "audio_stream", 16384, NULL, 20, NULL);
    xTaskCreate(button_task, "button", 2560, NULL, 10, NULL);
}
