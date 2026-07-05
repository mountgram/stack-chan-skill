/*
 * SPDX-License-Identifier: MIT
 */
#include "stacky_wake_word.h"

#include "stacky_wake_word_model.h"

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/ringbuf.h>
#include <freertos/task.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <vector>

#include <esphome/components/micro_wake_word/micro_wake_word.h>
#include <esphome/components/microphone/microphone.h>

namespace {

constexpr const char* TAG = "STACKY.WAKE";
constexpr int MODEL_SAMPLE_RATE = 16000;
constexpr float STACKY_CUTOFF = 0.99f;
constexpr size_t STACKY_SLIDING_WINDOW = 10;
constexpr size_t STACKY_TENSOR_ARENA = 40000;
constexpr size_t WAKE_WORD_AUDIO_BUFFER_BYTES = 16 * 1024;
constexpr uint8_t FEATURE_STEP_MS = 10;

float clamp_cutoff(float cutoff)
{
    if (!std::isfinite(cutoff)) return STACKY_CUTOFF;
    return std::min(0.999f, std::max(0.5f, cutoff));
}

size_t clamp_sliding_window(size_t sliding_window)
{
    return std::min<size_t>(20, std::max<size_t>(1, sliding_window));
}

class StackyBufferedMicrophone : public esphome::microphone::Microphone {
public:
    StackyBufferedMicrophone()
    {
        _ringbuf = xRingbufferCreate(WAKE_WORD_AUDIO_BUFFER_BYTES, RINGBUF_TYPE_BYTEBUF);
    }

    ~StackyBufferedMicrophone()
    {
        if (_ringbuf) {
            vRingbufferDelete(_ringbuf);
            _ringbuf = nullptr;
        }
    }

    void start() override
    {
        if (!_ringbuf) {
            ESP_LOGE(TAG, "wake-word audio ring buffer unavailable");
            state_ = esphome::microphone::STATE_STOPPED;
            return;
        }
        drain();
        _resample_accumulator = 0;
        state_ = esphome::microphone::STATE_RUNNING;
        ESP_LOGI(TAG, "wake-word buffered microphone started");
    }

    void stop() override
    {
        state_ = esphome::microphone::STATE_STOPPED;
        drain();
        ESP_LOGI(TAG, "wake-word buffered microphone stopped");
    }

    size_t read(int16_t* buf, size_t len) override
    {
        if (state_ != esphome::microphone::STATE_RUNNING || !buf || len == 0 || !_ringbuf) {
            return 0;
        }

        size_t bytes = 0;
        auto* item = static_cast<uint8_t*>(xRingbufferReceiveUpTo(_ringbuf, &bytes, pdMS_TO_TICKS(20), len));
        if (!item) {
            return 0;
        }
        memcpy(buf, item, bytes);
        vRingbufferReturnItem(_ringbuf, item);
        return bytes;
    }

    void feed(const int16_t* samples, size_t frames, int sample_rate, int channels)
    {
        if (state_ != esphome::microphone::STATE_RUNNING || !_ringbuf || !samples || frames == 0) {
            return;
        }
        sample_rate = sample_rate > 0 ? sample_rate : MODEL_SAMPLE_RATE;
        channels = std::max(channels, 1);
        _feed_chunk.clear();
        _feed_chunk.reserve((frames * MODEL_SAMPLE_RATE) / sample_rate + 2);
        for (size_t i = 0; i < frames; ++i) {
            _resample_accumulator += MODEL_SAMPLE_RATE;
            if (_resample_accumulator < static_cast<uint32_t>(sample_rate)) {
                continue;
            }
            _resample_accumulator -= static_cast<uint32_t>(sample_rate);
            _feed_chunk.push_back(samples[i * channels]);
        }
        if (_feed_chunk.empty()) {
            return;
        }
        const size_t bytes = _feed_chunk.size() * sizeof(int16_t);
        if (xRingbufferSend(_ringbuf, _feed_chunk.data(), bytes, 0) != pdTRUE) {
            drainOne();
            xRingbufferSend(_ringbuf, _feed_chunk.data(), bytes, 0);
        }
    }

private:
    RingbufHandle_t _ringbuf = nullptr;
    uint32_t _resample_accumulator = 0;
    std::vector<int16_t> _feed_chunk;

    void drainOne()
    {
        if (!_ringbuf) return;
        size_t bytes = 0;
        auto* item = static_cast<uint8_t*>(xRingbufferReceiveUpTo(_ringbuf, &bytes, 0, WAKE_WORD_AUDIO_BUFFER_BYTES));
        if (item) {
            vRingbufferReturnItem(_ringbuf, item);
        }
    }

    void drain()
    {
        while (_ringbuf) {
            size_t bytes = 0;
            auto* item = static_cast<uint8_t*>(xRingbufferReceiveUpTo(_ringbuf, &bytes, 0, WAKE_WORD_AUDIO_BUFFER_BYTES));
            if (!item) break;
            vRingbufferReturnItem(_ringbuf, item);
        }
    }
};

}  // namespace

struct StackyWakeWordDetector::Impl {
    Impl(float requested_cutoff, size_t requested_sliding_window)
        : cutoff(clamp_cutoff(requested_cutoff)), sliding_window(clamp_sliding_window(requested_sliding_window))
    {
    }

    StackyBufferedMicrophone microphone;
    esphome::micro_wake_word::MicroWakeWord wake_word;
    StackyWakeWordDetector::Callback callback;
    std::atomic<TaskHandle_t> task{nullptr};
    float cutoff = STACKY_CUTOFF;
    size_t sliding_window = STACKY_SLIDING_WINDOW;
    std::atomic_bool setup{false};
    std::atomic_bool task_running{false};
    std::atomic_bool armed{false};

    bool begin(StackyWakeWordDetector::Callback cb)
    {
        if (setup) {
            callback = std::move(cb);
            return true;
        }

        callback = std::move(cb);
        wake_word.set_microphone(&microphone);
        wake_word.set_features_step_size(FEATURE_STEP_MS);
        ESP_LOGI(TAG, "wake-word model cutoff %.3f sliding_window %u", cutoff, static_cast<unsigned>(sliding_window));
        wake_word.add_wake_word_model(stacky_wake_word_tflite, cutoff, sliding_window, "Stacky",
                                      STACKY_TENSOR_ARENA);
        wake_word.add_detection_callback([this](std::string wake_word_name) {
            if (!armed.exchange(false)) {
                return;
            }
            wake_word.stop();
            if (callback) {
                callback(wake_word_name);
            }
        });
        wake_word.setup();
        if (wake_word.is_failed()) {
            ESP_LOGE(TAG, "microWakeWord setup failed");
            return false;
        }

        task_running = true;
        TaskHandle_t task_handle = nullptr;
        BaseType_t created = xTaskCreatePinnedToCore(taskEntry, "stacky_wake_word", 12288, this, 2, &task_handle, 1);
        if (created != pdPASS) {
            task_running = false;
            task.store(nullptr);
            ESP_LOGE(TAG, "failed to create wake-word task");
            return false;
        }
        task.store(task_handle);
        setup = true;
        return true;
    }

    bool arm()
    {
        if (!setup) {
            return false;
        }
        if (wake_word.is_running()) {
            armed = true;
            return true;
        }
        wake_word.start();
        if (!wake_word.is_running()) {
            armed = false;
            return false;
        }
        armed = true;
        ESP_LOGI(TAG, "wake-word detector armed");
        return true;
    }

    void disarm(uint32_t wait_ms)
    {
        armed = false;
        if (!setup || !wake_word.is_running()) {
            return;
        }
        wake_word.stop();
        if (xTaskGetCurrentTaskHandle() == task.load()) {
            return;
        }
        const uint32_t started = xTaskGetTickCount();
        const uint32_t ticks   = pdMS_TO_TICKS(wait_ms);
        while (wait_ms > 0 && wake_word.is_running() && xTaskGetTickCount() - started < ticks) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    void shutdown()
    {
        disarm(500);
        task_running = false;
        if (xTaskGetCurrentTaskHandle() == task.load()) {
            return;
        }
        const uint32_t started = xTaskGetTickCount();
        while (task.load() && xTaskGetTickCount() - started < pdMS_TO_TICKS(500)) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        if (task.load()) {
            ESP_LOGE(TAG, "wake-word task did not exit in time");
        } else {
            setup = false;
        }
    }

    void feedAudio(const int16_t* samples, size_t frames, int sample_rate, int channels)
    {
        if (!setup || !armed) return;
        microphone.feed(samples, frames, sample_rate, channels);
    }

    static void taskEntry(void* arg)
    {
        static_cast<Impl*>(arg)->loopTask();
    }

    void loopTask()
    {
        while (task_running) {
            const bool running = wake_word.is_running();
            if (running) {
                wake_word.loop();
            }
            vTaskDelay(pdMS_TO_TICKS(running ? 10 : 50));
        }
        task.store(nullptr);
        vTaskDelete(nullptr);
    }
};

StackyWakeWordDetector::StackyWakeWordDetector(float cutoff, size_t sliding_window)
    : _impl(std::make_unique<Impl>(cutoff, sliding_window))
{
}

StackyWakeWordDetector::~StackyWakeWordDetector()
{
    shutdown();
}

bool StackyWakeWordDetector::begin(Callback callback)
{
    return _impl->begin(std::move(callback));
}

bool StackyWakeWordDetector::arm()
{
    return _impl->arm();
}

void StackyWakeWordDetector::disarm(uint32_t wait_ms)
{
    _impl->disarm(wait_ms);
}

void StackyWakeWordDetector::shutdown()
{
    _impl->shutdown();
}

void StackyWakeWordDetector::feedAudio(const int16_t* samples, size_t frames, int sample_rate, int channels)
{
    _impl->feedAudio(samples, frames, sample_rate, channels);
}

bool StackyWakeWordDetector::isArmed() const
{
    return _impl->armed;
}
