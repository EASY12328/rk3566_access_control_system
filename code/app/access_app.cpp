#include "access_app.h"
#include "access_decision.h"
#include "drivers/camera.h"
#include "platform/files.h"
#include "services/door_service.h"
#include "services/face_service.h"
#include "services/pin_store.h"
#include "services/user_store.h"
#include <atomic>
#include <cstdio>
#include <mutex>
#include <utility>

class AccessApp::Impl {
public:
    explicit Impl(AccessOptions config)
        : options(std::move(config)), camera(options.device, options.rotate180),
          face(options.recognizer, options.output + "/latest.bmp"),
          users(options.usersFile), pin(options.pinFile)
    {
    }

    const AccessOptions options;
    Camera camera;
    DoorService door;
    FaceService face;
    UserStore users;
    PinStore pin;
    mutable std::mutex usersMutex;
    std::string userError;
    platform::InstanceLock lock{"/tmp/access_preview.flock"};
    std::atomic<bool> stopping{false};
};

AccessApp::AccessApp(AccessOptions options) : impl_(std::make_unique<Impl>(std::move(options))) {}
AccessApp::~AccessApp() { stop(); }

bool AccessApp::start(std::string &error)
{
    if (!impl_->lock.locked()) {
        error = "Preview is already running or /tmp is not writable";
        return false;
    }
    if (!impl_->users.load(impl_->face.legacyFeature(), impl_->userError))
        fprintf(stderr, "User list: %s\n", impl_->userError.c_str());
    impl_->door.start(impl_->options.lockedUs, impl_->options.openUs);
    impl_->camera.start();
    return true;
}

void AccessApp::stop()
{
    if (!impl_->stopping.exchange(true)) {
        impl_->door.stop();
        impl_->camera.stop();
    }
}

bool AccessApp::validServoPositions(int lockedUs, int openUs)
{
    return ServoLock::validPositions(lockedUs, openUs);
}
int AccessApp::minPulseUs() { return ServoLock::MinPulseUs; }
int AccessApp::maxPulseUs() { return ServoLock::MaxPulseUs; }
bool AccessApp::validPin(const std::string &pin) { return PinStore::validPin(pin); }
int AccessApp::maxUsers() { return MaxUsers; }
int AccessApp::portraitSize() { return FaceSize; }
double AccessApp::threshold() const { return impl_->options.threshold; }
FramePtr AccessApp::latestFrame(std::string *status) const { return impl_->camera.latest(status); }
DoorStatus AccessApp::doorStatus() const { return impl_->door.status(); }
std::string AccessApp::userError() const { return impl_->userError; }

std::vector<UserView> AccessApp::users() const
{
    std::lock_guard<std::mutex> lock(impl_->usersMutex);
    std::vector<UserView> list;
    for (const auto &user : impl_->users.users())
        list.push_back(UserView{user.id, user.portraitRgb});
    return list;
}

bool AccessApp::canEnroll() const
{
    std::lock_guard<std::mutex> lock(impl_->usersMutex);
    return impl_->users.freeId() != 0;
}

bool AccessApp::removeUser(int id, std::string &error)
{
    std::lock_guard<std::mutex> lock(impl_->usersMutex);
    return impl_->users.remove(id, error);
}

FaceResult AccessApp::scanFrame(const FramePtr &frame, bool enrolling, const std::function<bool()> &cancel)
{
    FaceResult result;
    const auto interrupted = [&] { return impl_->stopping || (cancel && cancel()); };
    if (!frame || !frame->warmedUp || !frame->fresh()) {
        result.error = "摄像头尚未就绪";
        return result;
    }
    if (!impl_->userError.empty()) {
        result.error = impl_->userError;
        return result;
    }
    std::vector<FaceUser> references;
    {
        std::lock_guard<std::mutex> lock(impl_->usersMutex);
        if (enrolling && impl_->users.freeId() == 0) {
            result.error = "已达到 10 位用户上限";
            return result;
        }
        if (!enrolling)
            references = impl_->users.users();
    }
    if (!enrolling && references.empty()) {
        result.error = "请先录入人脸";
        return result;
    }

    FaceSample sample;
    if (!impl_->face.extract(*frame, enrolling, sample, result.error, interrupted) || interrupted())
        return result;
    if (enrolling) {
        std::lock_guard<std::mutex> lock(impl_->usersMutex);
        int id = 0;
        if (impl_->users.add(sample.feature, sample.portraitRgb, id, result.error))
            result.userId = id;
        return result;
    }

    FaceFeature candidate;
    if (!decodeFaceFeature(sample.feature, candidate)) {
        result.error = "人脸特征格式无效";
        return result;
    }
    for (const auto &user : references) {
        FaceFeature reference;
        if (!decodeFaceFeature(user.feature, reference)) {
            result.error = "用户名单中的参考特征无效";
            return result;
        }
        const double score = faceSimilarity(reference, candidate);
        if (result.userId == 0 || score > result.score) {
            result.userId = user.id;
            result.score = score;
        }
    }
    result.compared = true;
    result.allowed = allowsOpen(result.compared, result.score, threshold());
    fprintf(stderr, "Face similarity: %.6f best user slot %d\n", result.score, result.userId);
    fprintf(stderr, "Access decision: %s score %.6f threshold %.6f\n",
            result.allowed ? "ALLOW" : "DENY", result.score, threshold());
    if (result.allowed && !interrupted())
        result.door = impl_->door.unlock();
    return result;
}

PinResult AccessApp::verifyPin(const std::string &pin, bool openDoor)
{
    PinResult result;
    result.error = impl_->pin.verify(pin);
    if (result.error.empty() && openDoor && !impl_->stopping) {
        fprintf(stderr, "Access decision: ALLOW (PIN)\n");
        result.door = impl_->door.unlock();
    }
    return result;
}

std::string AccessApp::replacePin(const std::string &currentPin, const std::string &newPin)
{
    if (impl_->stopping)
        return "程序正在退出";
    const auto error = impl_->pin.replace(currentPin, newPin);
    if (error.empty())
        fprintf(stderr, "PIN updated\n");
    return error;
}
