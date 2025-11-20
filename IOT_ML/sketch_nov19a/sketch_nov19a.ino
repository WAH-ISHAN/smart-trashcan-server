/* Edge Impulse Arduino examples
 * ESP32-CAM AI-THINKER + WiFi AP + Web Stream + BBox serial + Web overlay
 */

#include <ML_IOT_inferencing.h>
#include "edge-impulse-sdk/dsp/image/image.hpp"
#include "esp_camera.h"
#include <WiFi.h>
#include <WebServer.h>

#define FLASH_LED_PIN 4   // ESP32-CAM AI Thinker flash LED

// Select camera model
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

/* Constant defines -------------------------------------------------------- */
#define EI_CAMERA_RAW_FRAME_BUFFER_COLS  320
#define EI_CAMERA_RAW_FRAME_BUFFER_ROWS  240
#define EI_CAMERA_FRAME_BYTE_SIZE        3

/* Private variables ------------------------------------------------------- */
static bool debug_nn       = false;
static bool is_initialised = false;
uint8_t *snapshot_buf; // model-sized RGB888 buffer (EI input)

// Best detection store (for /last endpoint)
String g_best_label = "none";
float  g_best_score = 0.0f;
int    g_best_cx    = -1;
int    g_best_w     = -1;
int    g_best_x     = -1;
int    g_best_y     = -1;
int    g_best_h     = -1;
unsigned long g_best_ts = 0;

// HTTP server
WebServer server(80);

static camera_config_t camera_config = {
    .pin_pwdn     = PWDN_GPIO_NUM,
    .pin_reset    = RESET_GPIO_NUM,
    .pin_xclk     = XCLK_GPIO_NUM,
    .pin_sscb_sda = SIOD_GPIO_NUM,
    .pin_sscb_scl = SIOC_GPIO_NUM,
    .pin_d7   = Y9_GPIO_NUM,
    .pin_d6   = Y8_GPIO_NUM,
    .pin_d5   = Y7_GPIO_NUM,
    .pin_d4   = Y6_GPIO_NUM,
    .pin_d3   = Y5_GPIO_NUM,
    .pin_d2   = Y4_GPIO_NUM,
    .pin_d1   = Y3_GPIO_NUM,
    .pin_d0   = Y2_GPIO_NUM,
    .pin_vsync= VSYNC_GPIO_NUM,
    .pin_href = HREF_GPIO_NUM,
    .pin_pclk = PCLK_GPIO_NUM,
    .xclk_freq_hz= 20000000,
    .ledc_timer  = LEDC_TIMER_0,
    .ledc_channel= LEDC_CHANNEL_0,
    .pixel_format= PIXFORMAT_JPEG, //YUV422,GRAYSCALE,RGB565,JPEG
    .frame_size  = FRAMESIZE_QVGA, //320x240
    .jpeg_quality= 12,
    .fb_count    = 1,
    .fb_location = CAMERA_FB_IN_PSRAM,
    .grab_mode   = CAMERA_GRAB_WHEN_EMPTY,
};

/* Function definitions ------------------------------------------------------- */
bool ei_camera_init(void);
void ei_camera_deinit(void);
bool ei_camera_capture(uint32_t img_width, uint32_t img_height, uint8_t *out_buf);
static int ei_camera_get_data(size_t offset, size_t length, float *out_ptr);

/* ---------------- HTTP handlers ---------------- */

void handle_root() {
    // canvas overlay + /last data draw karanna JS
    String html =
      "<html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<title>ESP32-CAM ML</title></head><body>"
      "<h3>ESP32-CAM ML Stream</h3>"
      "<div style='position:relative;width:320px;height:240px;'>"
      "<img id='v' src='/jpg' width='320' height='240' "
      "     style='position:absolute;left:0;top:0;'>"
      "<canvas id='c' width='320' height='240' "
      "        style='position:absolute;left:0;top:0;'></canvas>"
      "</div>"
      "<pre id='info'></pre>"
      "<script>"
      "function refreshImg(){"
        "document.getElementById('v').src='/jpg?'+Date.now();"
      "}"
      "setInterval(refreshImg,300);"
      "async function refreshBox(){"
        "try{"
          "let r=await fetch('/last');"
          "let t=await r.text();"
          "document.getElementById('info').innerText=t;"
          "let data={};"
          "t.trim().split('\\n').forEach(l=>{let p=l.split('=');if(p.length==2)data[p[0]]=p[1];});"
          "let c=document.getElementById('c');"
          "let ctx=c.getContext('2d');"
          "ctx.clearRect(0,0,c.width,c.height);"
          "if(data.label && data.label!='none'){"
            "let x=parseFloat(data.x||0);"
            "let y=parseFloat(data.y||0);"
            "let w=parseFloat(data.w||0);"
            "let h=parseFloat(data.h||0);"
            "let mw=parseFloat(data.mw||96);"
            "let mh=parseFloat(data.mh||96);"
            "let sx=c.width/mw;"
            "let sy=c.height/mh;"
            "ctx.strokeStyle='red';"
            "ctx.lineWidth=2;"
            "ctx.strokeRect(x*sx,y*sy,w*sx,h*sy);"
            "ctx.fillStyle='red';"
            "ctx.font='14px sans-serif';"
            "ctx.fillText(data.label+' '+Number(data.score||0).toFixed(2),x*sx+2,y*sy+14);"
          "}"
        "}catch(e){}"
      "}"
      "setInterval(refreshBox,500);"
      "</script>"
      "</body></html>";
    server.send(200, "text/html", html);
}

void handle_jpg() {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        server.send(500, "text/plain", "Camera capture failed");
        return;
    }
    server.sendHeader("Cache-Control", "no-cache");
    server.setContentLength(fb->len);
    server.send(200, "image/jpeg", "");
    WiFiClient client = server.client();
    client.write(fb->buf, fb->len);
    esp_camera_fb_return(fb);
}

void handle_last() {
    String s;
    s  = "label=" + g_best_label + "\n";
    s += "score=" + String(g_best_score, 3) + "\n";
    s += "cx="    + String(g_best_cx) + "\n";
    s += "x="     + String(g_best_x)  + "\n";
    s += "y="     + String(g_best_y)  + "\n";
    s += "w="     + String(g_best_w)  + "\n";
    s += "h="     + String(g_best_h)  + "\n";
    s += "mw="    + String(EI_CLASSIFIER_INPUT_WIDTH)  + "\n";
    s += "mh="    + String(EI_CLASSIFIER_INPUT_HEIGHT) + "\n";
    s += "ts="    + String(g_best_ts) + "\n";
    server.send(200, "text/plain", s);
}

/**
* @brief      Arduino setup function
*/
void setup()
{
    Serial.begin(115200);
    while (!Serial);
    Serial.println("Edge Impulse Inferencing Demo");

    // Flash LED always ON
    pinMode(FLASH_LED_PIN, OUTPUT);
    digitalWrite(FLASH_LED_PIN, HIGH);

    if (ei_camera_init() == false) {
        ei_printf("Failed to initialize Camera!\r\n");
    }
    else {
        ei_printf("Camera initialized\r\n");
    }

    // allocate once (model input size)
    snapshot_buf = (uint8_t*)malloc(EI_CLASSIFIER_INPUT_WIDTH *
                                    EI_CLASSIFIER_INPUT_HEIGHT *
                                    EI_CAMERA_FRAME_BYTE_SIZE);
    if (!snapshot_buf) {
        ei_printf("ERR: Failed to allocate snapshot buffer!\n");
        while (1) {}
    }

    // WiFi AP
    WiFi.mode(WIFI_AP);
    WiFi.softAP("ESP32CAM_TRASH", "12345678");
    IPAddress ip = WiFi.softAPIP();
    Serial.println("Access Point started.");
    Serial.print("  SSID: ESP32CAM_TRASH\n  PASS: 12345678\n  IP: ");
    Serial.println(ip);

    server.on("/",    HTTP_GET, handle_root);
    server.on("/jpg", HTTP_GET, handle_jpg);
    server.on("/last",HTTP_GET, handle_last);
    server.begin();
    Serial.println("HTTP server started");

    ei_printf("\nStarting continious inference in 2 seconds...\n");
    ei_sleep(2000);
}

/**
* @brief      Get data and run inferencing
*/
void loop()
{
    server.handleClient();

    if (ei_sleep(5) != EI_IMPULSE_OK) {
        return;
    }

    ei::signal_t signal;
    signal.total_length = EI_CLASSIFIER_INPUT_WIDTH * EI_CLASSIFIER_INPUT_HEIGHT;
    signal.get_data = &ei_camera_get_data;

    if (ei_camera_capture((size_t)EI_CLASSIFIER_INPUT_WIDTH,
                          (size_t)EI_CLASSIFIER_INPUT_HEIGHT,
                          snapshot_buf) == false) {
        ei_printf("Failed to capture image\r\n");
        return;
    }

    ei_impulse_result_t result = { 0 };
    EI_IMPULSE_ERROR err = run_classifier(&signal, &result, debug_nn);
    if (err != EI_IMPULSE_OK) {
        ei_printf("ERR: Failed to run classifier (%d)\n", err);
        return;
    }

    ei_printf("Predictions (DSP: %d ms., Classification: %d ms., Anomaly: %d ms.): \n",
                result.timing.dsp, result.timing.classification, result.timing.anomaly);

#if EI_CLASSIFIER_OBJECT_DETECTION == 1
    ei_printf("Object detection bounding boxes:\r\n");

    int   best_i = -1;
    float best_v = 0.0f;

    for (uint32_t i = 0; i < result.bounding_boxes_count; i++) {
        ei_impulse_result_bounding_box_t bb = result.bounding_boxes[i];
        if (bb.value == 0) continue;
        ei_printf("  %s (%f) [ x: %u, y: %u, width: %u, height: %u ]\r\n",
                bb.label,
                bb.value,
                bb.x,
                bb.y,
                bb.width,
                bb.height);

        if (bb.value > best_v) {
            best_v = bb.value;
            best_i = (int)i;
        }
    }

    if (best_i >= 0) {
        ei_impulse_result_bounding_box_t bb = result.bounding_boxes[best_i];
        int cx = bb.x + (bb.width / 2);

        g_best_label = String(bb.label);
        g_best_score = bb.value;
        g_best_cx    = cx;
        g_best_x     = bb.x;
        g_best_y     = bb.y;
        g_best_w     = bb.width;
        g_best_h     = bb.height;
        g_best_ts    = millis();

        ei_printf("BEST: %s (%f) cx:%d w:%d\r\n",
                  bb.label, bb.value, cx, bb.width);

        Serial.printf("BOX,cx=%d,w=%d,iw=%d,label=%s,score=%.2f\n",
                      cx,
                      bb.width,
                      (int)EI_CLASSIFIER_INPUT_WIDTH,
                      bb.label,
                      bb.value);
    }
    else {
        g_best_label = "none";
        g_best_score = 0;
        g_best_cx    = -1;
        g_best_x     = -1;
        g_best_y     = -1;
        g_best_w     = -1;
        g_best_h     = -1;
        g_best_ts    = millis();
        ei_printf("No object detected\r\n");
        Serial.println("BOX,none");
    }

#else
    ei_printf("Predictions:\r\n");
    for (uint16_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
        ei_printf("  %s: ", ei_classifier_inferencing_categories[i]);
        ei_printf("%.5f\r\n", result.classification[i].value);
    }
#endif

#if EI_CLASSIFIER_HAS_ANOMALY
    ei_printf("Anomaly prediction: %.3f\r\n", result.anomaly);
#endif

#if EI_CLASSIFIER_HAS_VISUAL_ANOMALY
    ei_printf("Visual anomalies:\r\n");
    for (uint32_t i = 0; i < result.visual_ad_count; i++) {
        ei_impulse_result_bounding_box_t bb = result.visual_ad_grid_cells[i];
        if (bb.value == 0) continue;
        ei_printf("  %s (%f) [ x: %u, y: %u, width: %u, height: %u ]\r\n",
                bb.label,
                bb.value,
                bb.x,
                bb.y,
                bb.width,
                bb.height);
    }
#endif
}

/* ---- camera helpers (original, with resize fix) ---- */

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
    if (s->id.PID == OV3660_PID) {
      s->set_vflip(s, 1);
      s->set_brightness(s, 1);
      s->set_saturation(s, 0);
    }

#if defined(CAMERA_MODEL_M5STACK_WIDE)
    s->set_vflip(s, 1);
    s->set_hmirror(s, 1);
#elif defined(CAMERA_MODEL_ESP_EYE)
    s->set_vflip(s, 1);
    s->set_hmirror(s, 1);
    s->set_awb_gain(s, 1);
#endif

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
 * Capture, rescale and crop image
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

    // temp raw buffer
    static uint8_t *raw_buf = nullptr;
    if (!raw_buf) {
        raw_buf = (uint8_t*)malloc(EI_CAMERA_RAW_FRAME_BUFFER_COLS *
                                   EI_CAMERA_RAW_FRAME_BUFFER_ROWS *
                                   EI_CAMERA_FRAME_BYTE_SIZE);
        if (!raw_buf) {
            ei_printf("ERR: Failed to allocate raw_buf!\n");
            esp_camera_fb_return(fb);
            return false;
        }
    }

    bool converted = fmt2rgb888(fb->buf, fb->len, PIXFORMAT_JPEG, raw_buf);
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
        ei::image::processing::crop_and_interpolate_rgb888(
            raw_buf,
            EI_CAMERA_RAW_FRAME_BUFFER_COLS,
            EI_CAMERA_RAW_FRAME_BUFFER_ROWS,
            out_buf,
            img_width,
            img_height);
    } else {
        memcpy(out_buf,
               raw_buf,
               img_width * img_height * EI_CAMERA_FRAME_BYTE_SIZE);
    }

    return true;
}

static int ei_camera_get_data(size_t offset, size_t length, float *out_ptr)
{
    size_t pixel_ix   = offset * 3;
    size_t pixels_left = length;
    size_t out_ptr_ix  = 0;

    while (pixels_left != 0) {
        out_ptr[out_ptr_ix] =
            (snapshot_buf[pixel_ix + 2] << 16) +
            (snapshot_buf[pixel_ix + 1] << 8)  +
             snapshot_buf[pixel_ix];

        out_ptr_ix++;
        pixel_ix += 3;
        pixels_left--;
    }
    return 0;
}

#if !defined(EI_CLASSIFIER_SENSOR) || EI_CLASSIFIER_SENSOR != EI_CLASSIFIER_SENSOR_CAMERA
#error "Invalid model for current sensor"
#endif