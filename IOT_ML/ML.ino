/* Edge Impulse Arduino examples - ESP32-CAM AI Thinker + WiFi + BBox serial
 */

#include <ML_IOT_inferencing.h>
#include "edge-impulse-sdk/dsp/image/image.hpp"
#include "esp_camera.h"
#include <WiFi.h>

// ===== WiFi config =====
const char* WIFI_SSID = "YOUR_SSID";      // <- oya SSID
const char* WIFI_PASS = "YOUR_PASSWORD";  // <- oya WiFi password

// ===== Camera model select =====
//#define CAMERA_MODEL_ESP_EYE // Has PSRAM
#define CAMERA_MODEL_AI_THINKER // Has PSRAM

#if defined(CAMERA_MODEL_ESP_EYE)
#define PWDN_GPIO_NUM    -1
#define RESET_GPIO_NUM   -1
#define XCLK_GPIO_NUM    4
#define SIOD_GPIO_NUM    18
#define SIOC_GPIO_NUM    23

#define Y9_GPIO_NUM      36
#define Y8_GPIO_NUM      37
#define Y7_GPIO_NUM      38
#define Y6_GPIO_NUM      39
#define Y5_GPIO_NUM      35
#define Y4_GPIO_NUM      14
#define Y3_GPIO_NUM      13
#define Y2_GPIO_NUM      34
#define VSYNC_GPIO_NUM   5
#define HREF_GPIO_NUM    27
#define PCLK_GPIO_NUM    25

#elif defined(CAMERA_MODEL_AI_THINKER)
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27

#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

#else
#error "Camera model not selected"
#endif

// ===== Camera constants =====
#define EI_CAMERA_RAW_FRAME_BUFFER_COLS  320
#define EI_CAMERA_RAW_FRAME_BUFFER_ROWS  240
#define EI_CAMERA_FRAME_BYTE_SIZE        3   // RGB888

// ===== Globals =====
static bool debug_nn       = false;
static bool is_initialised = false;

// 320x240 RGB888 buffer (JPEG -> RGB)
static uint8_t *snapshot_buf = nullptr;
// Model input size RGB888 buffer (e.g. 96x96)
static uint8_t *resized_buf  = nullptr;

// For bbox info (best detection)
String g_best_label = "none";
float  g_best_score = 0.0f;
int    g_best_cx    = -1;
int    g_best_w     = -1;

// Camera config
static camera_config_t camera_config = {
    .pin_pwdn    = PWDN_GPIO_NUM,
    .pin_reset   = RESET_GPIO_NUM,
    .pin_xclk    = XCLK_GPIO_NUM,
    .pin_sscb_sda= SIOD_GPIO_NUM,
    .pin_sscb_scl= SIOC_GPIO_NUM,

    .pin_d7      = Y9_GPIO_NUM,
    .pin_d6      = Y8_GPIO_NUM,
    .pin_d5      = Y7_GPIO_NUM,
    .pin_d4      = Y6_GPIO_NUM,
    .pin_d3      = Y5_GPIO_NUM,
    .pin_d2      = Y4_GPIO_NUM,
    .pin_d1      = Y3_GPIO_NUM,
    .pin_d0      = Y2_GPIO_NUM,
    .pin_vsync   = VSYNC_GPIO_NUM,
    .pin_href    = HREF_GPIO_NUM,
    .pin_pclk    = PCLK_GPIO_NUM,

    .xclk_freq_hz= 20000000,
    .ledc_timer  = LEDC_TIMER_0,
    .ledc_channel= LEDC_CHANNEL_0,

    .pixel_format= PIXFORMAT_JPEG,
    .frame_size  = FRAMESIZE_QVGA,   // 320x240
    .jpeg_quality= 12,
    .fb_count    = 1,
    .fb_location = CAMERA_FB_IN_PSRAM,
    .grab_mode   = CAMERA_GRAB_WHEN_EMPTY,
};

// Function prototypes
bool ei_camera_init(void);
void ei_camera_deinit(void);
bool ei_camera_capture(uint32_t img_width, uint32_t img_height, uint8_t *out_buf);
static int ei_camera_get_data(size_t offset, size_t length, float *out_ptr);


// ================== SETUP ==================
void setup()
{
    Serial.begin(115200);
    delay(1000);

    ei_printf("Edge Impulse Inferencing Demo\r\n");

    // Camera init
    if (ei_camera_init() == false) {
        ei_printf("Failed to initialize Camera!\r\n");
        while (1) {}
    }
    else {
        ei_printf("Camera initialized\r\n");
    }

    // Allocate buffers ONCE
    snapshot_buf = (uint8_t*)malloc(
        EI_CAMERA_RAW_FRAME_BUFFER_COLS *
        EI_CAMERA_RAW_FRAME_BUFFER_ROWS *
        EI_CAMERA_FRAME_BYTE_SIZE);
    if (!snapshot_buf) {
        ei_printf("ERR: Failed to allocate snapshot_buf!\r\n");
        while (1) {}
    }

    resized_buf = (uint8_t*)malloc(
        EI_CLASSIFIER_INPUT_WIDTH *
        EI_CLASSIFIER_INPUT_HEIGHT *
        EI_CAMERA_FRAME_BYTE_SIZE);
    if (!resized_buf) {
        ei_printf("ERR: Failed to allocate resized_buf!\r\n");
        while (1) {}
    }

    // WiFi connect (simple)
    WiFi.mode(WIFI_STA);
    ei_printf("Connecting WiFi to SSID: %s\r\n", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    int tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries < 40) {
        ei_printf(".");
        delay(500);
        tries++;
    }
    if (WiFi.status() == WL_CONNECTED) {
        ei_printf("\r\nWiFi OK. IP: %s\r\n", WiFi.localIP().toString().c_str());
    } else {
        ei_printf("\r\nWiFi Failed. status=%d\r\n", WiFi.status());
        // thama inference weda karanna puluwan; wifi na kiyalath
    }

    ei_printf("Model input: %dx%d\r\n",
              EI_CLASSIFIER_INPUT_WIDTH,
              EI_CLASSIFIER_INPUT_HEIGHT);

    ei_printf("\nStarting continuous inference in 2 seconds...\n");
    ei_sleep(2000);
}


// ================== LOOP ==================
void loop()
{
    if (ei_sleep(5) != EI_IMPULSE_OK) {
        return;
    }

    // prepare EI signal
    ei::signal_t signal;
    signal.total_length = EI_CLASSIFIER_INPUT_WIDTH * EI_CLASSIFIER_INPUT_HEIGHT;
    signal.get_data = &ei_camera_get_data;

    // Capture into resized_buf (model size RGB888)
    if (ei_camera_capture(
            (uint32_t)EI_CLASSIFIER_INPUT_WIDTH,
            (uint32_t)EI_CLASSIFIER_INPUT_HEIGHT,
            resized_buf) == false) {
        ei_printf("Failed to capture image\r\n");
        return;
    }

    // Run classifier
    ei_impulse_result_t result = { 0 };
    EI_IMPULSE_ERROR err = run_classifier(&signal, &result, debug_nn);
    if (err != EI_IMPULSE_OK) {
        ei_printf("ERR: Failed to run classifier (%d)\n", err);
        return;
    }

    ei_printf("Predictions (DSP: %d ms., Classification: %d ms., Anomaly: %d ms.): \n",
                result.timing.dsp, result.timing.classification, result.timing.anomaly);

#if EI_CLASSIFIER_OBJECT_DETECTION == 1
    // ----- print all boxes as before -----
    ei_printf("Object detection bounding boxes:\r\n");
    for (uint32_t i = 0; i < result.bounding_boxes_count; i++) {
        ei_impulse_result_bounding_box_t bb = result.bounding_boxes[i];
        if (bb.value == 0) continue;
        ei_printf("  %s (%.2f) [ x:%u y:%u w:%u h:%u ]\r\n",
                  bb.label, bb.value, bb.x, bb.y, bb.width, bb.height);
    }

    // ----- find BEST bbox for control (car/arm) -----
    int   best_i = -1;
    float best_v = 0.0f;
    for (uint32_t i = 0; i < result.bounding_boxes_count; i++) {
        auto &bb = result.bounding_boxes[i];
        if (bb.value == 0) continue;

        // mehe labels filter karanna (bottle/cup nam):
        // if (String(bb.label) != "bottle" && String(bb.label) != "cup") continue;

        if (bb.value > best_v) {
            best_v = bb.value;
            best_i = (int)i;
        }
    }

    if (best_i >= 0) {
        auto &bb = result.bounding_boxes[best_i];
        int cx = bb.x + (bb.width / 2);

        g_best_label = String(bb.label);
        g_best_score = bb.value;
        g_best_cx    = cx;
        g_best_w     = bb.width;

        ei_printf("BEST: %s (%.2f) center_x:%d width:%d\r\n",
                  bb.label, bb.value, cx, bb.width);

        // Simple serial line -> passe Nano/car walata parse karanna
        Serial.printf("BOX,cx=%d,w=%d,iw=%d,label=%s,score=%.2f\n",
                      cx,
                      bb.width,
                      (int)EI_CLASSIFIER_INPUT_WIDTH,
                      bb.label,
                      bb.value);
    } else {
        g_best_label = "none";
        g_best_score = 0;
        g_best_cx    = -1;
        g_best_w     = -1;
        ei_printf("No object detected with confidence > 0\r\n");
        Serial.println("BOX,none");
    }

#else   // --- classification model version ---
    ei_printf("Predictions:\r\n");
    int best_i = 0;
    float best_v = result.classification[0].value;
    for (uint16_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
        float v = result.classification[i].value;
        ei_printf("  %s: %.5f\r\n",
                  ei_classifier_inferencing_categories[i], v);
        if (v > best_v) { best_v = v; best_i = i; }
    }
    g_best_label = String(ei_classifier_inferencing_categories[best_i]);
    g_best_score = best_v;
    // classification walata cx/w ne, e nisa -1
    g_best_cx = -1;
    g_best_w  = -1;
    Serial.printf("CLS,label=%s,score=%.2f\n",
                  g_best_label.c_str(), g_best_score);
#endif

#if EI_CLASSIFIER_HAS_ANOMALY
    ei_printf("Anomaly prediction: %.3f\r\n", result.anomaly);
#endif
}


// ================== CAMERA FUNCTIONS ==================
bool ei_camera_init(void) {

    if (is_initialised) return true;

#if defined(CAMERA_MODEL_ESP_EYE)
    pinMode(13, INPUT_PULLUP);
    pinMode(14, INPUT_PULLUP);
#endif

    esp_err_t err = esp_camera_init(&camera_config);
    if (err != ESP_OK) {
      Serial.printf("Camera init failed with error 0x%x\n", err);
      return false;
    }

    sensor_t * s = esp_camera_sensor_get();
    // optional: OV3660 tweaks only (your cam is OV2640 usually)
    if (s->id.PID == OV3660_PID) {
      s->set_vflip(s, 1);
      s->set_brightness(s, 1);
      s->set_saturation(s, 0);
    }

    is_initialised = true;
    return true;
}

void ei_camera_deinit(void) {
    esp_err_t err = esp_camera_deinit();
    if (err != ESP_OK) {
        ei_printf("Camera deinit failed\n");
        return;
    }
    is_initialised = false;
}

/**
 * @brief Capture JPEG -> RGB888 -> resize into out_buf
 */
bool ei_camera_capture(uint32_t img_width, uint32_t img_height, uint8_t *out_buf) {
    bool do_resize = false;

    if (!is_initialised) {
        ei_printf("ERR: Camera is not initialized\r\n");
        return false;
    }

    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        ei_printf("Camera capture failed\n");
        return false;
    }

    // JPEG -> RGB888 (320x240) into snapshot_buf
    bool converted = fmt2rgb888(fb->buf, fb->len, PIXFORMAT_JPEG, snapshot_buf);
    esp_camera_fb_return(fb);

    if(!converted){
        ei_printf("Conversion failed\n");
        return false;
    }

    if ((img_width != EI_CAMERA_RAW_FRAME_BUFFER_COLS)
        || (img_height != EI_CAMERA_RAW_FRAME_BUFFER_ROWS)) {
        do_resize = true;
    }

    if (do_resize) {
        // source = snapshot_buf (320x240), dest = out_buf (model size)
        ei::image::processing::crop_and_interpolate_rgb888(
            snapshot_buf,
            EI_CAMERA_RAW_FRAME_BUFFER_COLS,
            EI_CAMERA_RAW_FRAME_BUFFER_ROWS,
            out_buf,
            img_width,
            img_height
        );
    } else {
        memcpy(out_buf,
               snapshot_buf,
               img_width * img_height * EI_CAMERA_FRAME_BYTE_SIZE);
    }

    return true;
}

/**
 * @brief EI signal callback: read from resized_buf
 */
static int ei_camera_get_data(size_t offset, size_t length, float *out_ptr)
{
    // resized_buf contains model-sized RGB888 (BGR to RGB swap here)
    size_t pixel_ix   = offset * 3;
    for (size_t i = 0; i < length; i++) {
        uint8_t b = resized_buf[pixel_ix + 0];
        uint8_t g = resized_buf[pixel_ix + 1];
        uint8_t r = resized_buf[pixel_ix + 2];

        out_ptr[i] = ( (uint32_t)r << 16 ) |
                     ( (uint32_t)g << 8  ) |
                     ( (uint32_t)b );

        pixel_ix += 3;
    }
    return 0;
}

#if !defined(EI_CLASSIFIER_SENSOR) || EI_CLASSIFIER_SENSOR != EI_CLASSIFIER_SENSOR_CAMERA
#error "Invalid model for current sensor"
#endif