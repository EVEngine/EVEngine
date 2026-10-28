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
    /** @brief Returns the name. */
    std::string getName() const override { return "null"; }
    /** @brief True when runtime available. */
    bool        isRuntimeAvailable() const override { return false; }

    /** @brief Loads model. */
    bool loadModel(const std::string &path) override {
        path_ = path;
        return !path.empty();
    }

    /** @brief Updates . */
    void update(float /*dt*/) override {}

    /** @brief Sets the parameter. */
    void setParameter(const std::string &name, float value) override {
        if (!name.empty()) params_[name] = value;
    }

    /** @brief Returns the parameter. */
    float getParameter(const std::string &name) const override {
        auto it = params_.find(name);
        return it == params_.end() ? 0.f : it->second;
    }

    /** @brief Sets the expression. */
    bool setExpression(const std::string &name) override {
        expression_ = name;
        return !name.empty();
    }

    /** @brief Sets the motion. */
    bool setMotion(const std::string &name) override {
        motion_ = name;
        return !name.empty();
    }

    /** @brief Model path. */
    std::string modelPath() const { return path_; }
    /** @brief Expression. */
    std::string expression() const { return expression_; }
    /** @brief Motion. */
    std::string motion() const { return motion_; }

private:
    std::string path_;
    std::string expression_;
    std::string motion_;
    std::unordered_map<std::string, float> params_;
};

/** @brief Creates null live 2 d backend. */
inline ILive2DBackend *createNullLive2DBackend() { return new NullLive2DBackend(); }

}  // namespace eve::avatar
