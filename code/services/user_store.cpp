#include "user_store.h"
#include "platform/crypto.h"
#include "platform/files.h"
#include "platform/json.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <utility>

bool decodeFaceFeature(const std::string &bytes, FaceFeature &feature)
{
    static_assert(sizeof(float) == sizeof(std::uint32_t), "SFace uses 32-bit floats");
    if (bytes.size() != FeatureBytes)
        return false;
    double norm = 0.0;
    for (std::size_t i = 0; i < feature.size(); ++i) {
        const auto *data = reinterpret_cast<const unsigned char *>(bytes.data() + i * 4);
        const std::uint32_t bits = data[0] | (std::uint32_t(data[1]) << 8) |
                                   (std::uint32_t(data[2]) << 16) | (std::uint32_t(data[3]) << 24);
        memcpy(&feature[i], &bits, sizeof(bits));
        if (!std::isfinite(feature[i]))
            return false;
        norm += double(feature[i]) * feature[i];
    }
    return norm >= 1e-24;
}

double faceSimilarity(const FaceFeature &first, const FaceFeature &second)
{
    double dot = 0.0, firstNorm = 0.0, secondNorm = 0.0;
    for (std::size_t i = 0; i < first.size(); ++i) {
        dot += double(first[i]) * second[i];
        firstNorm += double(first[i]) * first[i];
        secondNorm += double(second[i]) * second[i];
    }
    return std::clamp(dot / std::sqrt(firstNorm * secondNorm), -1.0, 1.0);
}

UserStore::UserStore(std::string path) : path_(std::move(path)) {}

bool UserStore::load(const std::string &legacyFeaturePath, std::string &error)
{
    using platform::ReadStatus;
    loaded_ = false;
    users_.clear();
    std::vector<FaceUser> records;
    std::string data;
    const auto status = platform::readFile(path_, 1024 * 1024, data, error);
    if (status == ReadStatus::Failed)
        return false;
    if (status == ReadStatus::Missing) {
        const auto legacyStatus = platform::readFile(legacyFeaturePath, FeatureBytes, data, error);
        if (legacyStatus == ReadStatus::Failed)
            return false;
        if (legacyStatus == ReadStatus::Ok) {
            FaceFeature feature;
            if (!decodeFaceFeature(data, feature)) {
                error = "旧人脸特征无效";
                return false;
            }
            records.push_back(FaceUser{1, std::move(data), {}});
        }
        if (!save(records, error))
            return false;
    } else {
        const auto doc = platform::parseJson(data);
        const auto *entries = platform::jsonField(doc.get(), "users");
        if (!platform::jsonNumber(doc.get(), "version", 1) || !cJSON_IsArray(entries) ||
            cJSON_GetArraySize(entries) > MaxUsers) {
            error = "用户名单格式无效";
            return false;
        }
        std::array<bool, MaxUsers + 1> occupied{};
        const cJSON *entry = nullptr;
        cJSON_ArrayForEach(entry, entries) {
            const auto *id = platform::jsonField(entry, "id");
            std::string feature, portrait;
            FaceUser user;
            FaceFeature decoded;
            if (!cJSON_IsNumber(id) || id->valuedouble < 1 || id->valuedouble > MaxUsers ||
                id->valueint != id->valuedouble || occupied[id->valueint] ||
                !platform::jsonString(entry, "feature", feature) ||
                !platform::jsonString(entry, "portraitRgb", portrait) ||
                !platform::decodeBase64(feature, user.feature) || !decodeFaceFeature(user.feature, decoded) ||
                !platform::decodeBase64(portrait, user.portraitRgb) ||
                (!user.portraitRgb.empty() && user.portraitRgb.size() != FaceRgbBytes)) {
                error = "用户名单中有无效记录";
                return false;
            }
            user.id = id->valueint;
            occupied[user.id] = true;
            records.push_back(std::move(user));
        }
        std::sort(records.begin(), records.end(),
                  [](const FaceUser &a, const FaceUser &b) { return a.id < b.id; });
    }
    users_ = std::move(records);
    loaded_ = true;
    return true;
}

int UserStore::freeId() const
{
    if (!loaded_)
        return 0;
    int id = 1;
    for (const auto &user : users_) {
        if (user.id != id)
            break;
        ++id;
    }
    return id <= MaxUsers ? id : 0;
}

bool UserStore::add(const std::string &feature, const std::string &portrait, int &id, std::string &error)
{
    id = freeId();
    FaceFeature decoded;
    if (!loaded_ || id == 0 || !decodeFaceFeature(feature, decoded) || portrait.size() != FaceRgbBytes) {
        error = !loaded_ ? "用户名单不可用" : id == 0 ? "已达到 10 位用户上限" : "录入的人脸数据无效";
        return false;
    }
    auto records = users_;
    const auto position = std::lower_bound(records.begin(), records.end(), id,
                                            [](const FaceUser &entry, int slot) { return entry.id < slot; });
    records.insert(position, FaceUser{id, feature, portrait});
    if (!save(records, error))
        return false;
    users_ = std::move(records);
    return true;
}

bool UserStore::remove(int id, std::string &error)
{
    if (!loaded_) {
        error = "用户名单不可用";
        return false;
    }
    auto records = users_;
    const auto it = std::find_if(records.begin(), records.end(),
                                 [id](const FaceUser &entry) { return entry.id == id; });
    if (it == records.end()) {
        error = "用户已不在名单中";
        return false;
    }
    records.erase(it);
    if (!save(records, error))
        return false;
    users_ = std::move(records);
    return true;
}

bool UserStore::save(const std::vector<FaceUser> &users, std::string &error)
{
    auto doc = platform::jsonObject();
    auto *entries = cJSON_AddArrayToObject(doc.get(), "users");
    if (!entries || !cJSON_AddNumberToObject(doc.get(), "version", 1)) {
        error = "无法生成用户名单";
        return false;
    }
    for (const auto &user : users) {
        auto entry = platform::jsonObject();
        if (!cJSON_AddNumberToObject(entry.get(), "id", user.id) ||
            !cJSON_AddStringToObject(entry.get(), "feature", platform::encodeBase64(user.feature).c_str()) ||
            !cJSON_AddStringToObject(entry.get(), "portraitRgb", platform::encodeBase64(user.portraitRgb).c_str()) ||
            !cJSON_AddItemToArray(entries, entry.get())) {
            error = "无法生成用户记录";
            return false;
        }
        entry.release(); // The JSON array owns the entry.
    }
    const auto data = platform::printJson(doc);
    if (data.empty()) {
        error = "无法生成用户名单";
        return false;
    }
    return platform::atomicWrite(path_, data, error);
}
