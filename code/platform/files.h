#pragma once
#include <cstddef>
#include <string>

namespace platform {
enum class ReadStatus { Ok, Missing, Failed };
ReadStatus readFile(const std::string &path, std::size_t limit, std::string &data, std::string &error);
bool atomicWrite(const std::string &path, const std::string &data, std::string &error);
bool writeDevice(const std::string &path, const std::string &value, std::string &error);
bool availableFile(const std::string &path, bool executable = false);
std::string absolutePath(const std::string &path);

class TemporaryDirectory {
public:
    TemporaryDirectory();
    ~TemporaryDirectory();
    TemporaryDirectory(const TemporaryDirectory &) = delete;
    TemporaryDirectory &operator=(const TemporaryDirectory &) = delete;
    const std::string &path() const { return path_; }

private:
    std::string path_;
};

class InstanceLock {
public:
    explicit InstanceLock(const std::string &path);
    ~InstanceLock();
    InstanceLock(const InstanceLock &) = delete;
    InstanceLock &operator=(const InstanceLock &) = delete;
    bool locked() const { return fd_ >= 0; }

private:
    int fd_ = -1;
};
}
