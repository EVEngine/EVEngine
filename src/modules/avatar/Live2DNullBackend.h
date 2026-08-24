#pragma once

#include "avatar/AvatarInstance.h"

#include <string>
#include <unordered_map>

namespace eve::avatar {

/**
 * @brief Built-in Live2D placeholder backend (no Cubism SDK).
 * Stores path / parameters / expression / motion so scripts and tests can run
 * without a proprietary runtime. Replace via Avatar::registerLive2DBackend
 * (see examples/live2d-backend-plugin).
 */
class NullLive2DBackend : public ILive2DBackend {
public:
    std::string getName() const override { return "null"; }
    bool        isRuntimeAvailable() const override { return false; }

    bool loadModel(const std::string &path) override {
        path_ = path;
        return !path.empty();
    }

    void update(float /*dt*/) override {}

    void setParameter(const std::string &name, float value) override {
        if (!name.empty()) params_[name] = value;
    }

    float getParameter(const std::string &name) const override {
        auto it = params_.find(name);
        return it == params_.end() ? 0.f : it->second;
    }

    bool setExpression(const std::string &name) override {
        expression_ = name;
        return !name.empty();
    }

    bool setMotion(const std::string &name) override {
        motion_ = name;
        return !name.empty();
    }

    std::string modelPath() const { return path_; }
    std::string expression() const { return expression_; }
    std::string motion() const { return motion_; }

private:
    std::string path_;
    std::string expression_;
    std::string motion_;
    std::unordered_map<std::string, float> params_;
};

inline ILive2DBackend *createNullLive2DBackend() { return new NullLive2DBackend(); }

}  // namespace eve::avatar
