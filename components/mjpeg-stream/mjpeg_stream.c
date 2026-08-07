#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"

#include "esp_jpeg_enc.h"

#include "sdkconfig.h"

#include "mjpeg_stream.h"

static const char *TAG = "mjpeg_stream";

#define PART_BOUNDARY "probingedd"
static const char STREAM_CONTENT_TYPE[] = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char STREAM_PART_HEADER[] = "\r\n--" PART_BOUNDARY "\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

static const char INDEX_HTML[] =
    "<!DOCTYPE html><html><head><title>probingedd</title>"
    "<style>body{background:#111;margin:0;display:flex;align-items:center;"
    "justify-content:center;height:100vh}img{max-width:100%;max-height:100%;"
    "image-rendering:pixelated}</style></head>"
    "<body><img src=\"/stream\"></body></html>";

static SemaphoreHandle_t s_mutex;
static jpeg_enc_handle_t s_encoder;
static int s_encoder_w, s_encoder_h;
static uint8_t *s_jpeg_buf;
static size_t s_jpeg_cap;
static size_t s_jpeg_len;
static volatile uint32_t s_frame_gen;

static void wifi_ap_init(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = {0};
    strlcpy((char *)wifi_config.ap.ssid, CONFIG_PROBINGEDD_WIFI_AP_SSID, sizeof(wifi_config.ap.ssid));
    wifi_config.ap.ssid_len = strlen(CONFIG_PROBINGEDD_WIFI_AP_SSID);
    wifi_config.ap.channel = CONFIG_PROBINGEDD_WIFI_AP_CHANNEL;
    wifi_config.ap.max_connection = 4;
    strlcpy((char *)wifi_config.ap.password, CONFIG_PROBINGEDD_WIFI_AP_PASSWORD, sizeof(wifi_config.ap.password));
    wifi_config.ap.authmode = (strlen(CONFIG_PROBINGEDD_WIFI_AP_PASSWORD) == 0) ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "AP \"%s\" up, browse to http://192.168.4.1/ to watch", CONFIG_PROBINGEDD_WIFI_AP_SSID);
}

static esp_err_t index_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, INDEX_HTML, sizeof(INDEX_HTML) - 1);
}

// Waits for the next published frame, copies it out under the lock, and
// hands it back for the caller to send *without* holding the lock - a slow
// client socket must never stall mjpeg_stream_push_frame().
static esp_err_t copy_latest_frame(uint32_t *last_gen, uint8_t *out_buf, size_t out_cap, size_t *out_len) {
    while (s_frame_gen == *last_gen) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    *last_gen = s_frame_gen;
    if (s_jpeg_len == 0 || s_jpeg_len > out_cap) {
        xSemaphoreGive(s_mutex);
        return ESP_FAIL;
    }
    memcpy(out_buf, s_jpeg_buf, s_jpeg_len);
    *out_len = s_jpeg_len;
    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

static esp_err_t stream_handler(httpd_req_t *req) {
    esp_err_t res = httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
    if (res != ESP_OK) {
        return res;
    }

    uint8_t *frame_buf = heap_caps_malloc(CONFIG_PROBINGEDD_STREAM_MAX_JPEG_BYTES, MALLOC_CAP_SPIRAM);
    if (frame_buf == NULL) {
        return ESP_ERR_NO_MEM;
    }

    char header[64];
    uint32_t last_gen = 0;
    while (1) {
        size_t len = 0;
        if (copy_latest_frame(&last_gen, frame_buf, CONFIG_PROBINGEDD_STREAM_MAX_JPEG_BYTES, &len) != ESP_OK) {
            continue;
        }

        int header_len = snprintf(header, sizeof(header), STREAM_PART_HEADER, (unsigned)len);
        if (httpd_resp_send_chunk(req, header, header_len) != ESP_OK) break;
        if (httpd_resp_send_chunk(req, (const char *)frame_buf, len) != ESP_OK) break;
    }

    heap_caps_free(frame_buf);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t snapshot_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "image/jpeg");
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    esp_err_t res;
    if (s_jpeg_len == 0) {
        res = httpd_resp_send_500(req);
    } else {
        res = httpd_resp_send(req, (const char *)s_jpeg_buf, s_jpeg_len);
    }
    xSemaphoreGive(s_mutex);
    return res;
}

static void start_http_server(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;
    config.max_open_sockets = 4;
    config.lru_purge_enable = true;

    httpd_handle_t server = NULL;
    ESP_ERROR_CHECK(httpd_start(&server, &config));

    const httpd_uri_t index_uri = {.uri = "/", .method = HTTP_GET, .handler = index_handler};
    const httpd_uri_t stream_uri = {.uri = "/stream", .method = HTTP_GET, .handler = stream_handler};
    const httpd_uri_t snapshot_uri = {.uri = "/snapshot", .method = HTTP_GET, .handler = snapshot_handler};
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &index_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &stream_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &snapshot_uri));
}

void mjpeg_stream_init(void) {
    s_mutex = xSemaphoreCreateMutex();
    s_jpeg_cap = CONFIG_PROBINGEDD_STREAM_MAX_JPEG_BYTES;
    s_jpeg_buf = heap_caps_malloc(s_jpeg_cap, MALLOC_CAP_SPIRAM);
    if (s_jpeg_buf == NULL) {
        ESP_LOGE(TAG, "failed to allocate %u byte JPEG buffer in PSRAM", (unsigned)s_jpeg_cap);
        abort();
    }

    wifi_ap_init();
    start_http_server();
}

static bool ensure_encoder(int width, int height) {
    if (s_encoder != NULL && s_encoder_w == width && s_encoder_h == height) {
        return true;
    }
    if (s_encoder != NULL) {
        jpeg_enc_close(s_encoder);
        s_encoder = NULL;
    }

    jpeg_enc_config_t cfg = DEFAULT_JPEG_ENC_CONFIG();
    cfg.width = width;
    cfg.height = height;
    cfg.src_type = JPEG_PIXEL_FORMAT_RGB565_LE;
    cfg.subsampling = JPEG_SUBSAMPLE_420;
    cfg.quality = CONFIG_PROBINGEDD_STREAM_JPEG_QUALITY;

    if (jpeg_enc_open(&cfg, &s_encoder) != JPEG_ERR_OK) {
        ESP_LOGE(TAG, "jpeg_enc_open(%dx%d) failed", width, height);
        s_encoder = NULL;
        return false;
    }
    s_encoder_w = width;
    s_encoder_h = height;
    return true;
}

void mjpeg_stream_push_frame(const uint16_t *rgb565, int width, int height) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);

    if (!ensure_encoder(width, height)) {
        xSemaphoreGive(s_mutex);
        return;
    }

    int out_size = 0;
    jpeg_error_t err = jpeg_enc_process(s_encoder, (const uint8_t *)rgb565, width * height * 2, s_jpeg_buf,
                                         (int)s_jpeg_cap, &out_size);
    if (err == JPEG_ERR_OK) {
        s_jpeg_len = out_size;
        s_frame_gen++;
    } else {
        ESP_LOGW(TAG, "jpeg_enc_process failed: %d", err);
    }

    xSemaphoreGive(s_mutex);
}
