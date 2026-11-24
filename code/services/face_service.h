#pragma once
#include "platform/frame.h"
#include "platform/process.h"
#include <string>

struct FaceSample {
    std::string feature;
    std::string portraitRgb;
};

class FaceService {
public:
    FaceService(std::string recognizerPath, std::string snapshotPath);
    std::string legacyFeature() const;
    bool extract(const RgbFrame &frame, bool enrolling, FaceSample &sample,
                 std::string &error, const platform::Cancel &cancel) const;

private:
    std::string recognizerPath_;
    std::string snapshotPath_;
};
