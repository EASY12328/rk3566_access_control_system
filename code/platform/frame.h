#pragma once
#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

struct RgbFrame {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels;
    std::uint64_t sequence = 0;
    bool warmedUp = false;
    std::chrono::steady_clock::time_point received;

    bool fresh() const {
        return std::chrono::steady_clock::now() - received < std::chrono::milliseconds(1500);
    }
};

using FramePtr = std::shared_ptr<const RgbFrame>;
