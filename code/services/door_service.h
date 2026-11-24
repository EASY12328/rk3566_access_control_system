#pragma once
#include "door_status.h"
#include "drivers/servo_lock.h"
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

// The relock deadline is independent of the graphical event loop.
class DoorService {
public:
    ~DoorService();
    void start(int lockedUs, int openUs);
    DoorStatus unlock();
    DoorStatus status() const;
    void stop();

private:
    void run();
    void setState(DoorState state, std::string error = {});
    ServoLock servo_;
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    std::thread thread_;
    std::chrono::steady_clock::time_point deadline_{};
    DoorStatus status_;
    bool stopping_ = false;
};
