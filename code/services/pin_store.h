#pragma once
#include <chrono>
#include <mutex>
#include <string>

class PinStore {
public:
    explicit PinStore(std::string path);
    static bool validPin(const std::string &pin);
    std::string verify(const std::string &pin);
    std::string replace(const std::string &currentPin, const std::string &newPin);

private:
    bool load(std::string &error);
    std::string verifyLocked(const std::string &pin);
    std::string save(const std::string &pin);
    std::string path_;
    std::mutex mutex_;
    std::string salt_;
    std::string digest_;
    int failedAttempts_ = 0;
    std::chrono::steady_clock::time_point blockedUntil_{};
};
