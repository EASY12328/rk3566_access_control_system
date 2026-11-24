#pragma once
#include <string>

// Owns PWM8 channel 0 on the Taishan Pi while the service is active.
class ServoLock {
public:
    static constexpr int MinPulseUs = 1000;
    static constexpr int MaxPulseUs = 2100;
    ServoLock() = default;
    ~ServoLock();
    ServoLock(const ServoLock &) = delete;
    ServoLock &operator=(const ServoLock &) = delete;

    static bool validPositions(int lockedUs, int openUs);
    bool start(int lockedUs, int openUs, std::string &error);
    bool moveOpen(std::string &error);
    bool moveLocked(std::string &error);
    void stop();

private:
    bool writePulse(int microseconds, std::string &error);
    std::string chipPath_;
    int lockedUs_ = 0;
    int openUs_ = 0;
    bool exported_ = false;
    bool enabled_ = false;
};
