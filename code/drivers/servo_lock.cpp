#include "servo_lock.h"
#include "platform/files.h"
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <thread>

namespace {
constexpr int ServoPeriodNs = 20000000;
constexpr auto ReturnTravel = std::chrono::milliseconds(500);
namespace fs = std::filesystem;
}

ServoLock::~ServoLock()
{
    stop();
}

bool ServoLock::validPositions(int lockedUs, int openUs)
{
    return lockedUs >= MinPulseUs && lockedUs <= MaxPulseUs &&
           openUs >= MinPulseUs && openUs <= MaxPulseUs && lockedUs != openUs;
}

bool ServoLock::start(int lockedUs, int openUs, std::string &error)
{
    if (exported_ || !validPositions(lockedUs, openUs)) {
        error = "舵机已初始化或锁位和开位无效";
        return false;
    }
    // Locate the physical PWM8 controller. pwmchip numbers can change at boot.
    const fs::path controller("/sys/devices/platform/fe6f0000.pwm/pwm");
    std::error_code ec;
    std::string candidate;
    unsigned count = 0;
    for (fs::directory_iterator it(controller, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->path().filename().string().rfind("pwmchip", 0) == 0 && it->is_directory(ec)) {
            candidate = it->path().string();
            ++count;
        }
    }
    if (ec || count != 1) {
        error = "找不到泰山派 PWM8 控制器";
        return false;
    }
    chipPath_ = candidate;
    if (fs::exists(chipPath_ + "/pwm0", ec) || ec) {
        error = "PWM8 已被占用或无法访问";
        return false;
    }
    if (!platform::writeDevice(chipPath_ + "/export", "0", error))
        return false;

    exported_ = true;
    lockedUs_ = lockedUs;
    openUs_ = openUs;
    const auto channel = chipPath_ + "/pwm0";
    // This kernel rejects polarity changes while the period is zero.
    if (!platform::writeDevice(channel + "/period", std::to_string(ServoPeriodNs), error) ||
        !platform::writeDevice(channel + "/polarity", "normal", error) ||
        !writePulse(lockedUs_, error)) {
        stop();
        return false;
    }
    // Even if the driver reports an error, it may already have enabled PWM.
    enabled_ = true;
    if (!platform::writeDevice(channel + "/enable", "1", error)) {
        stop();
        return false;
    }
    fprintf(stderr, "PWM8 ready: locked pulse %d us\n", lockedUs_);
    return true;
}

bool ServoLock::writePulse(int microseconds, std::string &error)
{
    return platform::writeDevice(chipPath_ + "/pwm0/duty_cycle",
                                 std::to_string(microseconds * 1000), error);
}

bool ServoLock::moveOpen(std::string &error)
{
    if (!enabled_) {
        error = "舵机尚未就绪";
        return false;
    }
    return writePulse(openUs_, error);
}

bool ServoLock::moveLocked(std::string &error)
{
    if (!enabled_) {
        error = "舵机尚未就绪";
        return false;
    }
    return writePulse(lockedUs_, error);
}

void ServoLock::stop()
{
    if (!exported_)
        return;
    std::string error;
    if (enabled_) {
        if (moveLocked(error))
            std::this_thread::sleep_for(ReturnTravel);
        else
            fprintf(stderr, "PWM8 return: %s\n", error.c_str());

        if (!platform::writeDevice(chipPath_ + "/pwm0/enable", "0", error)) {
            fprintf(stderr, "PWM8 disable: %s\n", error.c_str());
            return;
        }
        enabled_ = false;
    }
    if (platform::writeDevice(chipPath_ + "/unexport", "0", error))
        exported_ = false;
    else
        fprintf(stderr, "PWM8 unexport: %s\n", error.c_str());
}
