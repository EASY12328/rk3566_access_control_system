#pragma once
#include <string>

namespace platform {
std::string encodeBase64(const std::string &bytes);
bool decodeBase64(const std::string &text, std::string &bytes);
std::string randomBytes(int size);
std::string pinDigest(const std::string &pin, const std::string &salt, int iterations);
bool equalDigest(const std::string &a, const std::string &b);
}
