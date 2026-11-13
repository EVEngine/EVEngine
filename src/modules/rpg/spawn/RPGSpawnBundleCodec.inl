#pragma once
namespace eve::rpg_spawn {
namespace {
struct BundleError {
    Diagnostic diagnostic;
};
[[noreturn]] void bundleReject(std::string message, DiagnosticCode code = DiagnosticCode::ParseError) {
    throw BundleError{Diagnostic::error(code, std::move(message), "rpg.spawn.bundle")};
}
struct BundleReader {
    const Value& value;
    void         fields(std::initializer_list<std::string_view> allowed) const {
        if (!value.isObject()) bundleReject("Expected bundle object");
        for (const auto& key : value.keys())
            if (std::find(allowed.begin(), allowed.end(), key) == allowed.end())
                bundleReject("Unknown bundle field: " + key);
    }
    BundleReader at(const std::string& key) const {
        const auto* field = value.find(key);
        if (!field) bundleReject("Missing bundle field: " + key);
        return {*field};
    }
};
LogicalId bundleSchema() { return *LogicalId::fromParts("rpg.spawn", "bundle"); }
}  // namespace
Result<SnapshotEnvelope> RPGSpawnJointRestore::sealBundle(const SnapshotEnvelope& coreInput,
                                                          const SnapshotEnvelope& domainInput, PersistentId epoch,
                                                          std::span<const spawn::SavedLifecycleInput> pendingInput,
                                                          const SnapshotHashProvider&                 hash) {
    using R                                        = Result<SnapshotEnvelope>;
    auto                                    core   = coreInput;
    auto                                    domain = domainInput;
    std::vector<spawn::SavedLifecycleInput> pending(pendingInput.begin(), pendingInput.end());
    auto                                    admitted = prepare(core, domain, epoch, hash, pending);
    if (!admitted) return R::failure(admitted.status());
    auto coreValue = snapshotEnvelopeValue(core);
    if (!coreValue) return R::failure(coreValue.status());
    auto domainValue = snapshotEnvelopeValue(domain);
    if (!domainValue) return R::failure(domainValue.status());
    auto events = spawn::encodeSavedLifecycleInputs(pending);
    if (!events) return R::failure(events.status());
    auto payload = Value::object({{"core", std::move(coreValue).takeValue()},
                                  {"domain", std::move(domainValue).takeValue()},
                                  {"pending", std::move(events).takeValue()}});
    return makeSnapshotEnvelope("eve.rpg-spawn-bundle", bundleSchema(), SchemaVersion(1), epoch, core.revision,
                                core.tick, std::move(payload), hash);
}
Result<RPGSpawnJointRestore> RPGSpawnJointRestore::prepareBundle(
    const SnapshotEnvelope& input, PersistentId epoch, const SnapshotHashProvider& hash,
    std::shared_ptr<const rpg::SkillConditionBindings> conditions) {
    using R       = Result<RPGSpawnJointRestore>;
    auto bundle   = input;
    auto verified = verifySnapshotEnvelope(bundle, hash);
    if (!verified) return R::failure(verified.status());
    if (bundle.type != "eve.rpg-spawn-bundle" || bundle.schema != bundleSchema())
        return R::failure(Diagnostic::error(DiagnosticCode::Unsupported, "Expected RPG Spawn bundle"));
    if (bundle.schemaVersion != SchemaVersion(1))
        return R::failure(Diagnostic::error(DiagnosticCode::UnknownVersion, "Unknown RPG Spawn bundle version"));
    if (epoch.isNil() || bundle.instanceId != epoch)
        return R::failure(Diagnostic::error(DiagnosticCode::Conflict, "Bundle save identity mismatch"));
    try {
        BundleReader payload{bundle.payload};
        payload.fields({"core", "domain", "pending"});
        auto core = parseSnapshotEnvelopeValue(payload.at("core").value, hash);
        if (!core) return R::failure(core.status());
        auto domain = parseSnapshotEnvelopeValue(payload.at("domain").value, hash);
        if (!domain) return R::failure(domain.status());
        if (core.value().revision != bundle.revision || core.value().tick != bundle.tick ||
            domain.value().revision != bundle.revision || domain.value().tick != bundle.tick)
            bundleReject("Bundle and participant revision/tick disagree", DiagnosticCode::Conflict);
        auto pending = spawn::prepareSavedLifecycleInputs(payload.at("pending").value);
        if (!pending) return R::failure(pending.status());
        return prepare(core.value(), domain.value(), epoch, hash, pending.value(), std::move(conditions));
    } catch (const BundleError& error) {
        return R::failure(error.diagnostic);
    }
}
}  // namespace eve::rpg_spawn
