#include "door_service.h"
#include <cstdio>
#include <utility>

namespace {
constexpr auto OpenTime = std::chrono::seconds(3);
}

DoorService::~DoorService()
{
    stop();
}

void DoorService::setState(DoorState state, std::string error)
{
    status_.state = state;
    status_.error = std::move(error);
    ++status_.revision;
}

void DoorService::start(int lockedUs, int openUs)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (thread_.joinable())
        return;
    stopping_ = false;
    if (lockedUs == 0 && openUs == 0) {
        setState(DoorState::Simulation);
        return;
    }
    std::string error;
    if (!servo_.start(lockedUs, openUs, error)) {
        fprintf(stderr, "PWM8 init: %s\n", error.c_str());
        setState(DoorState::Fault, "舵机初始化失败");
        return;
    }
    setState(DoorState::Locked);
    thread_ = std::thread(&DoorService::run, this);
}

DoorStatus DoorService::unlock()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopping_ || status_.state != DoorState::Locked)
        return status_;
    std::string error;
    if (!servo_.moveOpen(error)) {
        fprintf(stderr, "PWM8 open: %s\n", error.c_str());
        if (!servo_.moveLocked(error))
            fprintf(stderr, "PWM8 recovery: %s\n", error.c_str());
        setState(DoorState::Fault, "开锁指令失败");
        return status_;
    }
    fprintf(stderr, "Servo: open pulse sent\n");
    deadline_ = std::chrono::steady_clock::now() + OpenTime;
    setState(DoorState::Open);
    changed_.notify_one();
    return status_;
}

DoorStatus DoorService::status() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return status_;
}

void DoorService::run()
{
    std::unique_lock<std::mutex> lock(mutex_);
    while (!stopping_) {
        changed_.wait(lock, [this] { return stopping_ || status_.state == DoorState::Open; });
        if (stopping_)
            break;
        if (changed_.wait_until(lock, deadline_, [this] { return stopping_; }))
            break;
        std::string error;
        if (servo_.moveLocked(error)) {
            fprintf(stderr, "Servo: lock pulse sent\n");
            setState(DoorState::Locked);
        } else {
            fprintf(stderr, "PWM8 lock: %s\n", error.c_str());
            setState(DoorState::Fault, "回锁指令失败，请检查舵机");
        }
    }
}

void DoorService::stop()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_)
            return;
        stopping_ = true;
    }
    changed_.notify_one();
    if (thread_.joinable())
        thread_.join();
    servo_.stop();
}
