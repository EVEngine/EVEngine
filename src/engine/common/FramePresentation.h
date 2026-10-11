#pragma once

namespace eve {

/** @brief Cross-module owner for swapchain presentation cadence. */
class IFramePresentation {
public:
    static constexpr const char* capabilityName = "eve.frame-presentation";

    virtual ~IFramePresentation() = default;

    /** @brief Apply Pcg VSync count; zero selects uncapped presentation. */
    virtual void setVSyncCount(int count) = 0;
    /** @brief Return the retained Pcg VSync count. */
    virtual int getVSyncCount() const noexcept = 0;
};

}  // namespace eve
