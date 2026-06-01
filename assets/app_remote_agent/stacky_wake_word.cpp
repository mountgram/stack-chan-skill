/*
 * SPDX-License-Identifier: MIT
 */
#include "stacky_wake_word.h"

#include "stacky_wake_word_model.h"

#include <audio/audio_codec.h>
#include <board.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
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
constexpr float STACKY_CUTOFF = 0.97f;
constexpr size_t STACKY_SLIDING_WINDOW = 5;
constexpr size_t STACKY_TENSOR_ARENA = 40000;
constexpr uint8_t FEATURE_STEP_MS = 10;

class StackyAudioCodecMicrophone : public esphome::microphone::Microphone {
public:
    void start() override
    {
        auto audio_codec = Board::GetInstance().GetAudioCodec();
        if (!audio_codec) {
            ESP_LOGE(TAG, "audio codec unavailable");
            state_ = esphome::microphone::STATE_STOPPED;
            return;
        }

        _input_sample_rate = audio_codec->input_sample_rate() > 0 ? audio_codec->input_sample_rate() : MODEL_SAMPLE_RATE;
        _input_channels    = std::max(audio_codec->input_channels(), 1);
        audio_codec->EnableInput(true);
        state_ = esphome::microphone::STATE_RUNNING;
        ESP_LOGI(TAG, "wake-word microphone started at %d Hz, %d channel(s)", _input_sample_rate, _input_channels);
    }

    void stop() override
    {
        auto audio_codec = Board::GetInstance().GetAudioCodec();
        if (audio_codec && audio_codec->input_enabled()) {
            audio_codec->EnableInput(false);
        }
        state_ = esphome::microphone::STATE_STOPPED;
        ESP_LOGI(TAG, "wake-word microphone stopped");
    }

    size_t read(int16_t* buf, size_t len) override
    {
        if (state_ != esphome::microphone::STATE_RUNNING || !buf || len == 0) {
            return 0;
        }

        auto audio_codec = Board::GetInstance().GetAudioCodec();
        if (!audio_codec) {
            vTaskDelay(pdMS_TO_TICKS(10));
            return 0;
        }

        const size_t output_samples = len / sizeof(int16_t);
        if (output_samples == 0) {
            return 0;
        }

        _input_sample_rate = audio_codec->input_sample_rate() > 0 ? audio_codec->input_sample_rate() : MODEL_SAMPLE_RATE;
        _input_channels    = std::max(audio_codec->input_channels(), 1);

        if (_input_sample_rate == MODEL_SAMPLE_RATE) {
            _input_chunk.resize(output_samples * _input_channels);
            if (!audio_codec->InputData(_input_chunk)) {
                vTaskDelay(pdMS_TO_TICKS(10));
                return 0;
            }
            for (size_t i = 0; i < output_samples; ++i) {
                buf[i] = _input_chunk[i * _input_channels];
            }
            return output_samples * sizeof(int16_t);
        }

        const float ratio = static_cast<float>(_input_sample_rate) / static_cast<float>(MODEL_SAMPLE_RATE);
        const size_t input_frames =
            std::max<size_t>(2, static_cast<size_t>(std::ceil((output_samples - 1) * ratio)) + 1);
        _input_chunk.resize(input_frames * _input_channels);
        if (!audio_codec->InputData(_input_chunk)) {
            vTaskDelay(pdMS_TO_TICKS(10));
            return 0;
        }

        for (size_t i = 0; i < output_samples; ++i) {
            const float position = static_cast<float>(i) * ratio;
            const size_t index   = std::min(static_cast<size_t>(position), input_frames - 1);
            const size_t next    = std::min(index + 1, input_frames - 1);
            const float frac     = position - static_cast<float>(index);
            const int32_t a      = _input_chunk[index * _input_channels];
            const int32_t b      = _input_chunk[next * _input_channels];
            buf[i]               = static_cast<int16_t>(a + static_cast<int32_t>((b - a) * frac));
        }

        return output_samples * sizeof(int16_t);
    }

private:
    int _input_sample_rate = MODEL_SAMPLE_RATE;
    int _input_channels    = 1;
    std::vector<int16_t> _input_chunk;
};

}  // namespace

struct StackyWakeWordDetector::Impl {
    StackyAudioCodecMicrophone microphone;
    esphome::micro_wake_word::MicroWakeWord wake_word;
    StackyWakeWordDetector::Callback callback;
    TaskHandle_t task = nullptr;
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
        wake_word.add_wake_word_model(stacky_wake_word_tflite, STACKY_CUTOFF, STACKY_SLIDING_WINDOW, "Stacky",
                                      STACKY_TENSOR_ARENA);
        wake_word.add_detection_callback([this](std::string wake_word_name) {
            armed = false;
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
        BaseType_t created = xTaskCreate(taskEntry, "stacky_wake_word", 12288, this, 2, &task);
        if (created != pdPASS) {
            task_running = false;
            task         = nullptr;
            ESP_LOGE(TAG, "failed to create wake-word task");
            return false;
        }

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
        if (xTaskGetCurrentTaskHandle() == task) {
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
        if (xTaskGetCurrentTaskHandle() == task) {
            return;
        }
        const uint32_t started = xTaskGetTickCount();
        while (task && xTaskGetTickCount() - started < pdMS_TO_TICKS(500)) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        setup = false;
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
        task = nullptr;
        vTaskDelete(nullptr);
    }
};

StackyWakeWordDetector::StackyWakeWordDetector() : _impl(std::make_unique<Impl>()) {}

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

bool StackyWakeWordDetector::isArmed() const
{
    return _impl->armed;
}
