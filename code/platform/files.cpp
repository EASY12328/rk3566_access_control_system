#include "files.h"
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;
namespace platform {
ReadStatus readFile(const std::string &path, std::size_t limit, std::string &data, std::string &error)
{
    data.clear();
    const int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        if (errno == ENOENT)
            return ReadStatus::Missing;
        error = path + ": " + strerror(errno);
        return ReadStatus::Failed;
    }
    ReadStatus status = ReadStatus::Ok;
    char buffer[4096];
    for (;;) {
        const auto count = read(fd, buffer, sizeof(buffer));
        if (count < 0 && errno == EINTR)
            continue;
        if (count == 0)
            break;
        if (count < 0 || static_cast<std::size_t>(count) > limit - data.size()) {
            error = path + ": " + (count < 0 ? strerror(errno) : "file is too large");
            status = ReadStatus::Failed;
            break;
        }
        data.append(buffer, static_cast<std::size_t>(count));
    }
    close(fd);
    return status;
}

bool atomicWrite(const std::string &path, const std::string &data, std::string &error)
{
    const auto parent = fs::path(path).parent_path();
    std::error_code ec;
    if (!parent.empty())
        fs::create_directories(parent, ec);
    if (ec) {
        error = path + ": " + ec.message();
        return false;
    }

    std::string temp = path + ".tmp.XXXXXX";
    const int fd = mkostemp(temp.data(), O_CLOEXEC);
    if (fd < 0) {
        error = path + ": " + strerror(errno);
        return false;
    }
    int failure = 0;
    std::size_t written = 0;
    while (written < data.size()) {
        const auto count = write(fd, data.data() + written, data.size() - written);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0) {
            failure = count < 0 ? errno : EIO;
            break;
        }
        written += static_cast<std::size_t>(count);
    }
    if (!failure && fsync(fd) < 0)
        failure = errno;
    if (close(fd) < 0 && !failure)
        failure = errno;
    if (!failure && rename(temp.c_str(), path.c_str()) < 0)
        failure = errno;
    if (failure) {
        unlink(temp.c_str());
        error = path + ": " + strerror(failure);
        return false;
    }
    // Sync the directory entry where supported. The rename has already committed.
    const int dir = open(parent.empty() ? "." : parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dir >= 0) {
        fsync(dir);
        close(dir);
    }
    return true;
}

bool writeDevice(const std::string &path, const std::string &value, std::string &error)
{
    const int fd = open(path.c_str(), O_WRONLY | O_CLOEXEC);
    if (fd < 0) {
        error = path + ": " + strerror(errno);
        return false;
    }
    ssize_t count;
    do { count = write(fd, value.data(), value.size()); } while (count < 0 && errno == EINTR);
    const int failure = count < 0 ? errno : EIO;
    close(fd);
    if (count != static_cast<ssize_t>(value.size())) {
        error = path + ": " + strerror(failure);
        return false;
    }
    return true;
}

bool availableFile(const std::string &path, bool executable)
{
    struct stat info {};
    return stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode) &&
           access(path.c_str(), executable ? X_OK : R_OK) == 0;
}

std::string absolutePath(const std::string &path)
{
    return fs::absolute(path).lexically_normal().string();
}

TemporaryDirectory::TemporaryDirectory()
{
    char name[] = "/tmp/access-preview-XXXXXX";
    if (mkdtemp(name))
        path_ = name;
}

TemporaryDirectory::~TemporaryDirectory()
{
    if (!path_.empty()) {
        std::error_code ignored;
        fs::remove_all(path_, ignored);
    }
}

InstanceLock::InstanceLock(const std::string &path)
{
    fd_ = open(path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd_ >= 0 && flock(fd_, LOCK_EX | LOCK_NB) < 0) {
        close(fd_);
        fd_ = -1;
    }
}

InstanceLock::~InstanceLock()
{
    if (fd_ >= 0)
        close(fd_);
}
}
