#include "face_service.h"
#include "user_store.h"
#include "platform/bmp.h"
#include "platform/files.h"
#include <cstdio>
#include <utility>

FaceService::FaceService(std::string recognizerPath, std::string snapshotPath)
    : recognizerPath_(platform::absolutePath(recognizerPath)),
      snapshotPath_(platform::absolutePath(snapshotPath))
{
}

std::string FaceService::legacyFeature() const
{
    return recognizerPath_ + "/board_feature.f32";
}

bool FaceService::extract(const RgbFrame &frame, bool enrolling, FaceSample &sample,
                          std::string &error, const platform::Cancel &cancel) const
{
    if (cancel && cancel()) {
        error = "已取消";
        return false;
    }
    for (const char *tool : {"detect_face", "face_align", "sface_extract"}) {
        if (!platform::availableFile(recognizerPath_ + "/" + tool, true)) {
            error = std::string("缺少识别工具：") + tool;
            return false;
        }
    }
    for (const char *model : {"detector.rknn", "sface_rk3566_fp16.rknn"}) {
        if (!platform::availableFile(recognizerPath_ + "/" + model)) {
            error = std::string("缺少识别模型：") + model;
            return false;
        }
    }

    platform::TemporaryDirectory work;
    if (work.path().empty()) {
        error = "无法创建识别临时目录";
        return false;
    }
    const auto input = enrolling ? work.path() + "/enrollment.bmp" : snapshotPath_;
    std::string detail;
    if (!platform::saveBmp(input, frame, detail)) {
        fprintf(stderr, "Snapshot: %s\n", detail.c_str());
        error = "保存画面失败";
        return false;
    }

    const auto run = [&](const char *name, const std::vector<std::string> &args) {
        const auto result = platform::runProcess(recognizerPath_ + "/" + name, args, work.path(),
                                                 recognizerPath_ + "/lib", cancel);
        if (!result.ok)
            fprintf(stderr, "%s: %s\n%s\n", name, result.error.c_str(), result.output.c_str());
        return result;
    };
    const auto detection = run("detect_face", {recognizerPath_ + "/detector.rknn", input});
    if (!detection.ok) {
        if (detection.output.find("Expected one face, found 0") != std::string::npos)
            error = "没检测到人脸，请正对摄像头";
        else if (detection.output.find("Expected one face, found ") != std::string::npos)
            error = "请保持画面中只有一张人脸";
        else
            error = "人脸检测失败";
        return false;
    }
    if (!run("face_align", {"detected_input.rgb", std::to_string(frame.width),
                           std::to_string(frame.height), "detected_points.txt", "aligned.rgb"}).ok) {
        error = "人脸对齐失败";
        return false;
    }
    if (!run("sface_extract", {recognizerPath_ + "/sface_rk3566_fp16.rknn",
                              "aligned.rgb", "feature.f32"}).ok) {
        error = "人脸特征提取失败";
        return false;
    }
    FaceFeature decoded;
    if (platform::readFile(work.path() + "/feature.f32", FeatureBytes, sample.feature, detail) !=
            platform::ReadStatus::Ok || !decodeFaceFeature(sample.feature, decoded)) {
        error = "人脸特征格式无效";
        return false;
    }
    if (enrolling &&
        (platform::readFile(work.path() + "/aligned.rgb", FaceRgbBytes, sample.portraitRgb, detail) !=
         platform::ReadStatus::Ok || sample.portraitRgb.size() != FaceRgbBytes)) {
        error = "人脸头像格式无效";
        return false;
    }
    return true;
}
