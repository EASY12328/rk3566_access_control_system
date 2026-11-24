#pragma once
#include "platform/frame.h"
#include "services/door_status.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

struct AccessOptions {
    std::string device = "/dev/video0";
    std::string output = "/userdata/access_preview/captures";
    std::string recognizer = "/userdata/detect_align_package";
    std::string usersFile = "/userdata/access_preview/users.json";
    std::string pinFile = "/userdata/access_preview/pin.json";
    bool rotate180 = true;
    double threshold = 0.50;
    int lockedUs = 0;
    int openUs = 0;
};

struct UserView {
    int id;
    std::string portraitRgb;
};

struct FaceResult {
    bool compared = false;
    bool allowed = false;
    double score = 0.0;
    int userId = 0;
    std::string error;
    DoorStatus door;
};

struct PinResult {
    std::string error;
    DoorStatus door;
};

// Stable, Qt-free API between the user interface and the access controller.
class AccessApp {
public:
    explicit AccessApp(AccessOptions options);
    ~AccessApp();
    AccessApp(const AccessApp &) = delete;
    AccessApp &operator=(const AccessApp &) = delete;

    bool start(std::string &error);
    void stop();
    static bool validServoPositions(int lockedUs, int openUs);
    static int minPulseUs();
    static int maxPulseUs();
    static bool validPin(const std::string &pin);
    static int maxUsers();
    static int portraitSize();
    double threshold() const;
    FramePtr latestFrame(std::string *status = nullptr) const;
    DoorStatus doorStatus() const;

    std::string userError() const;
    std::vector<UserView> users() const;
    bool canEnroll() const;
    bool removeUser(int id, std::string &error);
    FaceResult scanFrame(const FramePtr &frame, bool enrolling, const std::function<bool()> &cancel);

    PinResult verifyPin(const std::string &pin, bool openDoor);
    std::string replacePin(const std::string &currentPin, const std::string &newPin);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
