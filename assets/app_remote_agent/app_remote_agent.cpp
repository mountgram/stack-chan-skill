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
#include <esp_http_client.h>
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
#include <web_socket.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>

#ifndef STACKY_WS_URL
#define STACKY_WS_URL "ws://STACKY_BRAIN_HOST:6001/stacky/device?token=dev-token-change-me"
#endif

using namespace smooth_ui_toolkit::lvgl_cpp;
using namespace stackchan;

static const char* TAG = "REMOTE.AGENT";
static constexpr size_t MAX_RENDER_ANIMATIONS = 4;
static constexpr size_t MAX_RENDER_TRACKS = 32;
static constexpr uint32_t AUDIO_START_TIMEOUT_MS = 2000;

static int clamp_int(int value, int min, int max)
{
    return std::min(max, std::max(min, value));
}

static void append_u16_le(std::vector<uint8_t>& data, uint16_t value)
{
    data.push_back(value & 0xff);
    data.push_back((value >> 8) & 0xff);
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
    std::string value(emotion);
    if (value == "happy") return avatar::Emotion::Happy;
    if (value == "angry") return avatar::Emotion::Angry;
    if (value == "sad") return avatar::Emotion::Sad;
    if (value == "doubt" || value == "thinking" || value == "curious" || value == "surprised") return avatar::Emotion::Doubt;
    if (value == "sleepy" || value == "asleep") return avatar::Emotion::Sleepy;
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
        app->handleTap();
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
    connectWebSocket();
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

    _status_dot = lv_obj_create(_root);
    lv_obj_set_size(_status_dot, 14, 14);
    lv_obj_set_style_radius(_status_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(_status_dot, lv_color_hex(0xFF4D5E), 0);
    lv_obj_set_style_border_width(_status_dot, 0, 0);
    lv_obj_set_style_pad_all(_status_dot, 0, 0);
    lv_obj_align(_status_dot, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_add_flag(_status_dot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(_status_dot, panel_click_cb, LV_EVENT_CLICKED, this);

    _main_label = lv_label_create(_root);
    lv_label_set_long_mode(_main_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(_main_label, 300);
    lv_obj_set_style_text_color(_main_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(_main_label, &lv_font_montserrat_24, 0);
    lv_label_set_text(_main_label, "Connecting to brain...");
    lv_obj_align(_main_label, LV_ALIGN_CENTER, 0, -10);
    lv_obj_add_flag(_main_label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(_main_label, panel_click_cb, LV_EVENT_CLICKED, this);

    _log_label = lv_label_create(_root);
    lv_label_set_long_mode(_log_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(_log_label, 300);
    lv_obj_set_style_text_color(_log_label, lv_color_hex(0xA7B0C0), 0);
    lv_label_set_text(_log_label, STACKY_WS_URL);
    lv_obj_align(_log_label, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_add_flag(_log_label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(_log_label, panel_click_cb, LV_EVENT_CLICKED, this);
}

void AppRemoteAgent::connectWebSocket()
{
    _websocket.reset();
    _connected = false;
    setStatus("connecting", "Connecting to brain...");

    auto network = Board::GetInstance().GetNetwork();
    _websocket   = network->CreateWebSocket(1);
    if (!_websocket) {
        setStatus("error", "WebSocket create failed");
        return;
    }

    _websocket->OnConnected([this]() {
        _connected = true;
        setStatus("connected", "Ready");
        sendHello();
    });

    _websocket->OnDisconnected([this]() {
        _connected = false;
        disarmWakeWord(500);
        setStatus("offline", "Disconnected");
    });

    _websocket->OnData([this](const char* data, size_t len, bool binary) {
        if (binary) return;
        std::lock_guard<std::mutex> lock(_mutex);
        _messages.push({binary, std::string(data, len)});
    });

    if (!_websocket->Connect(STACKY_WS_URL)) {
        setStatus("error", "Connect failed");
    }
    _last_reconnect_attempt = GetHAL().millis();
}

void AppRemoteAgent::onRunning()
{
    if (!_opened) return;

    applyPendingStatus();

    if (!_websocket || !_websocket->IsConnected()) {
        if (GetHAL().millis() - _last_reconnect_attempt > 5000) {
            connectWebSocket();
        }
    } else {
        processMessages();
        sendQueuedAudioFrames();
        updateRenderAnimation();
        if (GetHAL().millis() - _last_telemetry_at > 3000) {
            sendTelemetry();
        }
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
    if (!_audio_frame_queue) {
        _audio_frame_queue = xQueueCreate(8, sizeof(AudioPcmFrame));
    }
    if (!_audio_playback_task) {
        xTaskCreate(audioPlaybackTaskEntry, "stacky_audio_out", 8192, this, 3, &_audio_playback_task);
    }
    if (!_audio_capture_task) {
        xTaskCreate(audioCaptureTaskEntry, "stacky_audio_in", 8192, this, 2, &_audio_capture_task);
    }
}

void AppRemoteAgent::stopAudioTasks()
{
    _tasks_stopping = true;
    _audio_streaming = false;
    _audio_start_pending = false;
    _audio_playback_cancel = true;
    if (_audio_playback_queue) {
        xQueueReset(_audio_playback_queue);
    }
    if (_audio_frame_queue) {
        xQueueReset(_audio_frame_queue);
    }
    if (_audio_playback_task) {
        vTaskDelete(_audio_playback_task);
        _audio_playback_task = nullptr;
    }
    if (_audio_capture_task) {
        vTaskDelete(_audio_capture_task);
        _audio_capture_task = nullptr;
    }
    if (_audio_playback_queue) {
        vQueueDelete(_audio_playback_queue);
        _audio_playback_queue = nullptr;
    }
    if (_audio_frame_queue) {
        vQueueDelete(_audio_frame_queue);
        _audio_frame_queue = nullptr;
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
        strncpy(mode, _pending_mode, sizeof(mode) - 1);
        strncpy(text, _pending_text, sizeof(text) - 1);
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

void AppRemoteAgent::sendQueuedAudioFrames()
{
    if (!_audio_frame_queue) return;
    AudioPcmFrame frame;
    for (int i = 0; i < 6; ++i) {
        if (xQueueReceive(_audio_frame_queue, &frame, 0) != pdTRUE) return;
        if (frame.len > 0) {
            sendPacket(0x31, frame.data, frame.len);
            if (_last_audio_frame_sent_at.load() == 0) {
                mclog::tagInfo(TAG, "first PCM frame sent from queue");
            }
            _last_audio_frame_sent_at = GetHAL().millis();
        }
    }
}

void AppRemoteAgent::ackPendingAudioStart()
{
    char requestId[64] = {0};
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (!_audio_start_pending) return;
        strncpy(requestId, _audio_stream_request_id, sizeof(requestId) - 1);
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
            strncpy(requestId, _audio_stream_request_id, sizeof(requestId) - 1);
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
    _last_audio_frame_queued_at = 0;
    _last_audio_frame_sent_at = 0;
    _audio_input_failures = 0;
    _audio_first_input_attempt_logged = false;
    if (_audio_frame_queue) {
        xQueueReset(_audio_frame_queue);
    }
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
        setStatus("speaking", "Speaking...");
        {
            LvglLockGuard lock;
            if (GetStackChan().hasAvatar()) {
                GetStackChan().avatar().clearSpeech();
            }
        }
        if (audioUrl && strlen(audioUrl) > 0) {
            if (queueAudioPlayback(requestId, audioUrl)) {
                sendAck(requestId);
            }
            return;
        }
        setStatus("speaking", "");
        mclog::tagInfo(TAG, "speechDone sent");
        sendJson(R"({"type":"event","event":"speechDone"})");
        sendAck(requestId);
        return;
    }

    if (strcmp(type, "startAudio") == 0) {
        mclog::tagInfo(TAG, "received startAudio requestId={}", requestId);
        auto audio_codec = Board::GetInstance().GetAudioCodec();
        if (!audio_codec) {
            sendError(requestId, "audio codec unavailable");
            return;
        }
        mclog::tagInfo(TAG, "wake-word disarm start");
        disarmWakeWord(500);
        mclog::tagInfo(TAG, "wake-word disarm end");
        resetAudioStartState(requestId);
        _audio_streaming = true;
        mclog::tagInfo(TAG, "_audio_streaming = true");
        setStatus("listening", "Streaming mic...");
        return;
    }

    if (strcmp(type, "standby") == 0) {
        const char* text = doc["text"] | "Standby. Tap to talk.";
        _audio_streaming = false;
        failPendingAudioStart("audio capture stopped before start");
        if (doc["wakeWord"].is<ArduinoJson::JsonObject>()) {
            ArduinoJson::JsonObject wake_word = doc["wakeWord"].as<ArduinoJson::JsonObject>();
            const char* model_id                   = wake_word["modelId"] | "";
            const char* phrase                     = wake_word["phrase"] | "Stacky";
            if (strcmp(model_id, "stacky") != 0 || strcmp(phrase, "Stacky") != 0) {
                sendError(requestId, "wake word model unavailable");
                return;
            }
            if (!ensureWakeWordDetector() || !_wake_word_detector->arm()) {
                sendError(requestId, "wake word detector unavailable");
                return;
            }
        } else {
            disarmWakeWord(500);
        }
        setStatus("standby", text);
        sendAck(requestId);
        return;
    }

    if (strcmp(type, "captureImage") == 0) {
        captureAndSendCameraImage(requestId, doc["enhance"] | false);
        sendAck(requestId);
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
        _audio_streaming = false;
        failPendingAudioStart("audio capture stopped before start");
        if (_audio_frame_queue) {
            xQueueReset(_audio_frame_queue);
        }
        mclog::tagInfo(TAG, "_audio_streaming = false");
        setStatus("thinking", "Mic stopped");
        sendAck(requestId);
        return;
    }

    if (strcmp(type, "stop") == 0) {
        _audio_streaming = false;
        _audio_playback_cancel = true;
        if (_audio_playback_queue) {
            xQueueReset(_audio_playback_queue);
        }
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
        const char* sceneId = doc["sceneId"] | "";
        if (!sceneId[0]) {
            sendError(requestId, "sceneId required");
            return;
        }
        _render_scene_id = sceneId;
        _render_scene_json = data;
        renderSceneJson(_render_scene_json);
        sendAck(requestId);
        return;
    }

    if (strcmp(type, "render.setScene") == 0) {
        const char* sceneId = doc["sceneId"] | "";
        if (_render_scene_id.empty() || _render_scene_id != sceneId) {
            sendError(requestId, "scene not defined");
            return;
        }
        renderSceneJson(_render_scene_json);
        sendAck(requestId);
        return;
    }

    if (strcmp(type, "render.animate") == 0) {
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
    if (_websocket && _websocket->IsConnected()) {
        _websocket->Send(data.c_str());
    }
}

void AppRemoteAgent::sendPacket(uint8_t type, const uint8_t* data, size_t len)
{
    if (!_websocket || !_websocket->IsConnected()) {
        return;
    }
    std::lock_guard<std::mutex> lock(_send_mutex);
    if (!_websocket || !_websocket->IsConnected()) {
        return;
    }
    std::vector<uint8_t> packet;
    packet.reserve(5 + len);
    packet.push_back(type);
    packet.push_back((len >> 24) & 0xff);
    packet.push_back((len >> 16) & 0xff);
    packet.push_back((len >> 8) & 0xff);
    packet.push_back(len & 0xff);
    packet.insert(packet.end(), data, data + len);
    _websocket->Send(packet.data(), packet.size(), true);
}

void AppRemoteAgent::sendHello()
{
    auto id = GetHAL().getFactoryMacString("");
    const bool wake_word_ready = ensureWakeWordDetector();
    char buffer[1400];
    if (wake_word_ready) {
        snprintf(buffer, sizeof(buffer),
                 R"({"type":"hello","id":"stacky-%s","version":2,"capabilities":["screen","face","look","led","telemetry","tap","audio","camera","volume","standby","wakeWord","render"],"wakeWord":{"version":1,"models":[{"id":"stacky","phrase":"Stacky","sampleRate":16000,"cutoff":0.97,"slidingWindow":5}]},"render":{"version":1,"screen":{"width":320,"height":240,"fps":30},"primitives":["group","circle","ellipse","rect"],"transforms":["translate","scale","rotate","opacity"],"animations":["keyframes","audioLevel"],"audioLevelSources":["playback","mic","any"],"limits":{"maxNodes":64,"maxSceneBytes":16384,"maxAnimationMs":300000,"maxActiveAnimations":4,"maxActiveTracks":32}}})",
                 id.c_str());
    } else {
        snprintf(buffer, sizeof(buffer),
                 R"({"type":"hello","id":"stacky-%s","version":2,"capabilities":["screen","face","look","led","telemetry","tap","audio","camera","volume","standby","render"],"render":{"version":1,"screen":{"width":320,"height":240,"fps":30},"primitives":["group","circle","ellipse","rect"],"transforms":["translate","scale","rotate","opacity"],"animations":["keyframes","audioLevel"],"audioLevelSources":["playback","mic","any"],"limits":{"maxNodes":64,"maxSceneBytes":16384,"maxAnimationMs":300000,"maxActiveAnimations":4,"maxActiveTracks":32}}})",
                 id.c_str());
    }
    sendJson(buffer);
}

void AppRemoteAgent::sendTelemetry()
{
    _last_telemetry_at = GetHAL().millis();
    char buffer[224];
    snprintf(buffer, sizeof(buffer),
             R"({"type":"telemetry","battery":%d,"charging":%s,"wifiRssi":0,"pose":{"yaw":%d,"pitch":%d},"volume":%d})",
             (int)GetHAL().getBatteryLevel(), GetHAL().isBatteryCharging() ? "true" : "false", _yaw, _pitch, _volume.load());
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

void AppRemoteAgent::renderSceneJson(const std::string& data)
{
    ArduinoJson::JsonDocument doc;
    auto error = ArduinoJson::deserializeJson(doc, data);
    if (error) return;

    auto nodes = doc["nodes"].as<ArduinoJson::JsonArray>();
    if (nodes.isNull() || nodes.size() > 64) return;

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
        const bool show_main = mode && (strcmp(mode, "connected") == 0 || strcmp(mode, "standby") == 0 || strcmp(mode, "error") == 0 || strcmp(mode, "offline") == 0 || strcmp(mode, "connecting") == 0);
        lv_label_set_text(_main_label, show_main && text ? text : "");
    }
    if (_log_label) {
        if (is_visible_status_mode(mode)) {
            lv_label_set_text(_log_label, text ? text : "");
        } else {
            lv_label_set_text(_log_label, _connected ? "" : STACKY_WS_URL);
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
    char buffer[96];
    snprintf(buffer, sizeof(buffer), R"({"type":"event","event":"tap","at":%lu})", (unsigned long)GetHAL().millis());
    sendJson(buffer);
}

bool AppRemoteAgent::ensureWakeWordDetector()
{
    if (_wake_word_detector) {
        return true;
    }

    auto detector = std::make_unique<StackyWakeWordDetector>();
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

void AppRemoteAgent::handleWakeWordDetected(const std::string& wake_word)
{
    _audio_streaming = false;
    queueStatus("listening", "Wake word heard");
    char buffer[192];
    snprintf(buffer, sizeof(buffer), R"({"type":"event","event":"wakeWord","wakeWord":"%s","modelId":"stacky","at":%lu})",
             wake_word.c_str(), (unsigned long)GetHAL().millis());
    sendJson(buffer);
}

bool AppRemoteAgent::captureAndSendAudioFrame()
{
    if (!_websocket || !_websocket->IsConnected()) {
        return false;
    }

    auto audio_codec = Board::GetInstance().GetAudioCodec();
    if (!audio_codec) {
        setStatus("error", "Audio codec unavailable");
        _audio_streaming = false;
        failPendingAudioStart("audio codec unavailable");
        return false;
    }

    constexpr size_t chunk_frames = 512;
    const size_t input_channels = std::max(audio_codec->input_channels(), 1);
    std::vector<int16_t> input_chunk(chunk_frames * input_channels);
    if (!audio_codec->InputData(input_chunk)) {
        _audio_input_failures++;
        vTaskDelay(pdMS_TO_TICKS(5));
        return false;
    }
    if (_last_audio_frame_queued_at.load() == 0) {
        mclog::tagInfo(TAG, "first successful InputData");
    }
    std::vector<uint8_t> pcm;
    pcm.reserve(chunk_frames * 2);
    std::vector<int16_t> mono_samples;
    mono_samples.reserve(chunk_frames);
    for (size_t frame = 0; frame < chunk_frames; ++frame) {
        int16_t sample = input_chunk[frame * input_channels];
        mono_samples.push_back(sample);
        append_u16_le(pcm, static_cast<uint16_t>(sample));
    }
    _mic_audio_level = smooth_level_1000(_mic_audio_level.load(), pcm_level_1000(mono_samples));
    if (!_audio_frame_queue || pcm.size() > sizeof(AudioPcmFrame::data)) {
        return false;
    }
    AudioPcmFrame frame;
    frame.len = pcm.size();
    memcpy(frame.data, pcm.data(), pcm.size());
    if (xQueueSend(_audio_frame_queue, &frame, 0) != pdTRUE) {
        _audio_input_failures++;
        return false;
    }
    if (_last_audio_frame_queued_at.load() == 0) {
        mclog::tagInfo(TAG, "first PCM frame queued");
    }
    _last_audio_frame_queued_at = GetHAL().millis();
    _audio_input_failures = 0;
    ackPendingAudioStart();
    return true;
}

bool AppRemoteAgent::queueAudioPlayback(const char* requestId, const char* url)
{
    if (!_audio_playback_queue) {
        sendError(requestId, "audio playback unavailable");
        return false;
    }
    AudioPlaybackRequest request;
    strncpy(request.requestId, requestId ? requestId : "", sizeof(request.requestId) - 1);
    strncpy(request.url, url ? url : "", sizeof(request.url) - 1);
    if (xQueueSend(_audio_playback_queue, &request, 0) != pdTRUE) {
        sendError(requestId, "audio playback queue full");
        return false;
    }
    return true;
}

void AppRemoteAgent::audioPlaybackLoop()
{
    AudioPlaybackRequest request;
    while (!_tasks_stopping) {
        if (!_audio_playback_queue || xQueueReceive(_audio_playback_queue, &request, pdMS_TO_TICKS(100)) != pdTRUE) {
            continue;
        }
        _audio_playback_cancel = false;
        if (request.url[0]) {
            playAudioUrl(request.url);
        }
        queueStatus("speaking", "");
        mclog::tagInfo(TAG, "speechDone sent");
        sendJson(R"({"type":"event","event":"speechDone"})");
    }
    _audio_playback_task = nullptr;
    vTaskDelete(nullptr);
}

void AppRemoteAgent::audioCaptureLoop()
{
    bool input_enabled = false;
    while (!_tasks_stopping) {
        if (_audio_streaming) {
            if (!input_enabled) {
                mclog::tagInfo(TAG, "audio capture loop sees streaming true");
                auto audio_codec = Board::GetInstance().GetAudioCodec();
                if (!audio_codec) {
                    queueStatus("error", "Audio codec unavailable");
                    _audio_streaming = false;
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
            if (_audio_start_pending && _audio_stream_started_at.load() > 0 &&
                GetHAL().millis() - _audio_stream_started_at.load() > AUDIO_START_TIMEOUT_MS &&
                _last_audio_frame_queued_at.load() == 0) {
                mclog::tagInfo(TAG, "audio capture start timed out after {} failures", _audio_input_failures.load());
                _audio_streaming = false;
                auto audio_codec = Board::GetInstance().GetAudioCodec();
                if (audio_codec && audio_codec->input_enabled()) {
                    mclog::tagInfo(TAG, "EnableInput(false) start after audio start timeout");
                    audio_codec->EnableInput(false);
                    mclog::tagInfo(TAG, "EnableInput(false) end after audio start timeout");
                }
                input_enabled = false;
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
    _audio_capture_task = nullptr;
    vTaskDelete(nullptr);
}

void AppRemoteAgent::captureAndSendCameraImage(const char* requestId, bool enhance)
{
    auto camera = hal_bridge::board_get_camera();
    if (!camera) {
        sendError(requestId, "camera unavailable");
        return;
    }

    for (int i = 0; i < 4; ++i) {
        if (!camera->StreamCaptures()) {
            sendError(requestId, "camera capture failed");
            return;
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
        sendError(requestId, "jpeg encode failed");
        return;
    }

    char meta[192];
    int meta_len = snprintf(meta, sizeof(meta), R"({"requestId":"%s","width":%d,"height":%d,"mediaType":"image/jpeg"})",
                            requestId ? requestId : "", width, height);
    if (meta_len <= 0 || meta_len >= (int)sizeof(meta)) {
        free(jpeg_data);
        sendError(requestId, "camera metadata failed");
        return;
    }

    std::vector<uint8_t> packet;
    packet.reserve(5 + meta_len + jpeg_len);
    packet.push_back(0x32);
    packet.push_back((meta_len >> 24) & 0xff);
    packet.push_back((meta_len >> 16) & 0xff);
    packet.push_back((meta_len >> 8) & 0xff);
    packet.push_back(meta_len & 0xff);
    packet.insert(packet.end(), meta, meta + meta_len);
    packet.insert(packet.end(), jpeg_data, jpeg_data + jpeg_len);
    std::lock_guard<std::mutex> lock(_send_mutex);
    if (_websocket && _websocket->IsConnected()) {
        _websocket->Send(packet.data(), packet.size(), true);
    }
    free(jpeg_data);
}

void AppRemoteAgent::playAudioUrl(const char* url)
{
    mclog::tagInfo(TAG, "playback start");
    esp_http_client_config_t config = {};
    config.url = url;
    config.timeout_ms = 10000;
    auto client = esp_http_client_init(&config);
    if (!client) {
        mclog::tagInfo(TAG, "playback end: http client init failed");
        return;
    }
    if (esp_http_client_open(client, 0) != ESP_OK) {
        esp_http_client_cleanup(client);
        mclog::tagInfo(TAG, "playback end: http open failed");
        return;
    }
    esp_http_client_fetch_headers(client);

    auto audio_codec = Board::GetInstance().GetAudioCodec();
    if (!audio_codec) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        mclog::tagInfo(TAG, "playback end: audio codec unavailable");
        return;
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
    while (!_audio_playback_cancel && !_tasks_stopping) {
        int read = esp_http_client_read(client, reinterpret_cast<char*>(bytes.data()), bytes.size());
        if (read <= 0) break;
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
            _playback_audio_level = smooth_level_1000(_playback_audio_level.load(), pcm_level_1000(samples));
            audio_codec->OutputData(samples);
        }
        GetHAL().feedTheDog();
        vTaskDelay(1);
    }
    _playback_audio_level = 0;
    mclog::tagInfo(TAG, "EnableOutput(false) start");
    audio_codec->EnableOutput(false);
    mclog::tagInfo(TAG, "EnableOutput(false) end");
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    mclog::tagInfo(TAG, "playback end");
}

void AppRemoteAgent::onClose()
{
    mclog::tagInfo(TAG, "on close");
    _opened = false;
    _audio_start_pending = false;
    if (_wake_word_detector) {
        _wake_word_detector->shutdown();
        _wake_word_detector.reset();
    }
    stopAudioTasks();
    _audio_streaming = false;
    _websocket.reset();
    LvglLockGuard lock;
    view::destroy_home_indicator();
    if (_root) {
        lv_obj_del(_root);
        _root = nullptr;
    }
    _status_dot   = nullptr;
    _main_label   = nullptr;
    _log_label    = nullptr;
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
