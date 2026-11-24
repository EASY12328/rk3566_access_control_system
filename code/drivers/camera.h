#pragma once
#include "platform/frame.h"
#include <atomic>
#include <mutex>
#include <string>
#include <thread>

class Camera {
public:
    Camera(std::string device, bool rotate180);
    ~Camera();
    void start();
    void stop();
    FramePtr latest(std::string *status = nullptr) const;

private:
    void run();
    std::string device_;
    const bool rotate180_;
    mutable std::mutex mutex_;
    std::thread thread_;
    std::atomic<bool> stopping_{false};
    FramePtr frame_;
    std::string status_ = "正在打开摄像头…";
};
