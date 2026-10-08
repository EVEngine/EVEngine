#pragma once
#include "common/Export.h"


#include "ui/UIHost.h"
#include "ui/Widget.h"

#include <memory>
#include <string>

namespace eve::ui {

/**
 * @brief React-style UI component: implement build(), call setState / markDirty, then rebuild().
 * Mounts onto a named UIHost via mountAs / attach.
 */
class EVENGINE_API_WORLD Component {
public:
    /** @brief Component. */
    virtual ~Component() = default;

    /** @brief Builds . */
    virtual WidgetDesc build() = 0;

    /** @brief Attaches . */
    void attach(UIHostHandle host);
    /** @brief Mounts as. */
    void mountAs(const std::string &hostName);
    /** @brief Returns the attached host handle, or an empty handle before attach. */
    [[nodiscard]] UIHostHandle host() const noexcept { return host_; }

    /** @brief Rebuild tree onto host (reconcile by key when possible). */
    void rebuild(bool forceFull = false);

    /** @brief Mark dirty. */
    void markDirty() { dirty_ = true; }
    /** @brief True when dirty. */
    bool isDirty() const { return dirty_; }

    /** @brief If dirty, rebuild and clear flag. Returns true if rebuilt. */
    bool updateIfDirty();

protected:
    /** @brief Subclasses call after mutating local state that affects build(). */
    void setState() { dirty_ = true; }

private:
    UIHostHandle host_{};
    bool dirty_ = true;
};

}  // namespace eve::ui
