#pragma once
#include <array>
#include <string>
#include <vector>

constexpr int MaxUsers = 10;
constexpr int FaceSize = 112;
constexpr int FaceRgbBytes = FaceSize * FaceSize * 3;
using FaceFeature = std::array<float, 128>;
constexpr int FeatureBytes = 512;

struct FaceUser {
    int id = 0;
    std::string feature;
    std::string portraitRgb;
};

bool decodeFaceFeature(const std::string &bytes, FaceFeature &feature);
double faceSimilarity(const FaceFeature &first, const FaceFeature &second);

class UserStore {
public:
    explicit UserStore(std::string path);
    bool load(const std::string &legacyFeaturePath, std::string &error);
    const std::vector<FaceUser> &users() const { return users_; }
    int freeId() const;
    bool add(const std::string &feature, const std::string &portrait, int &id, std::string &error);
    bool remove(int id, std::string &error);

private:
    bool save(const std::vector<FaceUser> &users, std::string &error);
    std::string path_;
    std::vector<FaceUser> users_;
    bool loaded_ = false;
};
