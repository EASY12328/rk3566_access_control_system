#pragma once
#include "third_party/cjson/cJSON.h"
#include <memory>
#include <string>

namespace platform {
using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;
inline Json parseJson(const std::string &text) {
    if (text.find('\0') != std::string::npos)
        return Json(nullptr, cJSON_Delete);
    return Json(cJSON_ParseWithLengthOpts(text.c_str(), text.size() + 1, nullptr, true), cJSON_Delete);
}
inline Json jsonObject() { return Json(cJSON_CreateObject(), cJSON_Delete); }
inline std::string printJson(const Json &object) {
    const std::unique_ptr<char, decltype(&cJSON_free)> text(cJSON_PrintUnformatted(object.get()), cJSON_free);
    return text ? std::string(text.get()) : std::string();
}
inline const cJSON *jsonField(const cJSON *object, const char *key) {
    return cJSON_GetObjectItemCaseSensitive(object, key);
}
inline bool jsonNumber(const cJSON *object, const char *key, int expected) {
    const auto *field = jsonField(object, key);
    return cJSON_IsNumber(field) && field->valuedouble == expected;
}
inline bool jsonString(const cJSON *object, const char *key, std::string &out) {
    const char *value = cJSON_GetStringValue(jsonField(object, key));
    if (!value)
        return false;
    out = value;
    return true;
}
}
