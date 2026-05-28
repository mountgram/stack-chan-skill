/*
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <mooncake.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <vector>

#include <ArduinoJson.hpp>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

class WebSocket;
typedef struct _lv_obj_t lv_obj_t;

class AppRemoteAgent : public mooncake::AppAbility {
public:
    AppRemoteAgent();
    ~AppRemoteAgent();

    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;
    void handleTap();

public:
    struct ReceivedMessage {
        bool binary = false;
        std::string text;
    };

    struct AudioPlaybackRequest {
        char requestId[64] = {0};
        char url[256]      = {0};
    };

    struct AudioPcmFrame {
        size_t len = 0;
        uint8_t data[1024] = {0};
    };

    struct RenderNodeRef {
        char id[65] = {0};
        lv_obj_t* obj = nullptr;
        int x = 0;
        int y = 0;
        int w = 1;
        int h = 1;
        float scaleX = 1.0f;
        float scaleY = 1.0f;
        float rotation = 0.0f;
        float opacity = 1.0f;
    };

    struct RenderKeyframe {
        uint32_t t = 0;
        float value = 0.0f;
    };

    struct RenderTrack {
        char target[65] = {0};
        char property[16] = {0};
        std::vector<RenderKeyframe> keyframes;
    };

private:

    std::unique_ptr<WebSocket> _websocket;
    std::mutex _mutex;
    std::mutex _send_mutex;
    std::queue<ReceivedMessage> _messages;
    QueueHandle_t _audio_playback_queue = nullptr;
    QueueHandle_t _audio_frame_queue    = nullptr;
    TaskHandle_t _audio_playback_task   = nullptr;
    TaskHandle_t _audio_capture_task    = nullptr;
    lv_obj_t* _root         = nullptr;
    lv_obj_t* _status_dot   = nullptr;
    lv_obj_t* _main_label   = nullptr;
    lv_obj_t* _log_label    = nullptr;
    lv_obj_t* _render_root  = nullptr;
    uint32_t _last_reconnect_attempt = 0;
    uint32_t _last_telemetry_at      = 0;
    uint32_t _last_motion_at         = 0;
    std::atomic_bool _opened{false};
    std::atomic_bool _connected{false};
    std::atomic_bool _audio_streaming{false};
    std::atomic_bool _audio_playback_cancel{false};
    std::atomic_bool _tasks_stopping{false};
    std::atomic_int _volume{90};
    int _current_emotion             = 0;
    int _yaw                        = 0;
    int _pitch                      = 35;
    std::vector<int> _decorator_ids;
    std::string _render_scene_id;
    std::string _render_scene_json;
    std::vector<RenderNodeRef> _render_nodes;
    std::vector<RenderTrack> _render_tracks;
    uint32_t _render_animation_started_at = 0;
    uint32_t _render_animation_last_frame_at = 0;
    uint32_t _render_animation_duration = 0;
    bool _render_animation_loop = false;
    bool _render_animation_yoyo = false;
    bool _render_active              = false;
    bool _pending_status_dirty      = false;
    char _pending_mode[24]          = {0};
    char _pending_text[160]         = {0};

    void createUi();
    void connectWebSocket();
    void startAudioTasks();
    void stopAudioTasks();
    void processMessages();
    void sendQueuedAudioFrames();
    void applyPendingStatus();
    void handleMessage(const std::string& data);
    void sendJson(const std::string& data);
    void sendHello();
    void sendTelemetry();
    void sendPacket(uint8_t type, const uint8_t* data, size_t len);
    void sendAck(const char* requestId);
    void sendError(const char* requestId, const char* message);
    void ensureAvatar();
    void hideAvatar();
    void clearRenderScene();
    void renderSceneJson(const std::string& data);
    bool startRenderAnimation(ArduinoJson::JsonDocument& doc, const char* requestId);
    void stopRenderAnimation();
    void updateRenderAnimation();
    void setStatus(const char* mode, const char* text);
    void queueStatus(const char* mode, const char* text);
    void setLog(const char* text);
    void captureAndSendAudioFrame();
    void captureAndSendCameraImage(const char* requestId, bool enhance);
    bool queueAudioPlayback(const char* requestId, const char* url);
    void audioPlaybackLoop();
    void audioCaptureLoop();
    void playAudioUrl(const char* url);
    static void audioPlaybackTaskEntry(void* arg);
    static void audioCaptureTaskEntry(void* arg);
};
