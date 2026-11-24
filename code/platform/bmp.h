#pragma once
#include "frame.h"
#include <string>

namespace platform {
bool saveBmp(const std::string &path, const RgbFrame &frame, std::string &error);
}
