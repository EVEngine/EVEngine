#include "attributes/AttributeProjectionCapture.h"
#include <charconv>
#include "attributes/AttributeProjection.h"
namespace eve::attributes {
struct AttributeProjectionCapture::Impl {
    PreparedAttributeSet values;
    Revision             revision;
    bool                 initialized;
    Impl(PreparedAttributeSet state, Revision rev, bool seeded)
        : values(std::move(state)), revision(rev), initialized(seeded) {}
};
struct PreparedAttributeProjection::Impl {
    PreparedAttributeSet values;
    Revision             revision;
    bool                 initialized;
    Impl(PreparedAttributeSet state, Revision rev, bool seeded)
        : values(std::move(state)), revision(rev), initialized(seeded) {}
};
AttributeProjectionCapture::AttributeProjectionCapture(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
AttributeProjectionCapture::~AttributeProjectionCapture()                                                = default;
AttributeProjectionCapture::AttributeProjectionCapture(AttributeProjectionCapture&&) noexcept            = default;
AttributeProjectionCapture& AttributeProjectionCapture::operator=(AttributeProjectionCapture&&) noexcept = default;
std::optional<std::reference_wrapper<const AttributeSet>> AttributeProjectionCapture::values() const noexcept {
    if (!impl_) return std::nullopt;
    return std::cref(impl_->values.view());
}
Revision AttributeProjectionCapture::revision() const noexcept { return impl_ ? impl_->revision : Revision::zero(); }
std::optional<bool> AttributeProjectionCapture::initialized() const noexcept {
    if (!impl_) return std::nullopt;
    return impl_->initialized;
}
Result<Value> AttributeProjectionCapture::encodeState() const {
    if (!impl_)
        return Result<Value>::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation, "Attribute capture moved"));
    auto values = impl_->values.view().encodeState();
    if (!values) return Result<Value>::failure(values.status());
    auto result = Value::object({{"schema", "attributes.projection"},
                                 {"version", 1},
                                 {"revision", std::to_string(impl_->revision.value())},
                                 {"initialized", impl_->initialized},
                                 {"values", std::move(values).takeValue()}});
    auto valid  = prepareState(result);
    if (!valid) return Result<Value>::failure(valid.status());
    return Result<Value>::success(std::move(result));
}
Result<AttributeProjectionCapture> AttributeProjectionCapture::prepareState(const Value& state) {
    using R          = Result<AttributeProjectionCapture>;
    const auto* root = state.getIf<Value::Object>();
    if (!root || root->size() != 5 || !root->contains("schema") || !root->contains("version") ||
        !root->contains("revision") || !root->contains("initialized") || !root->contains("values"))
        return R::failure(Diagnostic::error(DiagnosticCode::ParseError, "Unknown or missing projection fields"));
    const auto* schema = root->at("schema").getIf<std::string>();
    if (!schema || *schema != "attributes.projection")
        return R::failure(Diagnostic::error(DiagnosticCode::ParseError, "Invalid projection schema"));
    if (!root->at("version").isInt64() || root->at("version").asInt() != 1)
        return R::failure(Diagnostic::error(DiagnosticCode::UnknownVersion, "Unknown projection version"));
    const auto*   text        = root->at("revision").getIf<std::string>();
    const auto*   initialized = root->at("initialized").getIf<bool>();
    std::uint64_t revision    = 0;
    if (!text || !initialized)
        return R::failure(Diagnostic::error(DiagnosticCode::ParseError, "Invalid projection metadata types"));
    const auto parsed = std::from_chars(text->data(), text->data() + text->size(), revision);
    if (parsed.ec != std::errc{} || parsed.ptr != text->data() + text->size() || std::to_string(revision) != *text ||
        *initialized != (revision != 0))
        return R::failure(Diagnostic::error(DiagnosticCode::Conflict, "Projection initialization/revision mismatch"));
    auto values = AttributeSet::prepareState(root->at("values"));
    if (!values) return R::failure(values.status());
    if (!*initialized && values.value().view().attributeCount() != 0)
        return R::failure(Diagnostic::error(DiagnosticCode::Conflict, "Uninitialized projection has attribute facts"));
    return R::success(AttributeProjectionCapture(
        std::make_unique<Impl>(std::move(values).takeValue(), Revision(revision), *initialized)));
}
Result<AttributeProjectionCapture> AttributeProjection::capture() const {
    if (owner_.table && (!ecs::try_get(owner_) || owner_.table->is_deferred()))
        return Result<AttributeProjectionCapture>::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation, "Attribute capture requires a live idle owner"));
    return Result<AttributeProjectionCapture>::success(AttributeProjectionCapture(
        std::make_unique<AttributeProjectionCapture::Impl>(values_.prepareCopy(), revision_, initialized_)));
}
PreparedAttributeProjection::PreparedAttributeProjection(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
PreparedAttributeProjection::~PreparedAttributeProjection()                                                 = default;
PreparedAttributeProjection::PreparedAttributeProjection(PreparedAttributeProjection&&) noexcept            = default;
PreparedAttributeProjection& PreparedAttributeProjection::operator=(PreparedAttributeProjection&&) noexcept = default;
Result<PreparedAttributeProjection> PreparedAttributeProjection::prepare(const AttributeProjectionCapture& captured) {
    if (!captured.impl_)
        return Result<PreparedAttributeProjection>::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation, "Attribute capture moved"));
    return Result<PreparedAttributeProjection>::success(PreparedAttributeProjection(std::make_unique<Impl>(
        captured.impl_->values.view().prepareCopy(), captured.impl_->revision, captured.impl_->initialized)));
}
Result<void> AttributeProjection::validateRestore(const PreparedAttributeProjection& prepared,
                                                  ecs::EntityHandle                  owner) const {
    if (!prepared.impl_)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation, "Attribute token consumed"));
    if (!owner.table || !ecs::try_get(owner))
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::StaleHandle, "Attribute target owner retired"));
    if (owner.table->is_deferred())
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation, "Attribute restore needs idle owner"));
    if (owner_.table && !ownsSameLiveEntity(owner))
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "Attribute target belongs to another incarnation"));
    if (values_.subject() != prepared.impl_->values.view().subject())
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "Attribute subject differs from checkpoint"));
    return Result<void>::success();
}
Result<void> AttributeProjection::restore(PreparedAttributeProjection&& prepared, ecs::EntityHandle owner) {
    auto valid = validateRestore(prepared, owner);
    if (!valid) return valid;
    values_.commitPrepared(std::move(prepared.impl_->values));
    owner_       = owner;
    revision_    = prepared.impl_->revision;
    initialized_ = prepared.impl_->initialized;
    prepared.impl_.reset();
    return Result<void>::success(Status::success(StatusCode::Applied));
}
}  // namespace eve::attributes
