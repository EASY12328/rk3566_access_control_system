#include "camera.h"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <time.h>
#include <utility>
#include <vector>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

namespace {
using Clock = std::chrono::steady_clock;

int ioctlRetry(int fd, unsigned long request, void *arg)
{
    int rc;
    do { rc = ioctl(fd, request, arg); } while (rc < 0 && errno == EINTR);
    return rc;
}

void check(int rc, const char *operation)
{
    if (rc < 0)
        throw std::runtime_error(std::string(operation) + ": " + strerror(errno));
}

struct Mapping {
    void *data = MAP_FAILED;
    size_t size = 0;
};

struct Stream {
    int fd = -1;
    bool running = false;
    std::vector<Mapping> buffers;

    ~Stream()
    {
        if (fd < 0)
            return;
        v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        if (running && ioctlRetry(fd, VIDIOC_STREAMOFF, &type) < 0)
            fprintf(stderr, "STREAMOFF: %s\n", strerror(errno));
        for (const auto &buffer : buffers)
            if (buffer.data != MAP_FAILED)
                munmap(buffer.data, buffer.size);
        close(fd);
    }
};

v4l2_buffer makeBuffer(v4l2_plane &plane)
{
    v4l2_buffer buffer = {};
    buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    buffer.memory = V4L2_MEMORY_MMAP;
    buffer.length = 1;
    buffer.m.planes = &plane;
    return buffer;
}

// BT.601 limited range, matching the known-good my_camera conversion.
void toRgb(const std::uint8_t *nv12, int stride, bool rotate180, RgbFrame &frame)
{
    const int width = frame.width, height = frame.height;
    const auto rowBytes = size_t(width) * 3;
    frame.pixels.resize(rowBytes * height);
    const auto *uv = nv12 + size_t(stride) * height;
    const auto clip = [](int v) { return std::uint8_t(std::max(0, std::min(255, v))); };
    const int pixelStep = rotate180 ? -3 : 3;
    for (int y = 0; y < height; ++y) {
        const auto *luma = nv12 + size_t(y) * stride;
        const auto *chroma = uv + size_t(y / 2) * stride;
        auto *row = frame.pixels.data() + rowBytes * (rotate180 ? height - 1 - y : y);
        int offset = rotate180 ? (width - 1) * 3 : 0;
        for (int x = 0; x < width; x += 2) {
            const int u = chroma[x] - 128;
            const int v = chroma[x + 1] - 128;
            const int r = 409 * v + 128;
            const int g = -100 * u - 208 * v + 128;
            const int b = 516 * u + 128;
            for (int i = 0; i < 2; ++i) {
                const int level = (luma[x + i] - 16) * 298;
                row[offset] = clip((level + r) >> 8);
                row[offset + 1] = clip((level + g) >> 8);
                row[offset + 2] = clip((level + b) >> 8);
                offset += pixelStep;
            }
        }
    }
}
}

Camera::Camera(std::string device, bool rotate180)
    : device_(std::move(device)), rotate180_(rotate180)
{
}

Camera::~Camera()
{
    stop();
}

void Camera::start()
{
    if (!thread_.joinable()) {
        stopping_ = false;
        thread_ = std::thread(&Camera::run, this);
    }
}

void Camera::stop()
{
    stopping_ = true;
    if (thread_.joinable())
        thread_.join();
}

FramePtr Camera::latest(std::string *status) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (status)
        *status = status_;
    return frame_;
}

void Camera::run()
{
    try {
        Stream stream;
        stream.fd = open(device_.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
        check(stream.fd, "open camera");
        v4l2_capability capability = {};
        check(ioctlRetry(stream.fd, VIDIOC_QUERYCAP, &capability), "QUERYCAP");
        const auto flags = capability.capabilities & V4L2_CAP_DEVICE_CAPS
            ? capability.device_caps : capability.capabilities;
        if (!(flags & V4L2_CAP_VIDEO_CAPTURE_MPLANE) || !(flags & V4L2_CAP_STREAMING))
            throw std::runtime_error("Camera needs MPLANE capture and MMAP streaming");

        v4l2_format format = {};
        format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        auto &pix = format.fmt.pix_mp;
        pix.width = 1280;
        pix.height = 720;
        pix.pixelformat = V4L2_PIX_FMT_NV12;
        pix.field = V4L2_FIELD_NONE;
        check(ioctlRetry(stream.fd, VIDIOC_S_FMT, &format), "S_FMT");
        const unsigned stride = pix.plane_fmt[0].bytesperline;
        if (pix.pixelformat != V4L2_PIX_FMT_NV12 || pix.num_planes != 1 ||
            !pix.width || !pix.height || (pix.width & 1) || (pix.height & 1) ||
            pix.width > 8192 || pix.height > 8192 || stride < pix.width || stride > 65536)
            throw std::runtime_error("Expected contiguous NV12");
        const size_t size = size_t(stride) * (pix.height + pix.height / 2);
        if (pix.plane_fmt[0].sizeimage < size)
            throw std::runtime_error("Invalid NV12 plane size");
        std::vector<std::uint8_t> raw(size);
        fprintf(stderr, "NV12: %u x %u stride %u rotation %d\n",
                pix.width, pix.height, stride, rotate180_ ? 180 : 0);

        v4l2_requestbuffers request = {};
        request.count = 4;
        request.type = format.type;
        request.memory = V4L2_MEMORY_MMAP;
        check(ioctlRetry(stream.fd, VIDIOC_REQBUFS, &request), "REQBUFS");
        if (!request.count || request.count > 32)
            throw std::runtime_error("Invalid camera buffer count");
        stream.buffers.resize(request.count);
        for (unsigned i = 0; i < request.count; ++i) {
            v4l2_plane plane = {};
            auto buffer = makeBuffer(plane);
            buffer.index = i;
            check(ioctlRetry(stream.fd, VIDIOC_QUERYBUF, &buffer), "QUERYBUF");
            auto &mapped = stream.buffers[i];
            mapped.size = plane.length;
            if (mapped.size < size)
                throw std::runtime_error("MMAP buffer is smaller than the frame");
            mapped.data = mmap(nullptr, mapped.size, PROT_READ | PROT_WRITE,
                               MAP_SHARED, stream.fd, plane.m.mem_offset);
            if (mapped.data == MAP_FAILED)
                check(-1, "mmap");
            check(ioctlRetry(stream.fd, VIDIOC_QBUF, &buffer), "Initial QBUF");
        }

        v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        check(ioctlRetry(stream.fd, VIDIOC_STREAMON, &type), "STREAMON");
        stream.running = true;
        const auto started = Clock::now();
        auto lastValid = started;
        std::uint64_t sequence = 0;
        while (!stopping_) {
            if (Clock::now() - lastValid >= std::chrono::seconds(8))
                throw std::runtime_error("No valid camera frames for 8 seconds");
            pollfd cameraPoll = {stream.fd, POLLIN, 0};
            const int ready = poll(&cameraPoll, 1, 200);
            if (ready < 0 && errno == EINTR)
                continue;
            check(ready, "poll");
            if (!ready)
                continue;
            if (cameraPoll.revents & (POLLERR | POLLHUP | POLLNVAL))
                throw std::runtime_error("Camera poll failed");
            if (!(cameraPoll.revents & POLLIN))
                continue;
            v4l2_plane plane = {};
            auto buffer = makeBuffer(plane);
            const int dequeued = ioctlRetry(stream.fd, VIDIOC_DQBUF, &buffer);
            if (dequeued < 0 && errno == EAGAIN)
                continue;
            check(dequeued, "DQBUF");
            if (buffer.index >= stream.buffers.size())
                throw std::runtime_error("Invalid dequeued buffer index");
            const auto &mapped = stream.buffers[buffer.index];
            const bool valid = !(buffer.flags & V4L2_BUF_FLAG_ERROR) &&
                plane.bytesused <= mapped.size && plane.data_offset <= plane.bytesused &&
                size_t(plane.bytesused - plane.data_offset) >= size;
            const auto received = Clock::now();
            if (valid)
                memcpy(raw.data(), static_cast<const std::uint8_t *>(mapped.data) + plane.data_offset, size);
            // Return the DMA buffer before converting the copied frame.
            check(ioctlRetry(stream.fd, VIDIOC_QBUF, &buffer), "QBUF");
            if (!valid)
                continue;
            lastValid = received;

            auto next = std::make_shared<RgbFrame>();
            next->width = pix.width;
            next->height = pix.height;
            toRgb(raw.data(), stride, rotate180_, *next);
            next->sequence = ++sequence;
            next->received = received;
            next->warmedUp = received - started >= std::chrono::seconds(3);
            // Keep only the newest image. Published pixel data is read-only.
            std::lock_guard<std::mutex> lock(mutex_);
            status_ = next->warmedUp ? "摄像头就绪" : "正在调整曝光…";
            frame_ = std::move(next);
        }
    } catch (const std::exception &error) {
        fprintf(stderr, "Camera: %s\n", error.what());
        std::lock_guard<std::mutex> lock(mutex_);
        frame_.reset();
        status_ = std::string("采集停止：") + error.what();
    }
}
