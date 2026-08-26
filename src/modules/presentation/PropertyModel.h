#pragma once

#include "common/Export.h"
#include "presentation/Value.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace eve::presentation {

/** @brief Semantic property kind used by renderer-independent presenters. */
enum class PropertyKind {
    Auto,
    Bool,
    Integer,
    Number,
    String,
    Enum,
    Color,
    Vec2,
    Vec3,
    Vec4,
    AssetRef,
    ObjectRef,
    Struct,
    Array,
    Map,
    Action,
    ReadOnlyText
};

/** @brief Cross-host property visibility and editing flags. */
enum class PropertyFlag : std::uint64_t {
    None = 0,
    ReadOnly = 1ull << 0,
    Advanced = 1ull << 1,
    EditorOnly = 1ull << 2,
    Runtime = 1ull << 3,
    Transient = 1ull << 4,
    Dangerous = 1ull << 5,
    MultiEdit = 1ull << 6
};

constexpr PropertyFlag operator|(PropertyFlag left, PropertyFlag right) {
    return static_cast<PropertyFlag>(static_cast<std::uint64_t>(left) |
                                     static_cast<std::uint64_t>(right));
}
constexpr PropertyFlag operator&(PropertyFlag left, PropertyFlag right) {
    return static_cast<PropertyFlag>(static_cast<std::uint64_t>(left) &
                                     static_cast<std::uint64_t>(right));
}
constexpr bool hasFlag(PropertyFlag value, PropertyFlag flag) { return (value & flag) == flag; }

/** @brief Numeric editing metadata independent of a slider or spin-box widget. */
struct NumericMetadata {
    std::optional<double> minimum;
    std::optional<double> maximum;
    std::optional<double> step;
    std::string units;
    int precision = 3;
};

/** @brief One stable property description consumed by any UI shell. */
struct PropertyDescriptor {
    std::string path;
    std::string displayName;
    std::string description;
    std::string category;
    PropertyKind kind = PropertyKind::Auto;
    PropertyFlag flags = PropertyFlag::None;
    Value defaultValue;
    NumericMetadata numeric;
    std::vector<std::string> choices;
    std::string presenterHint;
};

/** @brief Versioned schema for one view model or editable target. */
struct PropertySchema {
    std::string typeId;
    std::uint32_t version = 1;
    std::vector<PropertyDescriptor> properties;

    /** @brief Find a property by stable path, or nullptr. */
    const PropertyDescriptor *find(const std::string &path) const;
};

/** @brief Result of a model write, including a stable diagnostic code. */
struct WriteResult {
    bool accepted = false;
    std::string code;
    std::string message;

    static WriteResult success() { return {true, {}, {}}; }
    static WriteResult reject(std::string code, std::string message) {
        return {false, std::move(code), std::move(message)};
    }
};

/** @brief Immutable notification emitted after a model property changes. */
struct PropertyChange {
    std::string path;
    Value value;
    std::uint64_t revision = 0;
};

/** @brief Move-only lifetime token for a property-model observer. */
class EVENGINE_API Subscription {
public:
    Subscription() = default;
    explicit Subscription(std::function<void()> dispose);
    Subscription(const Subscription &) = delete;
    Subscription &operator=(const Subscription &) = delete;
    Subscription(Subscription &&other) noexcept;
    Subscription &operator=(Subscription &&other) noexcept;
    ~Subscription();

    /** @brief Disconnect the observer. Safe to call repeatedly. */
    void dispose();
    /** @brief True after disposal or a move. */
    bool disposed() const { return disposed_; }

private:
    std::function<void()> dispose_;
    bool disposed_ = false;
};

/**
 * @brief Renderer- and transaction-independent MVVM property surface.
 *
 * Implementations may wrap gameplay state, reflected script objects, editor
 * targets or remote automation. Writes express intent; the implementation
 * decides whether to assign directly, dispatch a command, or reject it.
 */
class EVENGINE_API IPropertyModel {
public:
    using ChangeCallback = std::function<void(const PropertyChange &)>;

    virtual ~IPropertyModel() = default;
    /** @brief Return the stable schema exposed by this model. */
    virtual const PropertySchema &schema() const = 0;
    /** @brief Read a property value, or nullopt when it is absent. */
    virtual std::optional<Value> read(const std::string &path) const = 0;
    /** @brief Request a two-way binding write. */
    virtual WriteResult write(const std::string &path, const Value &value) = 0;
    /** @brief Monotonic revision incremented after observable changes. */
    virtual std::uint64_t revision() const = 0;
    /** @brief Observe changes until the returned token is destroyed. */
    virtual Subscription subscribe(ChangeCallback callback) = 0;
};

/** @brief General-purpose observable view model for game and tool UI state. */
class EVENGINE_API DynamicPropertyModel final : public IPropertyModel {
public:
    explicit DynamicPropertyModel(PropertySchema schema = {});
    ~DynamicPropertyModel() override;
    DynamicPropertyModel(const DynamicPropertyModel &) = delete;
    DynamicPropertyModel &operator=(const DynamicPropertyModel &) = delete;

    const PropertySchema &schema() const override { return schema_; }
    std::optional<Value> read(const std::string &path) const override;
    WriteResult write(const std::string &path, const Value &value) override;
    std::uint64_t revision() const override { return revision_; }
    Subscription subscribe(ChangeCallback callback) override;

    /** @brief Replace the schema and discard values not present in it. */
    void setSchema(PropertySchema schema);
    /** @brief Set state from the model side using the same validation and notifications. */
    WriteResult set(const std::string &path, Value value);

private:
    struct ObserverState;
    WriteResult validate(const PropertyDescriptor &property, const Value &value) const;
    void emit(const std::string &path, const Value &value);

    PropertySchema schema_;
    std::map<std::string, Value> values_;
    std::uint64_t revision_ = 0;
    std::shared_ptr<ObserverState> observers_;
};

}  // namespace eve::presentation
