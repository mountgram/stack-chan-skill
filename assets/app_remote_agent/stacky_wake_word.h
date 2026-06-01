/*
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

class StackyWakeWordDetector {
public:
    using Callback = std::function<void(const std::string&)>;

    StackyWakeWordDetector();
    ~StackyWakeWordDetector();

    bool begin(Callback callback);
    bool arm();
    void disarm(uint32_t wait_ms = 0);
    void shutdown();
    bool isArmed() const;

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};
