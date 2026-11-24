#include "bmp.h"
#include "files.h"
#include <cstddef>

namespace {
void putLe(std::string &buffer, std::size_t offset, std::uint32_t value, unsigned bytes)
{
    for (unsigned i = 0; i < bytes; ++i)
        buffer[offset + i] = static_cast<char>((value >> (8 * i)) & 255);
}
}

namespace platform {
bool saveBmp(const std::string &path, const RgbFrame &frame, std::string &error)
{
    if (frame.width <= 0 || frame.width > 8192 || frame.height <= 0 || frame.height > 8192 ||
        frame.pixels.size() != static_cast<std::size_t>(frame.width) * frame.height * 3) {
        error = "invalid RGB frame";
        return false;
    }
    const std::size_t rowBytes = static_cast<std::size_t>(frame.width) * 3;
    const std::size_t stride = (rowBytes + 3) & ~std::size_t(3);
    constexpr std::size_t HeaderSize = 54;
    std::string bmp(HeaderSize + stride * frame.height, '\0');
    bmp[0] = 'B';
    bmp[1] = 'M';
    putLe(bmp, 2, bmp.size(), 4);
    putLe(bmp, 10, HeaderSize, 4);
    putLe(bmp, 14, 40, 4);
    putLe(bmp, 18, frame.width, 4);
    putLe(bmp, 22, frame.height, 4);
    putLe(bmp, 26, 1, 2);
    putLe(bmp, 28, 24, 2);
    putLe(bmp, 34, stride * frame.height, 4);
    // BMP rows are bottom-up and store BGR instead of RGB.
    for (int y = 0; y < frame.height; ++y) {
        const auto *rgb = frame.pixels.data() + (frame.height - 1 - y) * rowBytes;
        char *bgr = bmp.data() + HeaderSize + y * stride;
        for (std::size_t x = 0; x < rowBytes; x += 3) {
            bgr[x] = static_cast<char>(rgb[x + 2]);
            bgr[x + 1] = static_cast<char>(rgb[x + 1]);
            bgr[x + 2] = static_cast<char>(rgb[x]);
        }
    }
    return atomicWrite(path, bmp, error);
}
}
