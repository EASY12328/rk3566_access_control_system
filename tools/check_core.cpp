#include "app/access_decision.h"
#include "platform/bmp.h"
#include "platform/crypto.h"
#include "platform/files.h"
#include "platform/process.h"
#include "services/pin_store.h"
#include "services/user_store.h"
#include <chrono>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool result, const char *label)
{
    if (!result)
        throw std::runtime_error(label);
}
}

int main()
{
    using platform::ReadStatus;
    try {
        platform::TemporaryDirectory files;
        check(!files.path().empty(), "temporary directory");
        const auto path = [&](const char *name) { return files.path() + "/" + name; };
        std::string error;
        std::string feature(FeatureBytes, '\0');
        feature[2] = static_cast<char>(0x80);
        feature[3] = static_cast<char>(0x3f);
        const std::string portrait(FaceRgbBytes, '\0');
        const auto fixture = "{\"users\":[{\"feature\":\"" + platform::encodeBase64(feature) +
                             "\",\"id\":2,\"portraitRgb\":\"\"}],\"version\":1}";
        check(platform::atomicWrite(path("users.json"), fixture, error), "write Qt-format roster");
        check(platform::atomicWrite(path("legacy.f32"), feature, error), "write legacy feature");
        UserStore users(path("users.json"));
        check(users.load(path("legacy.f32"), error) && users.users().size() == 1 &&
              users.users().front().id == 2 && users.users().front().feature == feature,
              "load Qt-format roster");
        int id = 0;
        check(users.add(feature, portrait, id, error) && id == 1, "reuse lowest available slot");
        check(users.remove(2, error) && users.remove(1, error), "delete users");
        UserStore reloaded(path("users.json"));
        check(reloaded.load(path("legacy.f32"), error) && reloaded.users().empty(),
              "empty roster remains empty after restart");

        // Known PBKDF2-SHA256 output generated independently for the previous
        // Qt JSON format (123456, salt bytes 00..0f, 600000 iterations).
        const std::string oldPin = R"({"digest":"k5IuOfF746yC7knkG2ibL4Jf/LmxjGRAY1DJK0+jxZw=","iterations":600000,"salt":"AAECAwQFBgcICQoLDA0ODw==","version":1})";
        check(platform::atomicWrite(path("pin.json"), oldPin, error), "write Qt-format PIN");
        PinStore pin(path("pin.json"));
        check(pin.verify("123456").empty(), "read existing PIN with OpenSSL");
        check(!pin.replace("111111", "583920").empty(), "wrong old PIN cannot change password");
        check(pin.replace("123456", "583920").empty(), "change PIN");
        PinStore newPin(path("pin.json"));
        check(!newPin.verify("123456").empty() && newPin.verify("583920").empty(),
              "changed password survives reload");

        RgbFrame frame;
        frame.width = 2;
        frame.height = 1;
        frame.pixels = {255, 0, 0, 0, 0, 255};
        check(platform::saveBmp(path("frame.bmp"), frame, error), "save frame as BMP");
        std::string bmp;
        check(platform::readFile(path("frame.bmp"), 80, bmp, error) == ReadStatus::Ok &&
              bmp.size() == 62 && bmp.substr(0, 2) == "BM" &&
              static_cast<unsigned char>(bmp[56]) == 255 && static_cast<unsigned char>(bmp[57]) == 255,
              "verify BGR order and padding");
        const auto child = platform::runProcess("/bin/sh", {"-c", "printf child-ok"}, files.path(), files.path());
        check(child.ok && child.output == "child-ok", "run tool in separate process");
        check(allowsOpen(true, 0.50, 0.50) && !allowsOpen(false, 1, 0.50), "allow threshold and fail closed");
        std::cout << "Core checks passed without Qt or hardware\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "Core check failed: " << error.what() << '\n';
        return 1;
    }
}
