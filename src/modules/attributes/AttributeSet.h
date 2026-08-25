#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace eve::attributes {

/** @brief One runtime modifier applied to a named attribute. */
struct AttributeModifier {
    std::string   id;
    std::string   attribute;
    std::string   source;
    std::string   operation;
    double        value    = 0.0;
    int           priority = 0;
    std::uint64_t sequence = 0;
};

/** @brief Generic collection of named numeric attributes and modifiers. */
class AttributeSet {
public:
    /** @brief Construct a set associated with an optional stable subject id. */
    explicit AttributeSet(std::string subject = {});

    /** @brief Stable game-defined id of the entity or object owning this set. */
    const std::string& subject() const;
    /** @brief Set or replace a base value. */
    void setBase(const std::string& attribute, double value);
    /** @brief Add delta to a base value, creating it from zero when absent. */
    void modifyBase(const std::string& attribute, double delta);
    /** @brief Return whether a base value or modifier names the attribute. */
    bool has(const std::string& attribute) const;
    /** @brief Return a base value, or fallback when absent. */
    double getBase(const std::string& attribute, double fallback = 0.0) const;
    /** @brief Return the deterministic final value after all modifiers. */
    double getFinal(const std::string& attribute, double fallback = 0.0) const;

    /**
     * @brief Add or replace a modifier by stable id.
     * @return false for an empty id/attribute or unsupported operation.
     *
     * Operations are add, multiply, override, min and max. Modifiers are
     * evaluated by priority then insertion sequence; replacing an id keeps a
     * new sequence so the result remains deterministic and explicit.
     */
    bool addModifier(const std::string& id, const std::string& attribute, const std::string& source,
                     const std::string& operation, double value, int priority = 0);
    /** @brief Remove one modifier by stable id. */
    bool removeModifier(const std::string& id);
    /** @brief Remove all modifiers from source, optionally limited to one attribute. */
    int removeBySource(const std::string& source, const std::string& attribute = {});
    /** @brief Remove all modifiers. */
    void clearModifiers();
    /** @brief Number of modifiers. */
    int modifierCount() const;
    /** @brief Return a modifier by deterministic sequence order, or nullptr. */
    const AttributeModifier* modifierAt(int index) const;

private:
    void rebuildOrder() const;

    std::string                                        subject_;
    std::unordered_map<std::string, double>            bases_;
    std::unordered_map<std::string, AttributeModifier> modifiers_;
    mutable std::vector<const AttributeModifier*>      order_;
    mutable bool                                       orderDirty_   = true;
    std::uint64_t                                      nextSequence_ = 1;
};

}  // namespace eve::attributes
