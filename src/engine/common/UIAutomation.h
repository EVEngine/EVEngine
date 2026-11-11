#pragma once

// Capability interface: semantic inspection and interaction for retained game
// UI. DevTools consumes this without depending on the ui module.

#include "common/Export.h"
#include "common/Result.h"

#include <string>

namespace eve {

/** @brief Semantic retained-UI automation provided by the ui module. */
class EVENGINE_API_FOUNDATION_INLINE IUIAutomation {
public:
    static constexpr const char* capabilityName = "IUIAutomation";

    /** @brief Iui automation. */
    virtual ~IUIAutomation() = default;

    /**
     * @brief Serialize visible and hidden retained UI hosts and their widget trees.
     * @param host Optional exact host name; empty returns all hosts.
     * @return Compact JSON, or an error string beginning with `error:`.
     */
    virtual std::string tree(const std::string& host) const = 0;

    /**
     * @brief Read one widget's semantic state.
     * @param host Optional exact host name used to disambiguate widget ids.
     * @param widget Widget id.
     * @return Compact JSON, or an error string beginning with `error:`.
     */
    virtual std::string get(const std::string& host, const std::string& widget) const = 0;

    /**
     * @brief Queue a semantic click through the normal UI event path.
     * @param host Optional exact host name used to disambiguate widget ids.
     * @param widget Clickable widget id.
     * @return Compact acknowledgement JSON, or an error string beginning with `error:`.
     */
    virtual std::string click(const std::string& host, const std::string& widget) = 0;

    /** @brief Replace one editable text widget value and queue its normal text-change event.
     * @param host Optional exact host name; duplicate matching widgets reject.
     * @param widget InputText or SearchField id in a visible, enabled host/tree.
     * @param text Owning complete replacement; empty clears the field. No SDL text chunk truncation.
     * @return Success means queued, not applied to a document. Missing/ambiguous/hidden/disabled/read-only widgets
     * reject.
     * @ownership Input strings are call-only borrows/owning values; the queued event owns its payload and weak world
     * witness.
     * @thread UI owner thread, outside Views. No callbacks during this call; normal dispatch performs callbacks later.
     * @remarks World/tree retirement before dispatch drops the event. This does not bypass form validation/history.
     * @cost Linear semantic search and text copying per explicit edit; no per-frame lookup.
     */
    [[nodiscard]] virtual Result<void> setText(const std::string& host, const std::string& widget,
                                               std::string text) = 0;
};

}  // namespace eve
