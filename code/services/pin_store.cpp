#include "pin_store.h"
#include "platform/crypto.h"
#include "platform/files.h"
#include "platform/json.h"
#include <algorithm>
#include <utility>

namespace {
constexpr int SaltBytes = 16;
constexpr int DigestBytes = 32;
constexpr int Iterations = 600000;
constexpr auto Lockout = std::chrono::seconds(30);
using Clock = std::chrono::steady_clock;
}

PinStore::PinStore(std::string path) : path_(std::move(path)) {}

bool PinStore::validPin(const std::string &pin)
{
    return pin.size() == 6 &&
           std::all_of(pin.begin(), pin.end(), [](char c) { return c >= '0' && c <= '9'; });
}

bool PinStore::load(std::string &error)
{
    if (!digest_.empty())
        return true;
    std::string data;
    const auto status = platform::readFile(path_, 4096, data, error);
    if (status == platform::ReadStatus::Missing) {
        error = save("123456");
        return error.empty();
    }
    if (status == platform::ReadStatus::Failed)
        return false;

    const auto record = platform::parseJson(data);
    std::string salt64, digest64, salt, digest;
    if (!platform::jsonNumber(record.get(), "version", 1) ||
        !platform::jsonNumber(record.get(), "iterations", Iterations) ||
        !platform::jsonString(record.get(), "salt", salt64) ||
        !platform::jsonString(record.get(), "digest", digest64) ||
        !platform::decodeBase64(salt64, salt) || salt.size() != SaltBytes ||
        !platform::decodeBase64(digest64, digest) || digest.size() != DigestBytes) {
        error = "密码文件无效，请检查设备";
        return false;
    }
    salt_ = std::move(salt);
    digest_ = std::move(digest);
    return true;
}

std::string PinStore::verify(const std::string &pin)
{
    std::lock_guard<std::mutex> lock(mutex_);
    return verifyLocked(pin);
}

std::string PinStore::verifyLocked(const std::string &pin)
{
    if (!validPin(pin))
        return "请输入 6 位数字";
    const auto now = Clock::now();
    if (blockedUntil_ > now) {
        const auto seconds = std::chrono::ceil<std::chrono::seconds>(blockedUntil_ - now).count();
        return "尝试过多，请在 " + std::to_string(seconds) + " 秒后重试";
    }
    if (failedAttempts_ >= 5)
        failedAttempts_ = 0;

    std::string error;
    if (!load(error))
        return error;
    const auto candidate = platform::pinDigest(pin, salt_, Iterations);
    if (candidate.size() != DigestBytes)
        return "密码计算失败";
    if (platform::equalDigest(candidate, digest_)) {
        failedAttempts_ = 0;
        return {};
    }
    if (++failedAttempts_ >= 5) {
        blockedUntil_ = Clock::now() + Lockout;
        return "尝试过多，请在 30 秒后重试";
    }
    return "密码错误，请重试";
}

std::string PinStore::replace(const std::string &currentPin, const std::string &newPin)
{
    if (!validPin(newPin))
        return "新密码必须是 6 位数字";
    std::lock_guard<std::mutex> lock(mutex_);
    const auto error = verifyLocked(currentPin);
    return error.empty() ? save(newPin) : error;
}

std::string PinStore::save(const std::string &pin)
{
    auto salt = platform::randomBytes(SaltBytes);
    if (salt.size() != SaltBytes)
        return "无法生成密码盐值";
    auto digest = platform::pinDigest(pin, salt, Iterations);
    if (digest.size() != DigestBytes)
        return "密码计算失败";
    const auto record = platform::jsonObject();
    if (!cJSON_AddNumberToObject(record.get(), "version", 1) ||
        !cJSON_AddNumberToObject(record.get(), "iterations", Iterations) ||
        !cJSON_AddStringToObject(record.get(), "salt", platform::encodeBase64(salt).c_str()) ||
        !cJSON_AddStringToObject(record.get(), "digest", platform::encodeBase64(digest).c_str()))
        return "无法生成密码记录";
    const auto data = platform::printJson(record);
    std::string error;
    if (data.empty())
        return "无法生成密码记录";
    if (!platform::atomicWrite(path_, data, error))
        return "保存密码失败：" + error;
    salt_ = std::move(salt);
    digest_ = std::move(digest);
    return {};
}
