/*
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

class StackyWakeWordDetector {
public:
    using Callback = std::function<void(const std::string&)>;

    StackyWakeWordDetector(float cutoff, size_t sliding_window);
    ~StackyWakeWordDetector();

    bool begin(Callback callback);
    bool arm();
    void disarm(uint32_t wait_ms = 0);
    void shutdown();
    void feedAudio(const int16_t* samples, size_t frames, int sample_rate, int channels);
    bool isArmed() const;

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};
