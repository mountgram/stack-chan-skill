/*
 * SPDX-License-Identifier: MIT
 */
#include "app_remote_agent.h"

#include "stacky_wake_word.h"

#include <ArduinoJson.hpp>
#include <apps/common/common.h>
#include <assets/assets.h>
#include <audio/audio_codec.h>
#include <board.h>
#include <esp_crt_bundle.h>
#include <esp_heap_caps.h>
#include <esp_http_client.h>
#include <esp_netif.h>
#include <nvs.h>
#include <hal/board/config.h>
#include <hal/board/hal_bridge.h>
#include <hal/hal.h>
#include <jpg/image_to_jpeg.h>
#include <lvgl.h>
#include <mooncake_log.h>
#include <smooth_lvgl.hpp>
#include <stackchan/stackchan.h>
#include <stackchan/avatar/decorators/decorators.h>
#include <stackchan/avatar/skins/default/default.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>
#include <unistd.h>

#ifndef STACKY_WS_PORT
#define STACKY_WS_PORT 6001
#endif

using namespace smooth_ui_toolkit::lvgl_cpp;
using namespace stackchan;

static const char* TAG = "REMOTE.AGENT";
static const char* STACKY_WS_PATH = "/stacky/device";
static const char* STACKY_BRAIN_CONFIG_PATH = "/stacky/brain";
static const char* STACKY_NVS_NAMESPACE = "stacky";
static AppRemoteAgent* s_websocket_app = nullptr;

static std::string websocket_listen_url()
{
    char buffer[96];
    auto* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip_info = {};
    if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK && ip_info.ip.addr != 0) {
        snprintf(buffer, sizeof(buffer), "ws://" IPSTR ":%d%s", IP2STR(&ip_info.ip), STACKY_WS_PORT, STACKY_WS_PATH);
        return buffer;
    }
    snprintf(buffer, sizeof(buffer), "ws://<stackchan>:%d%s", STACKY_WS_PORT, STACKY_WS_PATH);
    return buffer;
}
static constexpr size_t MAX_RENDER_ANIMATIONS = 4;
static constexpr size_t MAX_RENDER_TRACKS = 32;
static constexpr uint32_t AUDIO_START_TIMEOUT_MS = 2000;
static constexpr uint8_t PACKET_AUDIO_MIC_PCM = 0x31;
static constexpr uint8_t PACKET_AUDIO_PLAYBACK_PCM = 0x41;
static constexpr uint8_t PACKET_AUDIO_PLAYBACK_END = 0x42;
static constexpr size_t MIC_PCM_QUEUE_DEPTH = 4;
static constexpr size_t PLAYBACK_RING_BUFFER_BYTES = 24 * 1024;
static constexpr size_t PLAYBACK_PREBUFFER_BYTES = 4 * 1024;
static constexpr size_t PLAYBACK_READ_BYTES = 2048;
static constexpr uint32_t PLAYBACK_PREBUFFER_TIMEOUT_MS = 250;
static constexpr uint32_t PLAYBACK_QUEUE_FULL_LOG_INTERVAL_MS = 1000;
static constexpr size_t MIN_INTERNAL_SRAM_SPEAK = 8192;
static constexpr size_t MIN_INTERNAL_SRAM_WAKE_WORD = 64 * 1024;
static constexpr size_t MIN_INTERNAL_SRAM_RENDER = 12288;
static constexpr size_t MIN_INTERNAL_SRAM_CAMERA = 12288;
static constexpr size_t MIN_INTERNAL_SRAM_CAMERA_ENHANCED = 32768;
static constexpr int CAMERA_PREVIEW_WIDTH = 160;
static constexpr int CAMERA_PREVIEW_HEIGHT = 120;
static constexpr int BARGE_IN_LEVEL_THRESHOLD = 250;
static constexpr uint32_t PLAYBACK_DRAIN_CHECK_MS = 20;
static constexpr float WAKE_WORD_DEFAULT_CUTOFF = 0.99f;
static constexpr size_t WAKE_WORD_DEFAULT_SLIDING_WINDOW = 10;

static int clamp_int(int value, int min, int max)
{
    return std::min(max, std::max(min, value));
}

static uint8_t level_luma(uint8_t y, uint8_t low, uint8_t high)
{
    if (y <= low) return 0;
    if (y >= high) return 255;
    return static_cast<uint8_t>((static_cast<int>(y - low) * 255) / std::max<int>(1, high - low));
}

static lv_color_t parse_hex_color(const char* value, uint32_t fallback)
{
    if (!value || value[0] != '#' || strlen(value) != 7) return lv_color_hex(fallback);
    char* end = nullptr;
    unsigned long parsed = strtoul(value + 1, &end, 16);
    if (!end || *end != 0) return lv_color_hex(fallback);
    return lv_color_hex(parsed & 0xffffff);
}

struct RenderFrame {
    char id[65] = {0};
    int x       = 0;
    int y       = 0;
};

static bool find_render_frame(const std::vector<RenderFrame>& frames, const char* id, RenderFrame& out)
{
    if (!id || !id[0]) return false;
    for (const auto& frame : frames) {
        if (strcmp(frame.id, id) == 0) {
            out = frame;
            return true;
        }
    }
    return false;
}

static AppRemoteAgent::RenderNodeRef* find_render_node(std::vector<AppRemoteAgent::RenderNodeRef>& nodes, const char* id)
{
    if (!id || !id[0]) return nullptr;
    for (auto& node : nodes) {
        if (strcmp(node.id, id) == 0) return &node;
    }
    return nullptr;
}

static float read_float(ArduinoJson::JsonVariantConst value, float fallback)
{
    if (value.is<float>()) return value.as<float>();
    if (value.is<int>()) return static_cast<float>(value.as<int>());
    return fallback;
}

static int pcm_level_1000(const std::vector<int16_t>& samples)
{
    if (samples.empty()) return 0;
    uint64_t total = 0;
    for (auto sample : samples) {
        total += static_cast<uint16_t>(std::abs(static_cast<int>(sample)));
    }
    int average = static_cast<int>(total / samples.size());
    return clamp_int((average * 1000) / 12000, 0, 1000);
}

static int smooth_level_1000(int previous, int next)
{
    if (next > previous) return previous + ((next - previous) * 3) / 4;
    return previous + (next - previous) / 4;
}

static float interpolate_keyframes(const std::vector<AppRemoteAgent::RenderKeyframe>& keyframes, uint32_t t)
{
    if (keyframes.empty()) return 0.0f;
    if (t <= keyframes.front().t) return keyframes.front().value;
    for (size_t i = 1; i < keyframes.size(); ++i) {
        const auto& previous = keyframes[i - 1];
        const auto& next = keyframes[i];
        if (t <= next.t) {
            uint32_t span = next.t > previous.t ? next.t - previous.t : 1;
            float alpha = static_cast<float>(t - previous.t) / static_cast<float>(span);
            return previous.value + (next.value - previous.value) * alpha;
        }
    }
    return keyframes.back().value;
}

static bool find_luma_levels(const std::array<uint16_t, 256>& hist, size_t samples, uint8_t& low, uint8_t& high)
{
    if (samples == 0) return false;
    size_t low_target  = samples / 20;
    size_t high_target = samples - samples / 50;
    size_t count       = 0;

    low = 0;
    for (int i = 0; i < 256; ++i) {
        count += hist[i];
        if (count >= low_target) {
            low = static_cast<uint8_t>(i);
            break;
        }
    }

    count = 0;
    high  = 255;
    for (int i = 0; i < 256; ++i) {
        count += hist[i];
        if (count >= high_target) {
            high = static_cast<uint8_t>(i);
            break;
        }
    }

    return high > low + 16;
}

static void auto_level_yuyv(std::vector<uint8_t>& frame)
{
    std::array<uint16_t, 256> hist{};
    size_t samples = 0;
    for (size_t i = 0; i + 3 < frame.size(); i += 4) {
        ++hist[frame[i]];
        ++hist[frame[i + 2]];
        samples += 2;
    }

    uint8_t low = 0;
    uint8_t high = 255;
    if (!find_luma_levels(hist, samples, low, high)) return;

    for (size_t i = 0; i + 3 < frame.size(); i += 4) {
        frame[i]     = level_luma(frame[i], low, high);
        frame[i + 2] = level_luma(frame[i + 2], low, high);
    }
}

static void auto_level_grey(std::vector<uint8_t>& frame)
{
    std::array<uint16_t, 256> hist{};
    for (auto& y : frame) {
        ++hist[y];
    }

    uint8_t low = 0;
    uint8_t high = 255;
    if (!find_luma_levels(hist, frame.size(), low, high)) return;

    for (auto& y : frame) {
        y = level_luma(y, low, high);
    }
}

static avatar::Emotion parse_emotion(const char* emotion)
{
    if (!emotion) return avatar::Emotion::Neutral;
    if (strcmp(emotion, "happy") == 0) return avatar::Emotion::Happy;
    if (strcmp(emotion, "angry") == 0) return avatar::Emotion::Angry;
    if (strcmp(emotion, "sad") == 0) return avatar::Emotion::Sad;
    if (strcmp(emotion, "doubt") == 0 || strcmp(emotion, "thinking") == 0 || strcmp(emotion, "curious") == 0 || strcmp(emotion, "surprised") == 0) return avatar::Emotion::Doubt;
    if (strcmp(emotion, "sleepy") == 0 || strcmp(emotion, "asleep") == 0) return avatar::Emotion::Sleepy;
    return avatar::Emotion::Neutral;
}

static bool is_visible_status_mode(const char* mode)
{
    return mode && (strcmp(mode, "listening") == 0 || strcmp(mode, "thinking") == 0 || strcmp(mode, "speaking") == 0);
}

static lv_color_t status_color(const char* mode)
{
    if (mode && (strcmp(mode, "connected") == 0 || strcmp(mode, "standby") == 0)) return lv_color_hex(0xFFD24A);
    if (is_visible_status_mode(mode)) return lv_color_hex(0x35D0A4);
    return lv_color_hex(0xFF4D5E);
}

static void panel_click_cb(lv_event_t* event)
{
    auto* app = static_cast<AppRemoteAgent*>(lv_event_get_user_data(event));
    if (app) {
        const lv_event_code_t code = lv_event_get_code(event);
        if (code == LV_EVENT_LONG_PRESSED) {
            app->handleHold();
        } else if (code == LV_EVENT_CLICKED) {
            app->handleTap();
        }
    }
}

AppRemoteAgent::AppRemoteAgent()
{
    setAppInfo().name = "REMOTE.AGENT";
    static uint32_t theme_color = 0x33CC99;
    setAppInfo().userData       = (void*)&theme_color;
}

AppRemoteAgent::~AppRemoteAgent() = default;

void AppRemoteAgent::onCreate()
{
    mclog::tagInfo(TAG, "on create");
}

void AppRemoteAgent::onOpen()
{
    mclog::tagInfo(TAG, "on open");
    _opened = true;

    {
        LvglLockGuard lock;
        createUi();
        view::create_home_indicator([&]() { close(); }, 0x33CC99, 0x134233);
    }

    loadBrainConfig();
    setStatus("connecting", "Starting Wi-Fi...");
    GetHAL().startNetwork([this](std::string_view msg) {
        char buffer[120];
        size_t len = std::min(msg.size(), sizeof(buffer) - 1);
        memcpy(buffer, msg.data(), len);
        buffer[len] = 0;
        queueStatus("connecting", buffer);
    });
    applyPendingStatus();
    startAudioTasks();
    startWebSocketServer();
}

void AppRemoteAgent::createUi()
{
    _root = lv_obj_create(lv_screen_active());
    lv_obj_set_size(_root, 320, 240);
    lv_obj_align(_root, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(_root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(_root, 0, 0);
    lv_obj_set_style_pad_all(_root, 10, 0);
    lv_obj_add_flag(_root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(_root, panel_click_cb, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(_root, panel_click_cb, LV_EVENT_LONG_PRESSED, this);

    _status_dot = lv_obj_create(_root);
    lv_obj_set_size(_status_dot, 14, 14);
    lv_obj_set_style_radius(_status_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(_status_dot, lv_color_hex(0xFF4D5E), 0);
    lv_obj_set_style_border_width(_status_dot, 0, 0);
    lv_obj_set_style_pad_all(_status_dot, 0, 0);
    lv_obj_align(_status_dot, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_add_flag(_status_dot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(_status_dot, panel_click_cb, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(_status_dot, panel_click_cb, LV_EVENT_LONG_PRESSED, this);

    _main_label = lv_label_create(_root);
    lv_label_set_long_mode(_main_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(_main_label, 300);
    lv_obj_set_style_text_color(_main_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(_main_label, &lv_font_montserrat_24, 0);
    lv_label_set_text(_main_label, "Waiting for brain...");
    lv_obj_align(_main_label, LV_ALIGN_CENTER, 0, -10);
    lv_obj_add_flag(_main_label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(_main_label, panel_click_cb, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(_main_label, panel_click_cb, LV_EVENT_LONG_PRESSED, this);

    _log_label = lv_label_create(_root);
    lv_label_set_long_mode(_log_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(_log_label, 300);
    lv_obj_set_style_text_color(_log_label, lv_color_hex(0xA7B0C0), 0);
    lv_label_set_text(_log_label, websocket_listen_url().c_str());
    lv_obj_align(_log_label, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_add_flag(_log_label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(_log_label, panel_click_cb, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(_log_label, panel_click_cb, LV_EVENT_LONG_PRESSED, this);
}

void AppRemoteAgent::startWebSocketServer()
{
    if (_websocket_server) return;

    setStatus("connecting", "Starting WebSocket server...");
    s_websocket_app = this;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = STACKY_WS_PORT;
    config.max_open_sockets = 3;
    config.lru_purge_enable = true;
    config.recv_wait_timeout = 1;
    config.send_wait_timeout = 1;
    config.close_fn = AppRemoteAgent::webSocketCloseHandler;

    _ws_recv_buf.reserve(4096);

    esp_err_t err = httpd_start(&_websocket_server, &config);
    if (err != ESP_OK) {
        _websocket_server = nullptr;
        setStatus("error", "WebSocket server failed");
        mclog::tagInfo(TAG, "httpd_start failed: {}", (int)err);
        return;
    }

    httpd_uri_t ws_uri = {};
    ws_uri.uri = STACKY_WS_PATH;
    ws_uri.method = HTTP_GET;
    ws_uri.handler = AppRemoteAgent::webSocketHandler;
    ws_uri.user_ctx = this;
    ws_uri.is_websocket = true;

    err = httpd_register_uri_handler(_websocket_server, &ws_uri);
    if (err != ESP_OK) {
        mclog::tagInfo(TAG, "httpd_register_uri_handler failed: {}", (int)err);
        httpd_stop(_websocket_server);
        _websocket_server = nullptr;
        if (s_websocket_app == this) s_websocket_app = nullptr;
        setStatus("error", "WebSocket route failed");
        return;
    }

    httpd_uri_t brain_get = {};
    brain_get.uri = STACKY_BRAIN_CONFIG_PATH;
    brain_get.method = HTTP_GET;
    brain_get.handler = AppRemoteAgent::brainConfigHandler;
    brain_get.user_ctx = this;
    httpd_register_uri_handler(_websocket_server, &brain_get);

    httpd_uri_t brain_post = brain_get;
    brain_post.method = HTTP_POST;
    httpd_register_uri_handler(_websocket_server, &brain_post);

    setStatus("offline", offlineStatusText().c_str());
}

void AppRemoteAgent::stopWebSocketServer()
{
    _connected = false;
    httpd_handle_t server = _websocket_server;
    _websocket_server = nullptr;
    _websocket_fd.store(-1);
    _hello_pending = false;
    if (s_websocket_app == this) s_websocket_app = nullptr;
    if (server) {
        httpd_stop(server);
    }
}

void AppRemoteAgent::handleWebSocketConnected(int fd)
{
    if (_websocket_fd.load() >= 0 && _websocket_fd.load() != fd && _websocket_server) {
        httpd_sess_trigger_close(_websocket_server, _websocket_fd.load());
    }
    _connected = false;
    _hello_pending = false;
    _standby = false;
    _websocket_fd.store(fd);
    _audio_streaming = true;
    cancelPlayback(false);
    failPendingAudioStart("new brain connection opened");
    _mic_audio_level = 0;
    _playback_audio_level = 0;
    _audio_stream_started_at = GetHAL().millis();
    _last_audio_frame_sent_at = 0;
    _audio_input_failures = 0;
    _audio_first_input_attempt_logged = false;
    _audio_first_input_success_logged = false;
    if (_audio_capture_pcm_queue) {
        xQueueReset(_audio_capture_pcm_queue);
    }
    _connected = true;
    _hello_pending = true;
    queueStatus("connected", "Ready");
}

void AppRemoteAgent::handleWebSocketDisconnected(int fd)
{
    if (_websocket_fd.load() != fd) return;
    _websocket_fd.store(-1);
    _connected = false;
    _hello_pending = false;
    _standby = false;
    _audio_streaming = false;
    cancelPlayback(false);
    failPendingAudioStart("connection closed before audio capture started");
    if (_audio_capture_pcm_queue) {
        xQueueReset(_audio_capture_pcm_queue);
    }
    _mic_audio_level = 0;
    _playback_audio_level = 0;
    disarmWakeWord(500);
    queueStatus("offline", offlineStatusText().c_str());
}

esp_err_t AppRemoteAgent::handleWebSocketFrame(httpd_req_t* req)
{
    httpd_ws_frame_t frame = {};
    esp_err_t err = httpd_ws_recv_frame(req, &frame, 0);
    const int fd = httpd_req_to_sockfd(req);
    if (err != ESP_OK) {
        handleWebSocketDisconnected(fd);
        return err;
    }

    if (frame.type == HTTPD_WS_TYPE_CLOSE) {
        handleWebSocketDisconnected(fd);
        return ESP_OK;
    }
    if (frame.type == HTTPD_WS_TYPE_PING || frame.type == HTTPD_WS_TYPE_PONG) {
        return ESP_OK;
    }

    _ws_recv_buf.resize(frame.len + 1);
    frame.payload = _ws_recv_buf.data();
    err = httpd_ws_recv_frame(req, &frame, frame.len);
    if (err != ESP_OK) {
        handleWebSocketDisconnected(fd);
        return err;
    }

    if (frame.type == HTTPD_WS_TYPE_TEXT) {
        std::lock_guard<std::mutex> lock(_mutex);
        _messages.push({false, std::string(reinterpret_cast<char*>(_ws_recv_buf.data()), frame.len)});
    } else if (frame.type == HTTPD_WS_TYPE_BINARY) {
        handleBrainBinaryPacket(_ws_recv_buf.data(), frame.len);
    }
    return ESP_OK;
}

void AppRemoteAgent::handleBrainBinaryPacket(const uint8_t* data, size_t len)
{
    if (!data || len < 5) return;
    const uint8_t packet_type = data[0];
    const size_t packet_len = (static_cast<size_t>(data[1]) << 24) | (static_cast<size_t>(data[2]) << 16) |
                              (static_cast<size_t>(data[3]) << 8) | static_cast<size_t>(data[4]);
    if (packet_type == PACKET_AUDIO_PLAYBACK_PCM && packet_len <= len - 5) {
        queueWebSocketAudioFrame(data + 5, packet_len);
    } else if (packet_type == PACKET_AUDIO_PLAYBACK_END) {
        queueWebSocketAudioFrame(nullptr, 0);
    }
}

esp_err_t AppRemoteAgent::webSocketHandler(httpd_req_t* req)
{
    auto* app = static_cast<AppRemoteAgent*>(req->user_ctx);
    if (!app) return ESP_ERR_INVALID_ARG;
    if (req->method == HTTP_GET) {
        app->handleWebSocketConnected(httpd_req_to_sockfd(req));
        return ESP_OK;
    }
    return app->handleWebSocketFrame(req);
}

void AppRemoteAgent::webSocketCloseHandler(httpd_handle_t server, int fd)
{
    if (s_websocket_app && s_websocket_app->_websocket_server == server) {
        s_websocket_app->handleWebSocketDisconnected(fd);
    }
    ::close(fd);
}

// ── Outbound brain link (robot dials a remote brain) ──

std::string AppRemoteAgent::offlineStatusText()
{
    std::string url;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        url = _brain_url;
    }
    if (!url.empty()) return "Dialing " + url;
    return websocket_listen_url();
}

void AppRemoteAgent::loadBrainConfig()
{
    nvs_handle_t handle;
    if (nvs_open(STACKY_NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return;
    auto read = [&](const char* key, std::string& out) {
        size_t len = 0;
        if (nvs_get_str(handle, key, nullptr, &len) != ESP_OK || len == 0) return;
        std::vector<char> buffer(len);
        if (nvs_get_str(handle, key, buffer.data(), &len) == ESP_OK) out.assign(buffer.data());
    };
    std::string url, token;
    read("brain_url", url);
    read("brain_token", token);
    nvs_close(handle);
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _brain_url = url;
        _brain_token = token;
    }
    if (!url.empty()) mclog::tagInfo(TAG, "brain dial-out configured: {}", url);
}

bool AppRemoteAgent::saveBrainConfig(const std::string& url, const std::string& token)
{
    nvs_handle_t handle;
    if (nvs_open(STACKY_NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return false;
    bool ok = true;
    if (url.empty()) {
        nvs_erase_key(handle, "brain_url");
        nvs_erase_key(handle, "brain_token");
    } else {
        ok = nvs_set_str(handle, "brain_url", url.c_str()) == ESP_OK && ok;
        if (token.empty()) nvs_erase_key(handle, "brain_token");
        else ok = nvs_set_str(handle, "brain_token", token.c_str()) == ESP_OK && ok;
    }
    ok = nvs_commit(handle) == ESP_OK && ok;
    nvs_close(handle);
    return ok;
}

void AppRemoteAgent::maintainBrainClient()
{
    // A locally connected brain owns the robot; the dial-out link stands down.
    if (_websocket_fd.load() >= 0) {
        if (_ws_client) stopBrainClient();
        return;
    }
    if (_brain_config_dirty.exchange(false)) stopBrainClient();

    std::string url;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        url = _brain_url;
    }
    if (url.empty()) {
        if (_ws_client) stopBrainClient();
        return;
    }
    if (_ws_client) return;

    auto* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip_info = {};
    if (!netif || esp_netif_get_ip_info(netif, &ip_info) != ESP_OK || ip_info.ip.addr == 0) return;
    startBrainClient();
}

void AppRemoteAgent::startBrainClient()
{
    std::string url, token;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        url = _brain_url;
        token = _brain_token;
    }
    if (url.empty() || _ws_client) return;

    _ws_client_headers.clear();
    if (!token.empty()) _ws_client_headers = "Authorization: Bearer " + token + "\r\n";

    esp_websocket_client_config_t config = {};
    config.uri = url.c_str();
    if (!_ws_client_headers.empty()) config.headers = _ws_client_headers.c_str();
    config.buffer_size = 4096;
    config.task_stack = 8192;
    config.reconnect_timeout_ms = 3000;
    config.network_timeout_ms = 10000;
    config.crt_bundle_attach = esp_crt_bundle_attach;

    esp_websocket_client_handle_t client = esp_websocket_client_init(&config);
    if (!client) {
        mclog::tagInfo(TAG, "brain client init failed");
        return;
    }
    esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, AppRemoteAgent::wsClientEventHandler, this);
    if (esp_websocket_client_start(client) != ESP_OK) {
        mclog::tagInfo(TAG, "brain client start failed");
        esp_websocket_client_destroy(client);
        return;
    }
    _ws_client = client;
    mclog::tagInfo(TAG, "dialing brain {}", url);
    queueStatus("connecting", offlineStatusText().c_str());
}

void AppRemoteAgent::stopBrainClient()
{
    esp_websocket_client_handle_t client = _ws_client;
    if (!client) return;
    _ws_client = nullptr;
    const bool was_active = _client_link_active.exchange(false);
    esp_websocket_client_destroy(client);
    _ws_client_rx.clear();
    _ws_client_rx_opcode = 0;
    if (was_active && _websocket_fd.load() < 0) {
        _connected = false;
        _hello_pending = false;
        _audio_streaming = false;
        cancelPlayback(false);
        queueStatus("offline", offlineStatusText().c_str());
    }
}

void AppRemoteAgent::handleClientLinkOpened()
{
    if (_websocket_fd.load() >= 0) return;
    _connected = false;
    _hello_pending = false;
    _standby = false;
    _audio_streaming = true;
    cancelPlayback(false);
    failPendingAudioStart("new brain connection opened");
    _mic_audio_level = 0;
    _playback_audio_level = 0;
    _audio_stream_started_at = GetHAL().millis();
    _last_audio_frame_sent_at = 0;
    _audio_input_failures = 0;
    _audio_first_input_attempt_logged = false;
    _audio_first_input_success_logged = false;
    if (_audio_capture_pcm_queue) {
        xQueueReset(_audio_capture_pcm_queue);
    }
    _client_link_active = true;
    _connected = true;
    _hello_pending = true;
    mclog::tagInfo(TAG, "brain link connected");
    queueStatus("connected", "Ready (remote brain)");
}

void AppRemoteAgent::handleClientLinkClosed()
{
    if (!_client_link_active.exchange(false)) return;
    _ws_client_rx.clear();
    _ws_client_rx_opcode = 0;
    if (_websocket_fd.load() >= 0) return;
    _connected = false;
    _hello_pending = false;
    _standby = false;
    _audio_streaming = false;
    cancelPlayback(false);
    failPendingAudioStart("connection closed before audio capture started");
    if (_audio_capture_pcm_queue) {
        xQueueReset(_audio_capture_pcm_queue);
    }
    _mic_audio_level = 0;
    _playback_audio_level = 0;
    disarmWakeWord(500);
    queueStatus("offline", offlineStatusText().c_str());
}

void AppRemoteAgent::handleClientData(const esp_websocket_event_data_t* data)
{
    if (!data) return;
    if (data->op_code == 0x08 || data->op_code == 0x09 || data->op_code == 0x0A) return;
    if (data->op_code == 0x01 || data->op_code == 0x02) {
        if (data->payload_offset == 0) {
            _ws_client_rx.clear();
            _ws_client_rx_opcode = data->op_code;
        }
    } else if (data->op_code != 0x00) {
        return;
    }
    if (_ws_client_rx_opcode == 0) return;
    if (_ws_client_rx.size() + data->data_len > 256 * 1024) {
        mclog::tagInfo(TAG, "brain frame too large; dropping");
        _ws_client_rx.clear();
        _ws_client_rx_opcode = 0;
        return;
    }
    if (data->data_len > 0) {
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data->data_ptr);
        _ws_client_rx.insert(_ws_client_rx.end(), bytes, bytes + data->data_len);
    }
    // Frames can arrive in library-buffer-sized slices, and intermediaries
    // (the Cloudflare tunnel) may also re-fragment messages; dispatch only
    // once the frame is fully received and final.
    const bool frame_complete = data->payload_offset + data->data_len >= data->payload_len;
    if (!frame_complete || !data->fin) return;

    if (_ws_client_rx_opcode == 0x01) {
        std::lock_guard<std::mutex> lock(_mutex);
        _messages.push({false, std::string(reinterpret_cast<const char*>(_ws_client_rx.data()), _ws_client_rx.size())});
    } else {
        handleBrainBinaryPacket(_ws_client_rx.data(), _ws_client_rx.size());
    }
    _ws_client_rx.clear();
    _ws_client_rx_opcode = 0;
}

void AppRemoteAgent::wsClientEventHandler(void* arg, esp_event_base_t base, int32_t event_id, void* event_data)
{
    auto* app = static_cast<AppRemoteAgent*>(arg);
    if (!app) return;
    switch (event_id) {
        case WEBSOCKET_EVENT_CONNECTED:
            app->handleClientLinkOpened();
            break;
        case WEBSOCKET_EVENT_DISCONNECTED:
        case WEBSOCKET_EVENT_CLOSED:
            app->handleClientLinkClosed();
            break;
        case WEBSOCKET_EVENT_DATA:
            app->handleClientData(static_cast<esp_websocket_event_data_t*>(event_data));
            break;
        default:
            break;
    }
}

esp_err_t AppRemoteAgent::brainConfigHandler(httpd_req_t* req)
{
    auto* app = static_cast<AppRemoteAgent*>(req->user_ctx);
    if (!app) return ESP_ERR_INVALID_ARG;
    httpd_resp_set_type(req, "application/json");

    if (req->method == HTTP_GET) {
        std::string url;
        bool has_token;
        {
            std::lock_guard<std::mutex> lock(app->_mutex);
            url = app->_brain_url;
            has_token = !app->_brain_token.empty();
        }
        const char* brain = app->_websocket_fd.load() >= 0 ? "local"
                            : (app->_client_link_active.load() ? "remote" : "none");
        char buffer[420];
        snprintf(buffer, sizeof(buffer), R"({"url":"%s","hasToken":%s,"connected":%s,"brain":"%s"})",
                 url.c_str(), has_token ? "true" : "false", app->_connected.load() ? "true" : "false", brain);
        httpd_resp_sendstr(req, buffer);
        return ESP_OK;
    }

    if (req->content_len > 768) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "body too large");
        return ESP_FAIL;
    }
    char body[769] = {0};
    int received = 0;
    while (received < (int)req->content_len) {
        int chunk = httpd_req_recv(req, body + received, req->content_len - received);
        if (chunk <= 0) return ESP_FAIL;
        received += chunk;
    }
    ArduinoJson::JsonDocument doc;
    if (ArduinoJson::deserializeJson(doc, body) != ArduinoJson::DeserializationError::Ok) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid json");
        return ESP_FAIL;
    }
    const char* url = doc["url"] | "";
    const char* token = doc["token"] | "";
    if (url[0] && strncmp(url, "ws://", 5) != 0 && strncmp(url, "wss://", 6) != 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "url must be ws:// or wss://");
        return ESP_FAIL;
    }
    if (!app->saveBrainConfig(url, token)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "nvs write failed");
        return ESP_FAIL;
    }
    {
        std::lock_guard<std::mutex> lock(app->_mutex);
        app->_brain_url = url;
        app->_brain_token = token;
    }
    app->_brain_config_dirty = true;
    mclog::tagInfo(TAG, "brain config updated: {}", url[0] ? url : "(cleared)");
    char response[360];
    snprintf(response, sizeof(response), R"({"ok":true,"url":"%s"})", url);
    httpd_resp_sendstr(req, response);
    return ESP_OK;
}

struct CameraTaskParams {
    AppRemoteAgent* app;
    char requestId[64];
    bool enhance;
    bool preview;
};

void AppRemoteAgent::cameraTaskEntry(void* arg)
{
    auto* params = static_cast<CameraTaskParams*>(arg);
    AppRemoteAgent* app = params->app;
    char requestId[64];
    strncpy(requestId, params->requestId, sizeof(requestId) - 1);
    requestId[sizeof(requestId) - 1] = 0;
    const bool enhance = params->enhance;
    const bool preview = params->preview;
    delete params;
    if (app->captureAndSendCameraImage(requestId, enhance, preview)) {
        app->sendAck(requestId);
    }
    vTaskDelete(nullptr);
}

void AppRemoteAgent::onRunning()
{
    if (!_opened) return;

    applyPendingStatus();

    if (_connected) {
        if (_hello_pending.exchange(false)) {
            sendHello();
        }
        processMessages();
        updateRenderAnimation();
        if (GetHAL().millis() - _last_telemetry_at > (_standby ? 30000u : 3000u)) {
            sendTelemetry();
        }
    } else if (!_websocket_server) {
        startWebSocketServer();
    }

    maintainBrainClient();

    if (!_tasks_stopping && (!_audio_playback_task.load() || !_audio_capture_task.load() || !_audio_capture_send_task.load())) {
        startAudioTasks();
    }

    {
        LvglLockGuard lock;
        GetStackChan().update();
        view::update_home_indicator();
    }
}

void AppRemoteAgent::startAudioTasks()
{
    _tasks_stopping = false;
    if (!_audio_playback_queue) {
        _audio_playback_queue = xQueueCreate(2, sizeof(AudioPlaybackRequest));
    }
    if (!_audio_playback_ringbuf) {
        _audio_playback_ringbuf = xRingbufferCreateWithCaps(PLAYBACK_RING_BUFFER_BYTES, RINGBUF_TYPE_BYTEBUF, MALLOC_CAP_SPIRAM);
        _audio_playback_ringbuf_in_psram = (_audio_playback_ringbuf != nullptr);
        if (!_audio_playback_ringbuf) {
            _audio_playback_ringbuf = xRingbufferCreate(PLAYBACK_RING_BUFFER_BYTES, RINGBUF_TYPE_BYTEBUF);
        }
    }
    if (!_audio_capture_pcm_queue) {
        _audio_capture_pcm_queue = xQueueCreate(MIC_PCM_QUEUE_DEPTH, sizeof(AudioPcmFrame));
    }
    if (!_audio_playback_task.load()) {
        TaskHandle_t handle = nullptr;
        xTaskCreatePinnedToCore(audioPlaybackTaskEntry, "stacky_audio_out", 8192, this, 3, &handle, 1);
        _audio_playback_task.store(handle);
    }
    if (!_audio_capture_task.load()) {
        TaskHandle_t handle = nullptr;
        xTaskCreatePinnedToCore(audioCaptureTaskEntry, "stacky_audio_in", 8192, this, 3, &handle, 1);
        _audio_capture_task.store(handle);
    }
    if (!_audio_capture_send_task.load()) {
        TaskHandle_t handle = nullptr;
        xTaskCreatePinnedToCore(audioCaptureSendTaskEntry, "stacky_mic_send", 6144, this, 2, &handle, 1);
        _audio_capture_send_task.store(handle);
    }
}

void AppRemoteAgent::stopAudioTasks()
{
    _tasks_stopping = true;
    _audio_streaming = false;
    _audio_start_pending = false;
    cancelPlayback(false);
    const uint32_t started = xTaskGetTickCount();
    while ((_audio_playback_task.load() || _audio_capture_task.load() || _audio_capture_send_task.load()) && xTaskGetTickCount() - started < pdMS_TO_TICKS(4000)) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    bool force_deleted = false;
    if (_audio_playback_task.load()) {
        mclog::tagInfo(TAG, "force deleting audio playback task; send mutex may deadlock");
        vTaskDelete(_audio_playback_task.load());
        _audio_playback_task.store(nullptr);
        force_deleted = true;
    }
    if (_audio_capture_task.load()) {
        mclog::tagInfo(TAG, "force deleting audio capture task; send mutex may deadlock");
        vTaskDelete(_audio_capture_task.load());
        _audio_capture_task.store(nullptr);
        force_deleted = true;
    }
    if (_audio_capture_send_task.load()) {
        mclog::tagInfo(TAG, "force deleting mic send task; send mutex may deadlock");
        vTaskDelete(_audio_capture_send_task.load());
        _audio_capture_send_task.store(nullptr);
        force_deleted = true;
    }
    if (force_deleted) {
        _send_mutex.~mutex();
        new (&_send_mutex) std::mutex();
    }
    if (_audio_playback_queue) {
        vQueueDelete(_audio_playback_queue);
        _audio_playback_queue = nullptr;
    }
    if (_audio_playback_ringbuf) {
        if (_audio_playback_ringbuf_in_psram) {
            vRingbufferDeleteWithCaps(_audio_playback_ringbuf);
        } else {
            vRingbufferDelete(_audio_playback_ringbuf);
        }
        _audio_playback_ringbuf = nullptr;
    }
    if (_audio_capture_pcm_queue) {
        vQueueDelete(_audio_capture_pcm_queue);
        _audio_capture_pcm_queue = nullptr;
    }
}

void AppRemoteAgent::applyPendingStatus()
{
    char mode[24]  = {0};
    char text[160] = {0};
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (!_pending_status_dirty) {
            return;
        }
        snprintf(mode, sizeof(mode), "%s", _pending_mode);
        snprintf(text, sizeof(text), "%s", _pending_text);
        _pending_status_dirty = false;
    }
    setStatus(mode, text);
}

void AppRemoteAgent::processMessages()
{
    std::vector<ReceivedMessage> messages;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        while (!_messages.empty()) {
            messages.push_back(std::move(_messages.front()));
            _messages.pop();
        }
    }
    for (const auto& msg : messages) {
        handleMessage(msg.text);
    }
}

void AppRemoteAgent::logHeap(const char* label)
{
    mclog::tagInfo(TAG, "heap {} free={} min={}", label ? label : "", heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                   heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
}

bool AppRemoteAgent::hasInternalSram(size_t minimum, const char* label)
{
    const size_t free_sram = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    if (free_sram >= minimum) return true;
    mclog::tagInfo(TAG, "low sram for {} free={} required={} min={}", label ? label : "operation", free_sram, minimum,
                   heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
    return false;
}

void AppRemoteAgent::ackPendingAudioStart()
{
    char requestId[64] = {0};
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (!_audio_start_pending) return;
        snprintf(requestId, sizeof(requestId), "%s", _audio_stream_request_id);
        _audio_start_pending = false;
    }
    mclog::tagInfo(TAG, "audio capture ready requestId={}", requestId);
    sendAck(requestId);
}

void AppRemoteAgent::failPendingAudioStart(const char* message)
{
    char requestId[64] = {0};
    bool should_send = false;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_audio_start_pending) {
            snprintf(requestId, sizeof(requestId), "%s", _audio_stream_request_id);
            _audio_stream_request_id[0] = 0;
            _audio_start_pending = false;
            should_send = true;
        }
    }
    if (should_send) {
        mclog::tagInfo(TAG, "audio capture failed requestId={} message={}", requestId, message ? message : "");
        sendError(requestId, message ? message : "audio capture failed");
    }
}

void AppRemoteAgent::resetAudioStartState(const char* requestId)
{
    const uint32_t now = GetHAL().millis();
    {
        std::lock_guard<std::mutex> lock(_mutex);
        strncpy(_audio_stream_request_id, requestId ? requestId : "", sizeof(_audio_stream_request_id) - 1);
        _audio_stream_request_id[sizeof(_audio_stream_request_id) - 1] = 0;
        _audio_start_pending = true;
    }
    _audio_stream_started_at = now;
    _last_audio_frame_sent_at = 0;
    _audio_input_failures = 0;
    _audio_first_input_attempt_logged = false;
    _audio_first_input_success_logged = false;
}

void AppRemoteAgent::handleMessage(const std::string& data)
{
    ArduinoJson::JsonDocument doc;
    auto error = ArduinoJson::deserializeJson(doc, data);
    if (error) {
        sendError(nullptr, "invalid json");
        return;
    }

    const char* type      = doc["type"] | "";
    const char* requestId = doc["requestId"] | "";

    if (strcmp(type, "screen") == 0) {
        const char* mode = doc["mode"] | "connected";
        const char* text = doc["text"] | "";
        setStatus(mode, text);
        sendAck(requestId);
        return;
    }

    if (strcmp(type, "face") == 0) {
        const char* emotion = doc["emotion"] | "neutral";
        if (strcmp(emotion, "none") == 0) {
            hideAvatar();
            sendAck(requestId);
            return;
        }
        auto parsed = parse_emotion(emotion);
        _current_emotion = static_cast<int>(parsed);
        ensureAvatar();
        LvglLockGuard lock;
        if (GetStackChan().hasAvatar()) {
            GetStackChan().avatar().setEmotion(parsed);
        }
        sendAck(requestId);
        return;
    }

    if (strcmp(type, "look") == 0) {
        if (GetHAL().millis() - _last_motion_at < 120) {
            sendError(requestId, "motion rate limited");
            return;
        }
        _yaw       = doc["yaw"].is<int>() ? clamp_int(doc["yaw"].as<int>(), -128, 128) : _yaw;
        _pitch     = doc["pitch"].is<int>() ? clamp_int(doc["pitch"].as<int>(), 5, 85) : _pitch;
        float norm = doc["speed"].is<float>() ? doc["speed"].as<float>() : 0.5f;
        int speed  = clamp_int((int)(norm * 1000), 100, 1000);
        LvglLockGuard lock;
        GetStackChan().motion().moveWithSpeed(_yaw * 10, _pitch * 10, speed);
        _last_motion_at = GetHAL().millis();
        sendAck(requestId);
        return;
    }

    if (strcmp(type, "led") == 0) {
        const char* color = doc["color"] | "#33cc99";
        LvglLockGuard lock;
        GetStackChan().leftNeonLight().setColor(color);
        GetStackChan().rightNeonLight().setColor(color);
        sendAck(requestId);
        return;
    }

    if (strcmp(type, "speak") == 0) {
        const char* audioUrl = doc["audioUrl"] | "";
        const char* audioTransport = doc["audioTransport"] | "";
        const char* playbackId = doc["playbackId"] | "";
        const bool bargeIn = doc["bargeIn"] | false;
        disarmWakeWord(0);
        logHeap("before speak");
        if (!hasInternalSram(MIN_INTERNAL_SRAM_SPEAK, "speak")) {
            sendError(requestId, "low memory for speech");
            return;
        }
        setStatus("speaking", "Speaking...");
        {
            LvglLockGuard lock;
            if (GetStackChan().hasAvatar()) {
                GetStackChan().avatar().clearSpeech();
            }
        }
        if (audioUrl && strlen(audioUrl) > 0) {
            if (queueAudioPlayback(requestId, playbackId, audioUrl, bargeIn)) {
                sendAck(requestId);
            }
            return;
        }
        if (strcmp(audioTransport, "websocket") == 0) {
            if (queueWebSocketAudioPlayback(requestId, playbackId, bargeIn)) {
                sendAck(requestId);
            }
            return;
        }
        setStatus("speaking", "");
        mclog::tagInfo(TAG, "speechDone sent");
        sendPlaybackEvent("speechDone", playbackId);
        sendAck(requestId);
        return;
    }

    if (strcmp(type, "startAudio") == 0) {
        mclog::tagInfo(TAG, "received startAudio requestId={}", requestId);
        _standby = false;
        disarmWakeWord(0);
        _audio_streaming = true;
        if (_audio_capture_task.load() && _audio_capture_send_task.load()) {
            sendAck(requestId);
        } else {
            sendError(requestId, "audio capture task unavailable");
        }
        return;
    }

    if (strcmp(type, "standby") == 0) {
        _standby = true;
        const char* text = doc["text"] | "Standby. Tap to talk.";
        clearRenderScene();
        if (doc["wakeWord"].is<ArduinoJson::JsonObject>()) {
            ArduinoJson::JsonObject wake_word = doc["wakeWord"].as<ArduinoJson::JsonObject>();
            const char* model_id                   = wake_word["modelId"] | "";
            const char* phrase                     = wake_word["phrase"] | "Stacky";
            if (strcmp(model_id, "stacky") != 0 || strcmp(phrase, "Stacky") != 0) {
                mclog::tagInfo(TAG, "wake word model unavailable; falling back to tap standby");
                disarmWakeWord(500);
                text = "Standby. Tap to talk.";
            } else {
                if (!ensureWakeWordDetector(WAKE_WORD_DEFAULT_CUTOFF, WAKE_WORD_DEFAULT_SLIDING_WINDOW) ||
                    !_wake_word_detector->arm()) {
                    mclog::tagInfo(TAG, "wake word detector unavailable; falling back to tap standby");
                    disarmWakeWord(500);
                    text = "Standby. Tap to talk.";
                } else {
                    mclog::tagInfo(TAG, "wake word detector armed; full-duplex mic stream remains active");
                }
            }
        } else {
            disarmWakeWord(500);
        }
        setStatus("standby", text);
        sendAck(requestId);
        return;
    }

    if (strcmp(type, "captureImage") == 0) {
        if (_camera_capture_active.exchange(true)) {
            sendError(requestId, "camera busy");
            return;
        }
        auto* params = new (std::nothrow) CameraTaskParams{};
        if (!params) {
            _camera_capture_active = false;
            sendError(requestId, "camera task alloc failed");
            return;
        }
        params->app = this;
        strncpy(params->requestId, requestId, sizeof(params->requestId) - 1);
        params->requestId[sizeof(params->requestId) - 1] = 0;
        params->enhance = doc["enhance"] | false;
        params->preview = doc["preview"] | false;
        if (xTaskCreatePinnedToCore(cameraTaskEntry, "stacky_camera", 6144, params, 1, nullptr, 1) != pdPASS) {
            _camera_capture_active = false;
            delete params;
            sendError(requestId, "camera task failed");
            return;
        }
        return;
    }

    if (strcmp(type, "volume") == 0) {
        int volume = doc["volume"].is<int>() ? doc["volume"].as<int>() : _volume.load();
        _volume = clamp_int(volume, 0, 100);
        GetHAL().setSpeakerVolume(static_cast<uint8_t>(_volume.load()), false);
        sendAck(requestId);
        return;
    }

    if (strcmp(type, "stopAudio") == 0) {
        mclog::tagInfo(TAG, "received stopAudio requestId={}", requestId);
        sendAck(requestId);
        return;
    }

    if (strcmp(type, "stop") == 0) {
        const char* target = doc["target"] | "all";
        if (strcmp(target, "playback") == 0 || strcmp(target, "speech") == 0) {
            cancelPlayback(true);
            sendAck(requestId);
            return;
        }
        cancelPlayback(true);
        setStatus("connected", "Stopped");
        LvglLockGuard lock;
        GetStackChan().clearModifiers();
        if (GetStackChan().hasAvatar()) {
            for (int id : _decorator_ids) {
                GetStackChan().avatar().removeDecorator(id);
            }
            _decorator_ids.clear();
            GetStackChan().avatar().clearSpeech();
        }
        sendAck(requestId);
        return;
    }

    if (strcmp(type, "playbackClear") == 0) {
        cancelPlayback(true);
        sendAck(requestId);
        return;
    }

    if (strcmp(type, "home") == 0) {
        _yaw = 0;
        _pitch = 35;
        {
            LvglLockGuard lock;
            GetStackChan().motion().moveWithSpeed(_yaw * 10, _pitch * 10, 500);
        }
        setStatus("connected", "Home");
        sendAck(requestId);
        return;
    }

    if (strcmp(type, "avatarJson") == 0) {
        clearRenderScene();
        ensureAvatar();
        LvglLockGuard lock;
        if (GetStackChan().hasAvatar()) {
            GetStackChan().updateAvatarFromJson(data.c_str());
        }
        sendAck(requestId);
        return;
    }

    if (strcmp(type, "render.defineScene") == 0) {
        logHeap("before render.defineScene");
        const char* sceneId = doc["sceneId"] | "";
        if (!sceneId[0]) {
            sendError(requestId, "sceneId required");
            return;
        }
        _render_scene_id = sceneId;
        _render_scene_json = data;
        if (!renderSceneJson(_render_scene_json)) {
            sendError(requestId, "render scene failed");
            return;
        }
        logHeap("after render.defineScene");
        sendAck(requestId);
        return;
    }

    if (strcmp(type, "render.setScene") == 0) {
        logHeap("before render.setScene");
        const char* sceneId = doc["sceneId"] | "";
        if (_render_scene_id.empty() || _render_scene_id != sceneId) {
            sendError(requestId, "scene not defined");
            return;
        }
        if (!renderSceneJson(_render_scene_json)) {
            sendError(requestId, "render scene failed");
            return;
        }
        logHeap("after render.setScene");
        sendAck(requestId);
        return;
    }

    if (strcmp(type, "render.animate") == 0) {
        if (!hasInternalSram(MIN_INTERNAL_SRAM_RENDER, "render.animate")) {
            sendError(requestId, "low memory for render animation");
            return;
        }
        if (!startRenderAnimation(doc, requestId)) {
            return;
        }
        sendAck(requestId);
        return;
    }

    if (strcmp(type, "render.reset") == 0) {
        clearRenderScene();
        sendAck(requestId);
        return;
    }

    if (strcmp(type, "decorator") == 0) {
        const char* action = doc["action"] | "";
        if (strcmp(action, "add") == 0) {
            ensureAvatar();
        }
        LvglLockGuard lock;
        if (GetStackChan().hasAvatar()) {
            if (strcmp(action, "clear") == 0) {
                for (int id : _decorator_ids) {
                    GetStackChan().avatar().removeDecorator(id);
                }
                _decorator_ids.clear();
                sendAck(requestId);
                return;
            }

            if (strcmp(action, "add") == 0) {
                const char* name      = doc["name"] | "";
                uint32_t durationMs   = doc["durationMs"] | 3000;
                uint32_t intervalMs   = doc["animationIntervalMs"] | 500;

                int id = -1;
                if (strcmp(name, "heart") == 0) {
                    id = GetStackChan().avatar().addDecorator(
                        std::make_unique<avatar::HeartDecorator>(lv_screen_active(), durationMs, intervalMs));
                } else if (strcmp(name, "angry") == 0) {
                    id = GetStackChan().avatar().addDecorator(
                        std::make_unique<avatar::AngryDecorator>(lv_screen_active(), durationMs, intervalMs));
                } else if (strcmp(name, "sweat") == 0) {
                    id = GetStackChan().avatar().addDecorator(
                        std::make_unique<avatar::SweatDecorator>(lv_screen_active(), durationMs, intervalMs));
                } else if (strcmp(name, "shy") == 0) {
                    id = GetStackChan().avatar().addDecorator(
                        std::make_unique<avatar::ShyDecorator>(lv_screen_active(), durationMs));
                } else if (strcmp(name, "dizzy") == 0) {
                    id = GetStackChan().avatar().addDecorator(
                        std::make_unique<avatar::DizzyDecorator>(lv_screen_active(), durationMs, intervalMs));
                } else {
                    sendError(requestId, "unknown decorator name");
                    return;
                }
                if (id >= 0) {
                    _decorator_ids.push_back(id);
                }
                sendAck(requestId);
                return;
            }
        }
        sendError(requestId, "unknown decorator action");
        return;
    }

    if (strcmp(type, "ping") == 0) {
        sendAck(requestId);
        return;
    }

    sendError(requestId, "unknown command");
}

void AppRemoteAgent::sendJson(const std::string& data)
{
    std::lock_guard<std::mutex> lock(_send_mutex);
    sendWebSocketFrame(reinterpret_cast<const uint8_t*>(data.data()), data.size(), false);
}

bool AppRemoteAgent::sendWebSocketFrame(const uint8_t* data, size_t len, bool binary)
{
    if (!_connected) {
        return false;
    }

    // A local (server) brain session always owns the send path when present.
    if (_websocket_server && _websocket_fd.load() >= 0) {
        httpd_ws_frame_t frame = {};
        frame.type = binary ? HTTPD_WS_TYPE_BINARY : HTTPD_WS_TYPE_TEXT;
        frame.payload = const_cast<uint8_t*>(data);
        frame.len = len;

        esp_err_t err = httpd_ws_send_data(_websocket_server, _websocket_fd.load(), &frame);
        if (err != ESP_OK) {
            mclog::tagInfo(TAG, "websocket send failed: {}", (int)err);
            _websocket_fd.store(-1);
            _connected = false;
            _hello_pending = false;
            _audio_streaming = false;
            cancelPlayback(false);
            queueStatus("offline", offlineStatusText().c_str());
            return false;
        }
        return true;
    }

    if (_client_link_active.load() && _ws_client) {
        // The client library detects dead links itself and emits DISCONNECTED;
        // a failed send here must not tear down state from an audio task.
        const char* payload = reinterpret_cast<const char*>(data);
        int sent = binary ? esp_websocket_client_send_bin(_ws_client, payload, len, pdMS_TO_TICKS(2000))
                          : esp_websocket_client_send_text(_ws_client, payload, len, pdMS_TO_TICKS(2000));
        return sent >= 0;
    }

    return false;
}

bool AppRemoteAgent::sendPacket(uint8_t type, const uint8_t* data, size_t len)
{
    if (!_connected) {
        return false;
    }
    std::lock_guard<std::mutex> lock(_send_mutex);
    if (!_connected) {
        return false;
    }
    if (len <= 256) {
        std::array<uint8_t, 5 + 256> packet{};
        packet[0] = type;
        packet[1] = (len >> 24) & 0xff;
        packet[2] = (len >> 16) & 0xff;
        packet[3] = (len >> 8) & 0xff;
        packet[4] = len & 0xff;
        if (data && len > 0) {
            memcpy(packet.data() + 5, data, len);
        }
        return sendWebSocketFrame(packet.data(), 5 + len, true);
    }
    std::vector<uint8_t> packet;
    packet.reserve(5 + len);
    packet.push_back(type);
    packet.push_back((len >> 24) & 0xff);
    packet.push_back((len >> 16) & 0xff);
    packet.push_back((len >> 8) & 0xff);
    packet.push_back(len & 0xff);
    if (data && len > 0) {
        packet.insert(packet.end(), data, data + len);
    }
    return sendWebSocketFrame(packet.data(), packet.size(), true);
}

bool AppRemoteAgent::sendPacketIfSendIdle(uint8_t type, const uint8_t* data, size_t len)
{
    if (!_connected || len > sizeof(AudioPcmFrame::data)) {
        return false;
    }
    if (!_send_mutex.try_lock()) {
        _mic_frames_dropped.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    std::unique_lock<std::mutex> lock(_send_mutex, std::adopt_lock);
    if (!_connected) {
        return false;
    }
    std::array<uint8_t, 5 + sizeof(AudioPcmFrame::data)> packet{};
    packet[0] = type;
    packet[1] = (len >> 24) & 0xff;
    packet[2] = (len >> 16) & 0xff;
    packet[3] = (len >> 8) & 0xff;
    packet[4] = len & 0xff;
    if (data && len > 0) {
        memcpy(packet.data() + 5, data, len);
    }
    return sendWebSocketFrame(packet.data(), 5 + len, true);
}

void AppRemoteAgent::sendHello()
{
    auto id = GetHAL().getFactoryMacString("");
    const bool wake_word_ready = ensureWakeWordDetector(WAKE_WORD_DEFAULT_CUTOFF, WAKE_WORD_DEFAULT_SLIDING_WINDOW);
    char buffer[1600];
    if (wake_word_ready) {
        snprintf(buffer, sizeof(buffer),
                  R"({"type":"hello","id":"stacky-%s","version":2,"capabilities":["screen","face","look","led","telemetry","tap","hold","audio","fullDuplexAudio","playbackControl","bargeIn","camera","volume","standby","wakeWord","render"],"wakeWord":{"version":1,"models":[{"id":"stacky","phrase":"Stacky","sampleRate":16000,"cutoff":0.99,"slidingWindow":10,"source":"firmware"}],"dynamicModels":false},"render":{"version":1,"screen":{"width":320,"height":240,"fps":30},"primitives":["group","circle","ellipse","rect"],"transforms":["translate","scale","rotate","opacity"],"animations":["keyframes","audioLevel"],"audioLevelSources":["playback","mic","any"],"limits":{"maxNodes":64,"maxSceneBytes":16384,"maxAnimationMs":300000,"maxActiveAnimations":4,"maxActiveTracks":32}}})",
                  id.c_str());
    } else {
        snprintf(buffer, sizeof(buffer),
                  R"({"type":"hello","id":"stacky-%s","version":2,"capabilities":["screen","face","look","led","telemetry","tap","hold","audio","fullDuplexAudio","playbackControl","bargeIn","camera","volume","standby","render"],"render":{"version":1,"screen":{"width":320,"height":240,"fps":30},"primitives":["group","circle","ellipse","rect"],"transforms":["translate","scale","rotate","opacity"],"animations":["keyframes","audioLevel"],"audioLevelSources":["playback","mic","any"],"limits":{"maxNodes":64,"maxSceneBytes":16384,"maxAnimationMs":300000,"maxActiveAnimations":4,"maxActiveTracks":32}}})",
                  id.c_str());
    }
    sendJson(buffer);
}

void AppRemoteAgent::sendTelemetry()
{
    _last_telemetry_at = GetHAL().millis();
    char buffer[256];
    snprintf(buffer, sizeof(buffer),
             R"({"type":"telemetry","battery":%d,"charging":%s,"wifiRssi":0,"pose":{"yaw":%d,"pitch":%d},"volume":%d,"micDropped":%u})",
             (int)GetHAL().getBatteryLevel(), GetHAL().isBatteryCharging() ? "true" : "false", _yaw, _pitch, _volume.load(),
             (unsigned)_mic_frames_dropped.load(std::memory_order_relaxed));
    sendJson(buffer);
}

void AppRemoteAgent::sendAck(const char* requestId)
{
    char buffer[128];
    snprintf(buffer, sizeof(buffer), R"({"type":"ack","requestId":"%s","ok":true})", requestId ? requestId : "");
    sendJson(buffer);
}

void AppRemoteAgent::sendError(const char* requestId, const char* message)
{
    char buffer[192];
    snprintf(buffer, sizeof(buffer), R"({"type":"error","requestId":"%s","message":"%s"})", requestId ? requestId : "",
             message ? message : "error");
    sendJson(buffer);
}

void AppRemoteAgent::sendPlaybackEvent(const char* event, const char* playbackId)
{
    char buffer[192];
    if (playbackId && playbackId[0]) {
        snprintf(buffer, sizeof(buffer), R"({"type":"event","event":"%s","playbackId":"%s"})", event, playbackId);
    } else {
        snprintf(buffer, sizeof(buffer), R"({"type":"event","event":"%s"})", event);
    }
    sendJson(buffer);
}

void AppRemoteAgent::sendBargeInEvent(const char* playbackId)
{
    char buffer[224];
    const unsigned long at = (unsigned long)GetHAL().millis();
    if (playbackId && playbackId[0]) {
        snprintf(buffer, sizeof(buffer), R"({"type":"event","event":"bargeIn","playbackId":"%s","at":%lu})", playbackId, at);
    } else {
        snprintf(buffer, sizeof(buffer), R"({"type":"event","event":"bargeIn","at":%lu})", at);
    }
    sendJson(buffer);
}

void AppRemoteAgent::ensureAvatar()
{
    if (GetStackChan().hasAvatar()) return;
    LvglLockGuard lock;
    auto avatar = std::make_unique<avatar::DefaultAvatar>();
    avatar->init(lv_screen_active());
    avatar->setEmotion(static_cast<avatar::Emotion>(_current_emotion));
    GetStackChan().attachAvatar(std::move(avatar));
    if (_root) {
        lv_obj_move_foreground(_root);
    }
}

void AppRemoteAgent::hideAvatar()
{
    LvglLockGuard lock;
    if (GetStackChan().hasAvatar()) {
        GetStackChan().avatar().clearSpeech();
    }
    for (int id : _decorator_ids) {
        if (GetStackChan().hasAvatar()) {
            GetStackChan().avatar().removeDecorator(id);
        }
    }
    _decorator_ids.clear();
    GetStackChan().resetAvatar();
}

void AppRemoteAgent::clearRenderScene()
{
    LvglLockGuard lock;
    stopRenderAnimation();
    if (_render_root) {
        lv_obj_delete(_render_root);
        _render_root = nullptr;
    }
    _render_nodes.clear();
    _render_active = false;
    moveStatusChromeForeground();
}

bool AppRemoteAgent::renderSceneJson(const std::string& data)
{
    ArduinoJson::JsonDocument doc;
    auto error = ArduinoJson::deserializeJson(doc, data);
    if (error) return false;

    auto nodes = doc["nodes"].as<ArduinoJson::JsonArray>();
    if (nodes.isNull() || nodes.size() > 64) return false;

    LvglLockGuard lock;
    if (GetStackChan().hasAvatar()) {
        GetStackChan().avatar().clearSpeech();
    }
    GetStackChan().resetAvatar();

    if (_render_root) {
        stopRenderAnimation();
        lv_obj_delete(_render_root);
        _render_root = nullptr;
    }
    _render_nodes.clear();
    _render_active = false;

    if (!hasInternalSram(MIN_INTERNAL_SRAM_RENDER, "render scene")) return false;

    _render_root = lv_obj_create(lv_screen_active());
    lv_obj_set_size(_render_root, 320, 240);
    lv_obj_align(_render_root, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_border_width(_render_root, 0, 0);
    lv_obj_set_style_pad_all(_render_root, 0, 0);
    lv_obj_set_style_bg_color(_render_root, parse_hex_color(doc["background"] | "#05070d", 0x05070d), 0);
    lv_obj_set_style_bg_opa(_render_root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(_render_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(_render_root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(_render_root, panel_click_cb, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(_render_root, panel_click_cb, LV_EVENT_LONG_PRESSED, this);

    std::vector<RenderFrame> frames;
    frames.reserve(nodes.size());

    for (auto node : nodes) {
        const char* id = node["id"] | "";
        const char* kind = node["kind"] | "";
        const char* parent = node["parent"] | "";
        if (!id[0] || !kind[0]) continue;

        RenderFrame frame;
        strncpy(frame.id, id, sizeof(frame.id) - 1);
        frame.x = node["x"].is<int>() ? node["x"].as<int>() : 0;
        frame.y = node["y"].is<int>() ? node["y"].as<int>() : 0;

        RenderFrame parent_frame;
        if (find_render_frame(frames, parent, parent_frame)) {
            frame.x += parent_frame.x;
            frame.y += parent_frame.y;
        }

        frames.push_back(frame);
        if (strcmp(kind, "group") == 0) continue;

        int w = 0;
        int h = 0;
        int radius = 0;
        if (strcmp(kind, "circle") == 0) {
            int r = node["r"].is<int>() ? node["r"].as<int>() : 1;
            w = h = std::max(1, r * 2);
            radius = LV_RADIUS_CIRCLE;
        } else if (strcmp(kind, "ellipse") == 0) {
            int rx = node["rx"].is<int>() ? node["rx"].as<int>() : 1;
            int ry = node["ry"].is<int>() ? node["ry"].as<int>() : 1;
            w = std::max(1, rx * 2);
            h = std::max(1, ry * 2);
            radius = LV_RADIUS_CIRCLE;
        } else if (strcmp(kind, "rect") == 0) {
            w = node["width"].is<int>() ? std::max(1, node["width"].as<int>()) : 1;
            h = node["height"].is<int>() ? std::max(1, node["height"].as<int>()) : 1;
            radius = node["radius"].is<int>() ? std::max(0, node["radius"].as<int>()) : 0;
        } else {
            continue;
        }

        auto* obj = lv_obj_create(_render_root);
        lv_obj_set_size(obj, w, h);
        lv_obj_set_pos(obj, frame.x - w / 2, frame.y - h / 2);
        lv_obj_set_style_transform_pivot_x(obj, w / 2, 0);
        lv_obj_set_style_transform_pivot_y(obj, h / 2, 0);
        lv_obj_set_style_radius(obj, radius, 0);
        lv_obj_set_style_border_width(obj, node["strokeWidth"].is<int>() ? node["strokeWidth"].as<int>() : 0, 0);
        lv_obj_set_style_border_color(obj, parse_hex_color(node["stroke"] | "#000000", 0x000000), 0);
        lv_obj_set_style_bg_color(obj, parse_hex_color(node["fill"] | "#eef7ff", 0xeef7ff), 0);
        int opacity = node["opacity"].is<float>() ? clamp_int((int)(node["opacity"].as<float>() * 255), 0, 255) : 255;
        lv_obj_set_style_bg_opa(obj, opacity, 0);
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
        if (node["rotation"].is<int>()) {
            lv_obj_set_style_transform_rotation(obj, node["rotation"].as<int>() * 10, 0);
        }
        if (node["scaleX"].is<float>()) {
            lv_obj_set_style_transform_scale_x(obj, (int)(node["scaleX"].as<float>() * 256), 0);
        }
        if (node["scaleY"].is<float>()) {
            lv_obj_set_style_transform_scale_y(obj, (int)(node["scaleY"].as<float>() * 256), 0);
        }
        if (node["visible"].is<bool>() && !node["visible"].as<bool>()) {
            lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
        }

        RenderNodeRef ref;
        strncpy(ref.id, id, sizeof(ref.id) - 1);
        ref.obj = obj;
        ref.x = frame.x;
        ref.y = frame.y;
        ref.w = w;
        ref.h = h;
        ref.scaleX = read_float(node["scaleX"], 1.0f);
        ref.scaleY = read_float(node["scaleY"], 1.0f);
        ref.rotation = read_float(node["rotation"], 0.0f);
        ref.opacity = read_float(node["opacity"], 1.0f);
        _render_nodes.push_back(ref);
    }

    _render_active = true;
    lv_obj_move_foreground(_render_root);
    moveStatusChromeForeground();
    return true;
}

void AppRemoteAgent::moveStatusChromeForeground()
{
    if (_root) {
        lv_obj_move_foreground(_root);
    }
    if (_status_dot) {
        lv_obj_clear_flag(_status_dot, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(_status_dot);
    }
    if (_main_label) {
        lv_obj_move_foreground(_main_label);
    }
    if (_log_label) {
        lv_obj_move_foreground(_log_label);
    }
}

bool AppRemoteAgent::startRenderAnimation(ArduinoJson::JsonDocument& doc, const char* requestId)
{
    if (!_render_active || !_render_root) {
        sendError(requestId, "render scene required");
        return false;
    }

    auto tracks = doc["tracks"].as<ArduinoJson::JsonArray>();
    if (tracks.isNull() || tracks.size() == 0 || tracks.size() > 32) {
        sendError(requestId, "animation tracks required");
        return false;
    }

    const char* animationId = doc["animationId"] | "";
    if (!animationId[0]) {
        sendError(requestId, "animationId required");
        return false;
    }

    size_t existing_tracks = 0;
    for (const auto& animation : _render_animations) {
        if (strcmp(animation.animationId, animationId) != 0) {
            existing_tracks += animation.tracks.size();
        }
    }
    if (existing_tracks + tracks.size() > MAX_RENDER_TRACKS) {
        sendError(requestId, "too many animation tracks");
        return false;
    }

    std::vector<RenderTrack> next_tracks;
    uint32_t duration = 0;
    for (auto track_doc : tracks) {
        const char* target = track_doc["target"] | "";
        const char* property = track_doc["property"] | "";
        auto keyframes = track_doc["keyframes"].as<ArduinoJson::JsonArray>();
        if (!target[0] || !property[0] || keyframes.isNull() || keyframes.size() == 0 || keyframes.size() > 64) {
            sendError(requestId, "invalid animation track");
            return false;
        }
        if (!find_render_node(_render_nodes, target)) {
            sendError(requestId, "unknown animation target");
            return false;
        }
        if (strcmp(property, "x") != 0 && strcmp(property, "y") != 0 && strcmp(property, "scaleX") != 0 &&
            strcmp(property, "scaleY") != 0 && strcmp(property, "rotation") != 0 && strcmp(property, "opacity") != 0) {
            sendError(requestId, "unknown animation property");
            return false;
        }

        RenderTrack track;
        strncpy(track.animationId, animationId, sizeof(track.animationId) - 1);
        strncpy(track.target, target, sizeof(track.target) - 1);
        strncpy(track.property, property, sizeof(track.property) - 1);
        auto audioLevel = track_doc["audioLevel"];
        if (!audioLevel.isNull()) {
            track.audioReactive = true;
            const char* source = audioLevel["source"] | "playback";
            strncpy(track.audioSource, source, sizeof(track.audioSource) - 1);
            track.audioScale = read_float(audioLevel["scale"], 1.0f);
            track.audioOffset = read_float(audioLevel["offset"], 0.0f);
            if (audioLevel["min"].is<float>() || audioLevel["min"].is<int>()) {
                track.audioMin = read_float(audioLevel["min"], 0.0f);
                track.hasAudioMin = true;
            }
            if (audioLevel["max"].is<float>() || audioLevel["max"].is<int>()) {
                track.audioMax = read_float(audioLevel["max"], 0.0f);
                track.hasAudioMax = true;
            }
        }
        for (auto keyframe_doc : keyframes) {
            RenderKeyframe keyframe;
            keyframe.t = keyframe_doc["t"].is<int>() ? std::max(0, keyframe_doc["t"].as<int>()) : 0;
            keyframe.value = read_float(keyframe_doc["value"], 0.0f);
            duration = std::max(duration, keyframe.t);
            track.keyframes.push_back(keyframe);
        }
        std::sort(track.keyframes.begin(), track.keyframes.end(), [](const RenderKeyframe& a, const RenderKeyframe& b) {
            return a.t < b.t;
        });
        next_tracks.push_back(track);
    }

    if (duration > 300000) {
        sendError(requestId, "animation too long");
        return false;
    }

    _render_animations.erase(std::remove_if(_render_animations.begin(), _render_animations.end(), [animationId](const RenderAnimationState& animation) {
        return strcmp(animation.animationId, animationId) == 0;
    }), _render_animations.end());
    if (_render_animations.size() >= MAX_RENDER_ANIMATIONS) {
        sendError(requestId, "too many animations");
        return false;
    }

    RenderAnimationState animation;
    strncpy(animation.animationId, animationId, sizeof(animation.animationId) - 1);
    animation.tracks = std::move(next_tracks);
    animation.duration = duration;
    animation.loop = doc["loop"] | false;
    animation.yoyo = doc["yoyo"] | false;
    animation.startedAt = GetHAL().millis();
    _render_animations.push_back(std::move(animation));
    _render_animation_last_frame_at = 0;
    return true;
}

void AppRemoteAgent::stopRenderAnimation()
{
    _render_animations.clear();
    _render_animation_last_frame_at = 0;
}

void AppRemoteAgent::updateRenderAnimation()
{
    if (!_render_active || _render_animations.empty()) return;

    uint32_t now = GetHAL().millis();
    if (_render_animation_last_frame_at != 0 && now - _render_animation_last_frame_at < 33) return;
    _render_animation_last_frame_at = now;

    LvglLockGuard lock;
    for (const auto& animation : _render_animations) {
        uint32_t elapsed = now - animation.startedAt;
        if (animation.loop) {
            uint32_t cycle = animation.yoyo ? animation.duration * 2 : animation.duration;
            if (cycle == 0) continue;
            uint32_t cycle_t = elapsed % cycle;
            elapsed = animation.yoyo && cycle_t > animation.duration ? cycle - cycle_t : cycle_t;
        } else if (elapsed > animation.duration) {
            elapsed = animation.duration;
        }

        for (const auto& track : animation.tracks) {
            auto* node = find_render_node(_render_nodes, track.target);
            if (!node || !node->obj) continue;
            float value = interpolate_keyframes(track.keyframes, elapsed);
            if (track.audioReactive) {
                int source_level = 0;
                if (strcmp(track.audioSource, "mic") == 0) {
                    source_level = _mic_audio_level.load();
                } else if (strcmp(track.audioSource, "any") == 0) {
                    source_level = std::max(_mic_audio_level.load(), _playback_audio_level.load());
                } else {
                    source_level = _playback_audio_level.load();
                }
                value += (static_cast<float>(source_level) / 1000.0f) * track.audioScale + track.audioOffset;
                if (track.hasAudioMin) value = std::max(track.audioMin, value);
                if (track.hasAudioMax) value = std::min(track.audioMax, value);
            }
            if (strcmp(track.property, "x") == 0) {
                node->x = static_cast<int>(value);
                lv_obj_set_pos(node->obj, node->x - node->w / 2, node->y - node->h / 2);
            } else if (strcmp(track.property, "y") == 0) {
                node->y = static_cast<int>(value);
                lv_obj_set_pos(node->obj, node->x - node->w / 2, node->y - node->h / 2);
            } else if (strcmp(track.property, "scaleX") == 0) {
                node->scaleX = value;
                lv_obj_set_style_transform_scale_x(node->obj, static_cast<int>(value * 256.0f), 0);
            } else if (strcmp(track.property, "scaleY") == 0) {
                node->scaleY = value;
                lv_obj_set_style_transform_scale_y(node->obj, static_cast<int>(value * 256.0f), 0);
            } else if (strcmp(track.property, "rotation") == 0) {
                node->rotation = value;
                lv_obj_set_style_transform_rotation(node->obj, static_cast<int>(value * 10.0f), 0);
            } else if (strcmp(track.property, "opacity") == 0) {
                node->opacity = value;
                lv_obj_set_style_bg_opa(node->obj, clamp_int(static_cast<int>(value * 255.0f), 0, 255), 0);
            }
        }
    }

    _render_animations.erase(std::remove_if(_render_animations.begin(), _render_animations.end(), [now](const RenderAnimationState& animation) {
        return !animation.loop && now - animation.startedAt >= animation.duration;
    }), _render_animations.end());
}

void AppRemoteAgent::setStatus(const char* mode, const char* text)
{
    mclog::tagInfo(TAG, "{}: {}", mode, text);
    if (is_visible_status_mode(mode) && !_render_active) {
        ensureAvatar();
    } else if (!is_visible_status_mode(mode) && !_render_active) {
        hideAvatar();
    }
    LvglLockGuard lock;
    if (_status_dot) {
        lv_obj_set_style_bg_color(_status_dot, status_color(mode), 0);
        lv_obj_clear_flag(_status_dot, LV_OBJ_FLAG_HIDDEN);
    }
    if (_main_label) {
        const bool waiting_for_brain = mode && strcmp(mode, "offline") == 0 && !_connected;
        const bool show_main = !_render_active && !waiting_for_brain && mode && (strcmp(mode, "connected") == 0 || strcmp(mode, "standby") == 0 || strcmp(mode, "error") == 0 || strcmp(mode, "connecting") == 0);
        lv_label_set_text(_main_label, show_main && text ? text : "");
    }
    if (_log_label) {
        if (is_visible_status_mode(mode)) {
            lv_label_set_text(_log_label, text ? text : "");
        } else if (_render_active) {
            lv_label_set_text(_log_label, "");
        } else {
            lv_label_set_text(_log_label, _connected ? "" : (text ? text : "Waiting for brain"));
        }
    }
    moveStatusChromeForeground();
}

void AppRemoteAgent::queueStatus(const char* mode, const char* text)
{
    std::lock_guard<std::mutex> lock(_mutex);
    strncpy(_pending_mode, mode ? mode : "connected", sizeof(_pending_mode) - 1);
    _pending_mode[sizeof(_pending_mode) - 1] = 0;
    strncpy(_pending_text, text ? text : "", sizeof(_pending_text) - 1);
    _pending_text[sizeof(_pending_text) - 1] = 0;
    _pending_status_dirty = true;
}

void AppRemoteAgent::setLog(const char* text)
{
    LvglLockGuard lock;
    if (_log_label) {
        lv_label_set_text(_log_label, text);
    }
    moveStatusChromeForeground();
}

void AppRemoteAgent::handleTap()
{
    disarmWakeWord(500);
    setLog("tap sent to brain");
    if (_audio_playback_active || _audio_playback_pending) {
        cancelPlayback(true);
    }
    char buffer[96];
    snprintf(buffer, sizeof(buffer), R"({"type":"event","event":"tap","at":%lu})", (unsigned long)GetHAL().millis());
    sendJson(buffer);
}

void AppRemoteAgent::handleHold()
{
    disarmWakeWord(500);
    setLog("hold sent to brain");
    if (_audio_playback_active || _audio_playback_pending) {
        cancelPlayback(true);
    }
    char buffer[96];
    snprintf(buffer, sizeof(buffer), R"({"type":"event","event":"hold","at":%lu})", (unsigned long)GetHAL().millis());
    sendJson(buffer);
}

bool AppRemoteAgent::ensureWakeWordDetector(float cutoff, size_t sliding_window)
{
    if (_wake_word_detector) {
        return true;
    }
    if (!hasInternalSram(MIN_INTERNAL_SRAM_WAKE_WORD, "wakeWord")) {
        return false;
    }

    auto detector = std::make_unique<StackyWakeWordDetector>(cutoff, sliding_window);
    if (!detector->begin([this](const std::string& wake_word) { handleWakeWordDetected(wake_word); })) {
        return false;
    }
    _wake_word_detector = std::move(detector);
    return true;
}

void AppRemoteAgent::disarmWakeWord(uint32_t wait_ms)
{
    if (_wake_word_detector) {
        _wake_word_detector->disarm(wait_ms);
    }
}

void AppRemoteAgent::releaseWakeWordDetector()
{
    if (!_wake_word_detector) return;
    _wake_word_detector->shutdown();
    _wake_word_detector.reset();
}

void AppRemoteAgent::handleWakeWordDetected(const std::string& wake_word)
{
    queueStatus("listening", "Wake word heard");
    char buffer[192];
    snprintf(buffer, sizeof(buffer),
             R"({"type":"event","event":"wakeWord","wakeWord":"%s","modelId":"stacky","score":1.0,"at":%lu})",
             wake_word.c_str(), (unsigned long)GetHAL().millis());
    sendJson(buffer);
}

bool AppRemoteAgent::captureAndSendAudioFrame()
{
    if (!_connected || !_audio_capture_pcm_queue) {
        return false;
    }

    auto audio_codec = Board::GetInstance().GetAudioCodec();
    if (!audio_codec) {
        queueStatus("error", "Audio codec unavailable");
        failPendingAudioStart("audio codec unavailable");
        return false;
    }

    constexpr size_t chunk_frames = 512;
    const size_t input_channels = std::max(audio_codec->input_channels(), 1);
    const size_t input_samples = chunk_frames * input_channels;
    if (_audio_input_chunk.size() != input_samples) {
        _audio_input_chunk.resize(input_samples);
    }
    if (!audio_codec->InputData(_audio_input_chunk)) {
        _audio_input_failures++;
        vTaskDelay(pdMS_TO_TICKS(5));
        return false;
    }
    if (!_audio_first_input_success_logged.exchange(true)) {
        mclog::tagInfo(TAG, "first successful InputData");
    }
    if (_wake_word_detector && _wake_word_detector->isArmed()) {
        const int input_sample_rate = audio_codec->input_sample_rate() > 0 ? audio_codec->input_sample_rate() : 24000;
        _wake_word_detector->feedAudio(_audio_input_chunk.data(), chunk_frames, input_sample_rate, static_cast<int>(input_channels));
    }
    if (chunk_frames * 2 > sizeof(AudioPcmFrame::data)) {
        return false;
    }
    AudioPcmFrame frame;
    frame.len = chunk_frames * 2;
    uint64_t total = 0;
    if (input_channels == 1) {
        memcpy(frame.data, _audio_input_chunk.data(), chunk_frames * sizeof(int16_t));
        for (size_t i = 0; i < chunk_frames; ++i) {
            total += static_cast<uint16_t>(std::abs(static_cast<int>(_audio_input_chunk[i])));
        }
    } else {
        for (size_t i = 0; i < chunk_frames; ++i) {
            int16_t sample = _audio_input_chunk[i * input_channels];
            total += static_cast<uint16_t>(std::abs(static_cast<int>(sample)));
            frame.data[i * 2] = sample & 0xff;
            frame.data[i * 2 + 1] = (sample >> 8) & 0xff;
        }
    }
    const int average = static_cast<int>(total / chunk_frames);
    const int level = clamp_int((average * 1000) / 12000, 0, 1000);
    _mic_audio_level = smooth_level_1000(_mic_audio_level.load(), level);
    if (_audio_playback_active && _barge_in_enabled && level >= BARGE_IN_LEVEL_THRESHOLD && !_barge_in_reported.exchange(true)) {
        char playbackId[64] = {0};
        copyCurrentPlaybackId(playbackId, sizeof(playbackId));
        sendBargeInEvent(playbackId);
    }

    if (xQueueSend(_audio_capture_pcm_queue, &frame, 0) != pdTRUE) {
        AudioPcmFrame dropped;
        xQueueReceive(_audio_capture_pcm_queue, &dropped, 0);
        if (xQueueSend(_audio_capture_pcm_queue, &frame, 0) != pdTRUE) {
            _audio_input_failures++;
            _mic_frames_dropped.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
    }
    return true;
}

void AppRemoteAgent::setCurrentPlaybackId(const char* playbackId)
{
    std::lock_guard<std::mutex> lock(_mutex);
    strncpy(_current_playback_id, playbackId ? playbackId : "", sizeof(_current_playback_id) - 1);
    _current_playback_id[sizeof(_current_playback_id) - 1] = 0;
}

void AppRemoteAgent::copyCurrentPlaybackId(char* playbackId, size_t len)
{
    if (!playbackId || len == 0) return;
    std::lock_guard<std::mutex> lock(_mutex);
    snprintf(playbackId, len, "%s", _current_playback_id);
}

uint32_t AppRemoteAgent::nextPlaybackGeneration()
{
    return _audio_playback_generation.fetch_add(1) + 1;
}

void AppRemoteAgent::cancelPlayback(bool emit_event)
{
    const bool was_active = _audio_playback_active.exchange(false);
    const bool was_pending = _audio_playback_pending.exchange(false);
    const bool was_accepting = _websocket_playback_accepting.exchange(false);
    _audio_playback_cancel = true;
    _websocket_playback_eos = true;
    _barge_in_enabled = false;
    _barge_in_reported = false;
    _playback_audio_level = 0;
    _playback_queue_overflows = 0;
    nextPlaybackGeneration();

    if (_audio_playback_queue) {
        xQueueReset(_audio_playback_queue);
    }

    if (emit_event && (was_active || was_pending || was_accepting) && !_audio_playback_interrupted_reported.exchange(true)) {
        char playbackId[64] = {0};
        copyCurrentPlaybackId(playbackId, sizeof(playbackId));
        mclog::tagInfo(TAG, "speechInterrupted sent");
        sendPlaybackEvent("speechInterrupted", playbackId);
    }
}

bool AppRemoteAgent::queueAudioPlayback(const char* requestId, const char* playbackId, const char* url, bool bargeIn)
{
    if (!_audio_playback_queue) {
        sendError(requestId, "audio playback unavailable");
        return false;
    }
    cancelPlayback(true);
    AudioPlaybackRequest request;
    strncpy(request.requestId, requestId ? requestId : "", sizeof(request.requestId) - 1);
    strncpy(request.playbackId, playbackId ? playbackId : "", sizeof(request.playbackId) - 1);
    strncpy(request.url, url ? url : "", sizeof(request.url) - 1);
    request.generation = nextPlaybackGeneration();
    request.bargeIn = bargeIn;
    setCurrentPlaybackId(request.playbackId);
    _audio_playback_pending = true;
    _audio_playback_interrupted_reported = false;
    _playback_queue_overflows = 0;
    if (xQueueSend(_audio_playback_queue, &request, 0) != pdTRUE) {
        _audio_playback_pending = false;
        sendError(requestId, "audio playback queue full");
        return false;
    }
    return true;
}

bool AppRemoteAgent::queueWebSocketAudioPlayback(const char* requestId, const char* playbackId, bool bargeIn)
{
    if (!_audio_playback_queue || !_audio_playback_ringbuf) {
        sendError(requestId, "audio playback unavailable");
        return false;
    }
    cancelPlayback(true);
    AudioPlaybackRequest request;
    strncpy(request.requestId, requestId ? requestId : "", sizeof(request.requestId) - 1);
    strncpy(request.playbackId, playbackId ? playbackId : "", sizeof(request.playbackId) - 1);
    request.generation = nextPlaybackGeneration();
    request.websocket = true;
    request.bargeIn = bargeIn;
    setCurrentPlaybackId(request.playbackId);
    _audio_playback_pending = true;
    _audio_playback_interrupted_reported = false;
    _websocket_playback_accepting = true;
    _websocket_playback_eos = false;
    _playback_queue_overflows = 0;
    if (xQueueSend(_audio_playback_queue, &request, 0) != pdTRUE) {
        _audio_playback_pending = false;
        _websocket_playback_accepting = false;
        sendError(requestId, "audio playback queue full");
        return false;
    }
    return true;
}

void AppRemoteAgent::drainPlaybackRingBuffer()
{
    if (!_audio_playback_ringbuf) return;
    while (true) {
        size_t len = 0;
        auto* item = static_cast<uint8_t*>(xRingbufferReceiveUpTo(_audio_playback_ringbuf, &len, 0, PLAYBACK_READ_BYTES));
        if (!item) break;
        vRingbufferReturnItem(_audio_playback_ringbuf, item);
    }
}

size_t AppRemoteAgent::playbackRingBufferUsed() const
{
    if (!_audio_playback_ringbuf) return 0;
    const size_t free_size = xRingbufferGetCurFreeSize(_audio_playback_ringbuf);
    return free_size >= PLAYBACK_RING_BUFFER_BYTES ? 0 : PLAYBACK_RING_BUFFER_BYTES - free_size;
}

void AppRemoteAgent::queueWebSocketAudioFrame(const uint8_t* data, size_t len)
{
    if (!_audio_playback_ringbuf) return;
    if (!_websocket_playback_accepting) return;
    if (!data || len == 0) {
        _websocket_playback_eos = true;
        return;
    }
    if (xRingbufferSend(_audio_playback_ringbuf, data, len, 0) != pdTRUE) {
        const uint32_t overflows = _playback_queue_overflows.fetch_add(1) + 1;
        const uint32_t now = GetHAL().millis();
        if (now - _last_playback_queue_full_log_at.load() > PLAYBACK_QUEUE_FULL_LOG_INTERVAL_MS) {
            _last_playback_queue_full_log_at = now;
            mclog::tagInfo(TAG, "websocket playback ring full; dropped {} bytes, overflows={}", (int)len,
                           (unsigned)overflows);
        }
    }
}

void AppRemoteAgent::audioPlaybackLoop()
{
    AudioPlaybackRequest request;
    while (!_tasks_stopping) {
        if (!_audio_playback_queue || xQueueReceive(_audio_playback_queue, &request, pdMS_TO_TICKS(100)) != pdTRUE) {
            continue;
        }
        _audio_playback_cancel = false;
        _audio_playback_pending = false;
        _audio_playback_interrupted_reported = false;
        _barge_in_enabled = request.bargeIn;
        _barge_in_reported = false;
        setCurrentPlaybackId(request.playbackId);
        bool completed = true;
        if (request.websocket) {
            completed = playWebSocketAudio(request);
        } else if (request.url[0]) {
            completed = playAudioUrl(request.url, request.playbackId);
        }
        _barge_in_enabled = false;
        _barge_in_reported = false;
        if (completed) {
            queueStatus("speaking", "");
            mclog::tagInfo(TAG, "speechDone sent");
            sendPlaybackEvent("speechDone", request.playbackId);
        } else if (!_audio_playback_interrupted_reported.exchange(true)) {
            queueStatus("connected", "Ready");
            mclog::tagInfo(TAG, "speechInterrupted sent");
            sendPlaybackEvent("speechInterrupted", request.playbackId);
        }
    }
    _audio_playback_task.store(nullptr);
    vTaskDelete(nullptr);
}

void AppRemoteAgent::audioCaptureSendLoop()
{
    AudioPcmFrame frame;
    while (!_tasks_stopping) {
        if (!_audio_capture_pcm_queue) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        if (!_connected) {
            xQueueReset(_audio_capture_pcm_queue);
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        if (xQueueReceive(_audio_capture_pcm_queue, &frame, pdMS_TO_TICKS(100)) != pdTRUE) {
            continue;
        }
        if (!_connected) {
            continue;
        }
        if (!sendPacketIfSendIdle(PACKET_AUDIO_MIC_PCM, frame.data, frame.len)) {
            _audio_input_failures++;
            continue;
        }
        if (_last_audio_frame_sent_at.load() == 0) {
            mclog::tagInfo(TAG, "first PCM frame sent from mic send task");
        }
        _last_audio_frame_sent_at = GetHAL().millis();
        _audio_input_failures = 0;
        ackPendingAudioStart();
    }
    _audio_capture_send_task.store(nullptr);
    vTaskDelete(nullptr);
}

void AppRemoteAgent::audioCaptureLoop()
{
    bool input_enabled = false;
    while (!_tasks_stopping) {
        if (_connected) {
            if (!input_enabled) {
                mclog::tagInfo(TAG, "audio capture loop sees connected true");
                auto audio_codec = Board::GetInstance().GetAudioCodec();
                if (!audio_codec) {
                    queueStatus("error", "Audio codec unavailable");
                    failPendingAudioStart("audio codec unavailable");
                    vTaskDelay(pdMS_TO_TICKS(100));
                    continue;
                }
                mclog::tagInfo(TAG, "EnableInput(true) start");
                audio_codec->EnableInput(true);
                mclog::tagInfo(TAG, "EnableInput(true) end");
                input_enabled = true;
            }
            if (!_audio_first_input_attempt_logged.exchange(true)) {
                mclog::tagInfo(TAG, "first InputData attempt");
            }
            captureAndSendAudioFrame();
            taskYIELD();
            if (_audio_start_pending && _audio_stream_started_at.load() > 0 &&
                GetHAL().millis() - _audio_stream_started_at.load() > AUDIO_START_TIMEOUT_MS &&
                _last_audio_frame_sent_at.load() == 0) {
                mclog::tagInfo(TAG, "audio capture start timed out after {} failures", _audio_input_failures.load());
                queueStatus("error", "Mic failed; tap or say Stacky");
                failPendingAudioStart("audio capture start timed out");
            }
        } else {
            if (input_enabled) {
                auto audio_codec = Board::GetInstance().GetAudioCodec();
                if (audio_codec && audio_codec->input_enabled()) {
                    mclog::tagInfo(TAG, "EnableInput(false) start");
                    audio_codec->EnableInput(false);
                    mclog::tagInfo(TAG, "EnableInput(false) end");
                }
                input_enabled = false;
            }
            _mic_audio_level = 0;
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }
    if (input_enabled) {
        auto audio_codec = Board::GetInstance().GetAudioCodec();
        if (audio_codec && audio_codec->input_enabled()) {
            mclog::tagInfo(TAG, "EnableInput(false) start on capture task exit");
            audio_codec->EnableInput(false);
            mclog::tagInfo(TAG, "EnableInput(false) end on capture task exit");
        }
    }
    _audio_capture_task.store(nullptr);
    vTaskDelete(nullptr);
}

bool AppRemoteAgent::captureAndSendCameraImage(const char* requestId, bool enhance, bool preview)
{
    logHeap("before captureImage");
    _camera_capture_active = true;
    const size_t camera_sram = (enhance && !preview) ? MIN_INTERNAL_SRAM_CAMERA_ENHANCED : MIN_INTERNAL_SRAM_CAMERA;
    if (!hasInternalSram(camera_sram, "captureImage")) {
        _camera_capture_active = false;
        sendError(requestId, "low memory for camera capture");
        return false;
    }
    auto camera = hal_bridge::board_get_camera();
    if (!camera) {
        _camera_capture_active = false;
        sendError(requestId, "camera unavailable");
        return false;
    }

    for (int i = 0; i < 4; ++i) {
        if (!camera->StreamCaptures()) {
            _camera_capture_active = false;
            sendError(requestId, "camera capture failed");
            return false;
        }
        if (i < 3) {
            GetHAL().delay(250);
        }
    }

    const uint8_t* frameData = camera->GetFrameData();
    size_t frameSize         = camera->GetFrameSize();
    int width                = camera->GetFrameWidth();
    int height               = camera->GetFrameHeight();
    int format               = camera->GetFrameFormat();

    if (preview && (format == V4L2_PIX_FMT_YUYV || format == V4L2_PIX_FMT_GREY)) {
        const bool color_preview = enhance && format == V4L2_PIX_FMT_YUYV;
        const int bytes_per_pixel = color_preview ? 3 : 1;
        const int row_stride = ((CAMERA_PREVIEW_WIDTH * bytes_per_pixel) + 3) & ~3;
        const size_t header_size = color_preview ? 14 + 40 : 14 + 40 + 256 * 4;
        const size_t image_size = row_stride * CAMERA_PREVIEW_HEIGHT;
        const size_t file_size = header_size + image_size;
        {
            uint8_t* bmp_ptr = static_cast<uint8_t*>(heap_caps_malloc(file_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
            if (!bmp_ptr) {
                bmp_ptr = static_cast<uint8_t*>(malloc(file_size));
            }
            if (!bmp_ptr) {
                _camera_capture_active = false;
                sendError(requestId, "camera buffer alloc failed");
                return false;
            }
            memset(bmp_ptr, 0, file_size);
            _camera_preview_bmp_buf.reset(bmp_ptr);
            _camera_preview_bmp_size = file_size;
        }

        uint8_t min_luma = 255;
        uint8_t max_luma = 0;
        if (enhance) {
            if (format == V4L2_PIX_FMT_GREY) {
                for (size_t i = 0; i < frameSize; ++i) {
                    min_luma = std::min(min_luma, frameData[i]);
                    max_luma = std::max(max_luma, frameData[i]);
                }
            } else {
                for (size_t i = 0; i + 2 < frameSize; i += 4) {
                    min_luma = std::min(min_luma, frameData[i]);
                    max_luma = std::max(max_luma, frameData[i]);
                    min_luma = std::min(min_luma, frameData[i + 2]);
                    max_luma = std::max(max_luma, frameData[i + 2]);
                }
            }
        }
        const int luma_range = max_luma > min_luma ? max_luma - min_luma : 0;

        auto put16 = [this](size_t offset, uint16_t value) {
            _camera_preview_bmp_buf[offset] = value & 0xff;
            _camera_preview_bmp_buf[offset + 1] = (value >> 8) & 0xff;
        };
        auto put32 = [this](size_t offset, uint32_t value) {
            _camera_preview_bmp_buf[offset] = value & 0xff;
            _camera_preview_bmp_buf[offset + 1] = (value >> 8) & 0xff;
            _camera_preview_bmp_buf[offset + 2] = (value >> 16) & 0xff;
            _camera_preview_bmp_buf[offset + 3] = (value >> 24) & 0xff;
        };

        _camera_preview_bmp_buf[0] = 'B';
        _camera_preview_bmp_buf[1] = 'M';
        put32(2, file_size);
        put32(10, header_size);
        put32(14, 40);
        put32(18, CAMERA_PREVIEW_WIDTH);
        put32(22, CAMERA_PREVIEW_HEIGHT);
        put16(26, 1);
        put16(28, color_preview ? 24 : 8);
        put32(34, image_size);
        if (!color_preview) {
            for (int i = 0; i < 256; ++i) {
                const size_t offset = 14 + 40 + i * 4;
                _camera_preview_bmp_buf[offset] = i;
                _camera_preview_bmp_buf[offset + 1] = i;
                _camera_preview_bmp_buf[offset + 2] = i;
            }
        }

        for (int y = 0; y < CAMERA_PREVIEW_HEIGHT; ++y) {
            const int src_y = (y * height) / CAMERA_PREVIEW_HEIGHT;
            uint8_t* row = _camera_preview_bmp_buf.get() + header_size + (CAMERA_PREVIEW_HEIGHT - 1 - y) * row_stride;
            for (int x = 0; x < CAMERA_PREVIEW_WIDTH; ++x) {
                const int src_x = (x * width) / CAMERA_PREVIEW_WIDTH;
                uint8_t luma = 0;
                if (format == V4L2_PIX_FMT_GREY) {
                    const size_t index = static_cast<size_t>(src_y) * width + src_x;
                    if (index < frameSize) luma = frameData[index];
                } else {
                    const size_t pixel = static_cast<size_t>(src_y) * width + src_x;
                    const size_t index = (pixel / 2) * 4 + (src_x % 2 == 0 ? 0 : 2);
                    if (index < frameSize) luma = frameData[index];
                }
                const uint8_t adjusted_luma = luma_range > 0 ? ((luma - min_luma) * 255) / luma_range : luma;
                if (color_preview) {
                    const size_t pixel = static_cast<size_t>(src_y) * width + src_x;
                    const size_t index = (pixel / 2) * 4;
                    const int u = index + 1 < frameSize ? frameData[index + 1] - 128 : 0;
                    const int v = index + 3 < frameSize ? frameData[index + 3] - 128 : 0;
                    const int yv = adjusted_luma;
                    row[x * 3] = clamp_int((298 * yv + 516 * u + 128) >> 8, 0, 255);
                    row[x * 3 + 1] = clamp_int((298 * yv - 100 * u - 208 * v + 128) >> 8, 0, 255);
                    row[x * 3 + 2] = clamp_int((298 * yv + 409 * v + 128) >> 8, 0, 255);
                } else {
                    row[x] = adjusted_luma;
                }
            }
        }

        char event[256];
        int event_len = snprintf(event, sizeof(event),
                                 R"({"type":"event","event":"cameraImage","requestId":"%s","width":%d,"height":%d,"mediaType":"image/bmp","bytes":%u})",
                                 requestId ? requestId : "", CAMERA_PREVIEW_WIDTH, CAMERA_PREVIEW_HEIGHT,
                                 static_cast<unsigned>(_camera_preview_bmp_size));
        if (event_len <= 0 || event_len >= (int)sizeof(event)) {
            _camera_capture_active = false;
            sendError(requestId, "camera metadata failed");
            return false;
        }

        std::lock_guard<std::mutex> lock(_send_mutex);
        sendWebSocketFrame(reinterpret_cast<const uint8_t*>(event), event_len, false);
        sendWebSocketFrame(_camera_preview_bmp_buf.get(), _camera_preview_bmp_size, true);
        _camera_capture_active = false;
        logHeap("after captureImage preview");
        return true;
    }

    std::vector<uint8_t> adjusted_frame;
    if (enhance && format == V4L2_PIX_FMT_YUYV) {
        adjusted_frame.assign(frameData, frameData + frameSize);
        auto_level_yuyv(adjusted_frame);
        frameData = adjusted_frame.data();
    } else if (enhance && format == V4L2_PIX_FMT_GREY) {
        adjusted_frame.assign(frameData, frameData + frameSize);
        auto_level_grey(adjusted_frame);
        frameData = adjusted_frame.data();
    }

    uint8_t* jpeg_data = nullptr;
    size_t jpeg_len    = 0;
    if (!image_to_jpeg((uint8_t*)frameData, frameSize, width, height, (v4l2_pix_fmt_t)format, 80, &jpeg_data, &jpeg_len) ||
        !jpeg_data) {
        _camera_capture_active = false;
        sendError(requestId, "jpeg encode failed");
        return false;
    }

    char event[256];
    int event_len = snprintf(event, sizeof(event),
                             R"({"type":"event","event":"cameraImage","requestId":"%s","width":%d,"height":%d,"mediaType":"image/jpeg","bytes":%u})",
                             requestId ? requestId : "", width, height, static_cast<unsigned>(jpeg_len));
    if (event_len <= 0 || event_len >= (int)sizeof(event)) {
        free(jpeg_data);
        _camera_capture_active = false;
        sendError(requestId, "camera metadata failed");
        return false;
    }

    std::lock_guard<std::mutex> lock(_send_mutex);
    sendWebSocketFrame(reinterpret_cast<const uint8_t*>(event), event_len, false);
    sendWebSocketFrame(jpeg_data, jpeg_len, true);
    free(jpeg_data);
    _camera_capture_active = false;
    logHeap("after captureImage");
    return true;
}

bool AppRemoteAgent::playWebSocketAudio(const AudioPlaybackRequest& request)
{
    mclog::tagInfo(TAG, "websocket playback start");
    logHeap("websocket playback start");
    _audio_playback_active = true;
    _websocket_playback_accepting = true;

    auto audio_codec = Board::GetInstance().GetAudioCodec();
    if (!audio_codec || !_audio_playback_ringbuf) {
        _audio_playback_active = false;
        _websocket_playback_accepting = false;
        mclog::tagInfo(TAG, "websocket playback end: audio playback unavailable");
        logHeap("websocket playback end");
        return true;
    }

    const uint32_t prebuffer_started_at = GetHAL().millis();
    while (!_audio_playback_cancel && !_tasks_stopping &&
           !_websocket_playback_eos && playbackRingBufferUsed() < PLAYBACK_PREBUFFER_BYTES &&
           GetHAL().millis() - prebuffer_started_at < PLAYBACK_PREBUFFER_TIMEOUT_MS) {
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    if (_audio_playback_cancel || _tasks_stopping) {
        _audio_playback_active = false;
        return false;
    }

    GetHAL().setSpeakerVolume(static_cast<uint8_t>(_volume.load()), false);
    mclog::tagInfo(TAG, "EnableOutput(true) start");
    audio_codec->EnableOutput(true);
    mclog::tagInfo(TAG, "EnableOutput(true) end");

    std::vector<int16_t> samples;
    samples.reserve(PLAYBACK_READ_BYTES / 2 + 1);
    bool has_pending_byte = false;
    uint8_t pending_byte = 0;
    uint32_t playback_started_at = 0;
    uint32_t last_pcm_at = GetHAL().millis();
    size_t playback_samples = 0;
    bool natural_completion = false;

    while (!_audio_playback_cancel && !_tasks_stopping) {
        size_t frame_len = 0;
        auto* frame_data = static_cast<uint8_t*>(
            xRingbufferReceiveUpTo(_audio_playback_ringbuf, &frame_len, pdMS_TO_TICKS(20), PLAYBACK_READ_BYTES));
        if (_audio_playback_cancel) {
            if (frame_data) vRingbufferReturnItem(_audio_playback_ringbuf, frame_data);
            break;
        }
        if (!frame_data) {
            if (_websocket_playback_eos && playbackRingBufferUsed() == 0) {
                natural_completion = true;
                break;
            }
            if (GetHAL().millis() - last_pcm_at > 3000) {
                mclog::tagInfo(TAG, "websocket playback timeout waiting for PCM");
                break;
            }
            continue;
        }
        last_pcm_at = GetHAL().millis();

        samples.clear();
        size_t offset = 0;
        if (has_pending_byte && frame_len > 0) {
            samples.push_back(static_cast<int16_t>(pending_byte | (frame_data[0] << 8)));
            has_pending_byte = false;
            offset = 1;
        }
        for (size_t i = offset; i + 1 < frame_len; i += 2) {
            samples.push_back(static_cast<int16_t>(frame_data[i] | (frame_data[i + 1] << 8)));
        }
        if (((frame_len - offset) & 1) != 0) {
            pending_byte = frame_data[frame_len - 1];
            has_pending_byte = true;
        }
        vRingbufferReturnItem(_audio_playback_ringbuf, frame_data);
        if (!samples.empty()) {
            if (playback_started_at == 0) {
                playback_started_at = GetHAL().millis();
                sendPlaybackEvent("speechStart", request.playbackId);
            }
            playback_samples += samples.size();
            _playback_audio_level = smooth_level_1000(_playback_audio_level.load(), pcm_level_1000(samples));
            audio_codec->OutputData(samples);
        }
        GetHAL().feedTheDog();
    }

    const bool current_generation = _audio_playback_generation.load() == request.generation;
    if (current_generation) {
        _websocket_playback_accepting = false;
        _websocket_playback_eos = false;
    }
    if (natural_completion && playback_started_at > 0 && !_audio_playback_cancel) {
        const uint32_t expected_ms = static_cast<uint32_t>(playback_samples / 24);
        const uint32_t elapsed_ms = GetHAL().millis() - playback_started_at;
        if (expected_ms > elapsed_ms) {
            uint32_t remaining_ms = std::min<uint32_t>(expected_ms - elapsed_ms, 3000);
            while (remaining_ms > 0 && !_audio_playback_cancel && !_tasks_stopping) {
                const uint32_t delay_ms = std::min<uint32_t>(remaining_ms, PLAYBACK_DRAIN_CHECK_MS);
                vTaskDelay(pdMS_TO_TICKS(delay_ms));
                remaining_ms -= delay_ms;
            }
        }
    }
    _playback_audio_level = 0;
    drainPlaybackRingBuffer();
    mclog::tagInfo(TAG, "EnableOutput(false) start");
    audio_codec->EnableOutput(false);
    mclog::tagInfo(TAG, "EnableOutput(false) end");
    vTaskDelay(pdMS_TO_TICKS(100));
    _audio_playback_active = false;
    mclog::tagInfo(TAG, "websocket playback end");
    logHeap("websocket playback end");
    return natural_completion && !_audio_playback_cancel && !_tasks_stopping;
}

bool AppRemoteAgent::playAudioUrl(const char* url, const char* playbackId)
{
    mclog::tagInfo(TAG, "playback start");
    logHeap("playback start");
    _audio_playback_active = true;
    esp_http_client_config_t config = {};
    config.url = url;
    config.timeout_ms = 200;
    auto client = esp_http_client_init(&config);
    if (!client) {
        _audio_playback_active = false;
        mclog::tagInfo(TAG, "playback end: http client init failed");
        logHeap("playback end");
        return true;
    }
    if (esp_http_client_open(client, 0) != ESP_OK) {
        esp_http_client_cleanup(client);
        _audio_playback_active = false;
        mclog::tagInfo(TAG, "playback end: http open failed");
        logHeap("playback end");
        return true;
    }
    esp_http_client_fetch_headers(client);

    auto audio_codec = Board::GetInstance().GetAudioCodec();
    if (!audio_codec) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        _audio_playback_active = false;
        mclog::tagInfo(TAG, "playback end: audio codec unavailable");
        logHeap("playback end");
        return true;
    }

    GetHAL().setSpeakerVolume(static_cast<uint8_t>(_volume.load()), false);
    mclog::tagInfo(TAG, "EnableOutput(true) start");
    audio_codec->EnableOutput(true);
    mclog::tagInfo(TAG, "EnableOutput(true) end");
    std::array<uint8_t, 2048> bytes{};
    std::vector<int16_t> samples;
    samples.reserve(1024);
    bool has_pending_byte = false;
    uint8_t pending_byte  = 0;
    uint32_t playback_started_at = 0;
    size_t playback_samples = 0;
    bool natural_completion = false;
    while (!_audio_playback_cancel && !_tasks_stopping) {
        int read = esp_http_client_read(client, reinterpret_cast<char*>(bytes.data()), bytes.size());
        if (read == -ESP_ERR_HTTP_EAGAIN) {
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }
        if (read <= 0) {
            natural_completion = !_audio_playback_cancel;
            break;
        }
        samples.clear();
        size_t offset = 0;
        if (has_pending_byte) {
            samples.push_back(static_cast<int16_t>(pending_byte | (bytes[0] << 8)));
            has_pending_byte = false;
            offset = 1;
        }
        for (size_t i = offset; i + 1 < static_cast<size_t>(read); i += 2) {
            samples.push_back(static_cast<int16_t>(bytes[i] | (bytes[i + 1] << 8)));
        }
        if (((static_cast<size_t>(read) - offset) & 1) != 0) {
            pending_byte = bytes[read - 1];
            has_pending_byte = true;
        }
        if (!samples.empty()) {
            if (playback_started_at == 0) {
                playback_started_at = GetHAL().millis();
                sendPlaybackEvent("speechStart", playbackId);
            }
            playback_samples += samples.size();
            _playback_audio_level = smooth_level_1000(_playback_audio_level.load(), pcm_level_1000(samples));
            audio_codec->OutputData(samples);
        }
        GetHAL().feedTheDog();
    }
    if (natural_completion && playback_started_at > 0 && !_audio_playback_cancel) {
        const uint32_t expected_ms = static_cast<uint32_t>(playback_samples / 24);
        const uint32_t elapsed_ms = GetHAL().millis() - playback_started_at;
        if (expected_ms > elapsed_ms) {
            uint32_t remaining_ms = std::min<uint32_t>(expected_ms - elapsed_ms, 3000);
            while (remaining_ms > 0 && !_audio_playback_cancel && !_tasks_stopping) {
                const uint32_t delay_ms = std::min<uint32_t>(remaining_ms, PLAYBACK_DRAIN_CHECK_MS);
                vTaskDelay(pdMS_TO_TICKS(delay_ms));
                remaining_ms -= delay_ms;
            }
        }
    }
    _playback_audio_level = 0;
    mclog::tagInfo(TAG, "EnableOutput(false) start");
    audio_codec->EnableOutput(false);
    mclog::tagInfo(TAG, "EnableOutput(false) end");
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    _audio_playback_active = false;
    mclog::tagInfo(TAG, "playback end");
    logHeap("playback end");
    return natural_completion && !_audio_playback_cancel && !_tasks_stopping;
}

void AppRemoteAgent::onClose()
{
    mclog::tagInfo(TAG, "on close");
    _opened = false;
    _audio_start_pending = false;
    _audio_playback_active = false;
    _camera_capture_active = false;
    if (_wake_word_detector) {
        _wake_word_detector->shutdown();
        _wake_word_detector.reset();
    }
    stopAudioTasks();
    _audio_streaming = false;
    stopBrainClient();
    stopWebSocketServer();
    clearRenderScene();
    LvglLockGuard lock;
    view::destroy_home_indicator();
    if (_root) {
        lv_obj_del(_root);
        _root = nullptr;
    }
    _status_dot   = nullptr;
    _main_label   = nullptr;
    _log_label    = nullptr;
    _decorator_ids.clear();
    GetStackChan().resetAvatar();
}

void AppRemoteAgent::audioPlaybackTaskEntry(void* arg)
{
    static_cast<AppRemoteAgent*>(arg)->audioPlaybackLoop();
}

void AppRemoteAgent::audioCaptureTaskEntry(void* arg)
{
    static_cast<AppRemoteAgent*>(arg)->audioCaptureLoop();
}

void AppRemoteAgent::audioCaptureSendTaskEntry(void* arg)
{
    static_cast<AppRemoteAgent*>(arg)->audioCaptureSendLoop();
}
