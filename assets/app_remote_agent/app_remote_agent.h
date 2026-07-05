/*
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <mooncake.h>
#include <atomic>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <vector>

#include <ArduinoJson.hpp>
#include <esp_http_server.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/ringbuf.h>
#include <freertos/task.h>

class StackyWakeWordDetector;
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
    void handleHold();

public:
    struct ReceivedMessage {
        bool binary = false;
        std::string text;
    };

    struct AudioPlaybackRequest {
        char requestId[64] = {0};
        char playbackId[64] = {0};
        char url[256]      = {0};
        uint32_t generation = 0;
        bool websocket     = false;
        bool bargeIn       = false;
    };

    struct AudioPcmFrame {
        size_t len = 0;
        uint32_t generation = 0;
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
        char animationId[97] = {0};
        char property[16] = {0};
        std::vector<RenderKeyframe> keyframes;
        bool audioReactive = false;
        char audioSource[16] = {0};
        float audioScale = 0.0f;
        float audioOffset = 0.0f;
        float audioMin = 0.0f;
        float audioMax = 0.0f;
        bool hasAudioMin = false;
        bool hasAudioMax = false;
    };

    struct RenderAnimationState {
        char animationId[97] = {0};
        std::vector<RenderTrack> tracks;
        uint32_t startedAt = 0;
        uint32_t duration = 0;
        bool loop = false;
        bool yoyo = false;
    };

private:

    httpd_handle_t _websocket_server = nullptr;
    std::atomic<int> _websocket_fd{-1};
    std::unique_ptr<StackyWakeWordDetector> _wake_word_detector;
    std::mutex _mutex;
    std::mutex _send_mutex;
    std::queue<ReceivedMessage> _messages;
    QueueHandle_t _audio_playback_queue = nullptr;
    RingbufHandle_t _audio_playback_ringbuf = nullptr;
    bool _audio_playback_ringbuf_in_psram = false;
    QueueHandle_t _audio_capture_pcm_queue = nullptr;
    std::atomic<TaskHandle_t> _audio_playback_task{nullptr};
    std::atomic<TaskHandle_t> _audio_capture_task{nullptr};
    std::atomic<TaskHandle_t> _audio_capture_send_task{nullptr};
    lv_obj_t* _root         = nullptr;
    lv_obj_t* _status_dot   = nullptr;
    lv_obj_t* _main_label   = nullptr;
    lv_obj_t* _log_label    = nullptr;
    lv_obj_t* _render_root  = nullptr;
    uint32_t _last_telemetry_at      = 0;
    uint32_t _last_motion_at         = 0;
    std::atomic_bool _opened{false};
    std::atomic_bool _connected{false};
    std::atomic_bool _hello_pending{false};
    std::atomic_bool _audio_streaming{false};
    std::atomic_bool _audio_start_pending{false};
    std::atomic_bool _audio_first_input_attempt_logged{false};
    std::atomic_bool _audio_first_input_success_logged{false};
    std::atomic_bool _audio_playback_active{false};
    std::atomic_bool _audio_playback_pending{false};
    std::atomic_bool _audio_playback_cancel{false};
    std::atomic_bool _audio_playback_interrupted_reported{false};
    std::atomic_bool _websocket_playback_accepting{false};
    std::atomic_bool _websocket_playback_eos{false};
    std::atomic_bool _barge_in_enabled{false};
    std::atomic_bool _barge_in_reported{false};
    std::atomic_bool _camera_capture_active{false};
    std::atomic_bool _tasks_stopping{false};
    std::atomic_bool _standby{false};
    std::atomic<uint32_t> _audio_stream_started_at{0};
    std::atomic<uint32_t> _last_audio_frame_sent_at{0};
    std::atomic<uint32_t> _audio_playback_generation{0};
    std::atomic<uint32_t> _last_playback_queue_full_log_at{0};
    std::atomic<uint32_t> _playback_queue_overflows{0};
    std::atomic<uint32_t> _mic_frames_dropped{0};
    std::atomic_int _volume{90};
    std::atomic_int _mic_audio_level{0};
    std::atomic_int _playback_audio_level{0};
    std::atomic_int _audio_input_failures{0};
    int _current_emotion             = 0;
    int _yaw                        = 0;
    int _pitch                      = 35;
    std::vector<int> _decorator_ids;
    std::string _render_scene_id;
    std::string _render_scene_json;
    std::vector<int16_t> _audio_input_chunk;
    std::vector<uint8_t> _ws_recv_buf;
    struct CameraBmpFree { void operator()(uint8_t* p) const noexcept { free(p); } };
    std::unique_ptr<uint8_t[], CameraBmpFree> _camera_preview_bmp_buf;
    size_t _camera_preview_bmp_size = 0;
    std::vector<RenderNodeRef> _render_nodes;
    uint32_t _render_animation_last_frame_at = 0;
    std::vector<RenderAnimationState> _render_animations;
    bool _render_active              = false;
    bool _pending_status_dirty      = false;
    char _pending_mode[24]          = {0};
    char _pending_text[160]         = {0};
    char _audio_stream_request_id[64] = {0};
    char _current_playback_id[64] = {0};

    void createUi();
    void startWebSocketServer();
    void stopWebSocketServer();
    void handleWebSocketConnected(int fd);
    void handleWebSocketDisconnected(int fd);
    esp_err_t handleWebSocketFrame(httpd_req_t* req);
    bool sendWebSocketFrame(const uint8_t* data, size_t len, bool binary);
    void startAudioTasks();
    void stopAudioTasks();
    void processMessages();
    void applyPendingStatus();
    void handleMessage(const std::string& data);
    void sendJson(const std::string& data);
    void sendHello();
    void sendTelemetry();
    bool sendPacket(uint8_t type, const uint8_t* data, size_t len);
    bool sendPacketIfSendIdle(uint8_t type, const uint8_t* data, size_t len);
    void sendAck(const char* requestId);
    void sendError(const char* requestId, const char* message);
    void sendPlaybackEvent(const char* event, const char* playbackId = nullptr);
    void sendBargeInEvent(const char* playbackId = nullptr);
    void logHeap(const char* label);
    bool hasInternalSram(size_t minimum, const char* label);
    void ackPendingAudioStart();
    void failPendingAudioStart(const char* message);
    void resetAudioStartState(const char* requestId);
    void ensureAvatar();
    void hideAvatar();
    void clearRenderScene();
    bool renderSceneJson(const std::string& data);
    void moveStatusChromeForeground();
    bool startRenderAnimation(ArduinoJson::JsonDocument& doc, const char* requestId);
    void stopRenderAnimation();
    void updateRenderAnimation();
    void setStatus(const char* mode, const char* text);
    void queueStatus(const char* mode, const char* text);
    void setLog(const char* text);
    bool ensureWakeWordDetector(float cutoff, size_t sliding_window);
    void disarmWakeWord(uint32_t wait_ms = 0);
    void releaseWakeWordDetector();
    void handleWakeWordDetected(const std::string& wake_word);
    bool captureAndSendAudioFrame();
    bool captureAndSendCameraImage(const char* requestId, bool enhance, bool preview);
    void cancelPlayback(bool emit_event = true);
    void setCurrentPlaybackId(const char* playbackId);
    void copyCurrentPlaybackId(char* playbackId, size_t len);
    uint32_t nextPlaybackGeneration();
    bool queueAudioPlayback(const char* requestId, const char* playbackId, const char* url, bool bargeIn);
    bool queueWebSocketAudioPlayback(const char* requestId, const char* playbackId, bool bargeIn);
    void drainPlaybackRingBuffer();
    size_t playbackRingBufferUsed() const;
    void queueWebSocketAudioFrame(const uint8_t* data, size_t len);
    void audioPlaybackLoop();
    void audioCaptureLoop();
    void audioCaptureSendLoop();
    bool playAudioUrl(const char* url, const char* playbackId);
    bool playWebSocketAudio(const AudioPlaybackRequest& request);
    static esp_err_t webSocketHandler(httpd_req_t* req);
    static void webSocketCloseHandler(httpd_handle_t server, int fd);
    static void audioPlaybackTaskEntry(void* arg);
    static void audioCaptureTaskEntry(void* arg);
    static void audioCaptureSendTaskEntry(void* arg);
    static void cameraTaskEntry(void* arg);
};
