#include "asset/EvaManifest.h"

#include <algorithm>
#include <set>

namespace eve::asset {
namespace {

const Value* required(const Value::Object& object, std::string_view key) {
    const auto found = object.find(std::string(key));
    return found == object.end() ? nullptr : &found->second;
}

Result<AssetRef> parseAssetRef(const Value& value, std::string path) {
    if (!value.isString())
        return Result<AssetRef>::failure(Diagnostic::error(
            DiagnosticCode::ParseError, "asset reference must be a string", std::move(path), {}, "asset.eva.manifest"));
    auto parsed = AssetRef::parse(value.asString());
    if (!parsed)
        return Result<AssetRef>::failure(Diagnostic::error(
            DiagnosticCode::ParseError, "asset reference is not canonical", std::move(path), {}, "asset.eva.manifest"));
    return Result<AssetRef>::success(std::move(parsed).takeValue());
}

Result<std::string> parseString(const Value::Object& object, std::string_view key,
                                std::string_view pathPrefix) {
    const Value* value = required(object, key);
    const std::string path = std::string(pathPrefix) + std::string(key);
    if (!value || !value->isString())
        return Result<std::string>::failure(Diagnostic::error(
            DiagnosticCode::ParseError, "required manifest field must be a string", path, {}, "asset.eva.manifest"));
    if (value->asString().empty())
        return Result<std::string>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                              "required manifest string must not be empty", path, {},
                                                              "asset.eva.manifest"));
    return Result<std::string>::success(value->asString());
}

Result<SchemaVersion> parseVersion(const Value::Object& object, std::string_view key,
                                   std::string_view pathPrefix) {
    const Value* value = required(object, key);
    const std::string path = std::string(pathPrefix) + std::string(key);
    if (!value || !value->isInt64() || value->asInt() <= 0)
        return Result<SchemaVersion>::failure(Diagnostic::error(
            DiagnosticCode::ParseError, "schema version must be a positive integer", path, {}, "asset.eva.manifest"));
    return Result<SchemaVersion>::success(SchemaVersion(static_cast<std::uint64_t>(value->asInt())));
}

Result<EvaDependencyKind> parseDependencyKind(const Value& value, std::string path) {
    if (!value.isString())
        return Result<EvaDependencyKind>::failure(Diagnostic::error(
            DiagnosticCode::ParseError, "dependency kind must be a string", std::move(path), {}, "asset.eva.manifest"));
    const std::string& text = value.asString();
    if (text == "runtime-required") return Result<EvaDependencyKind>::success(EvaDependencyKind::RuntimeRequired);
    if (text == "runtime-optional") return Result<EvaDependencyKind>::success(EvaDependencyKind::RuntimeOptional);
    if (text == "build") return Result<EvaDependencyKind>::success(EvaDependencyKind::Build);
    if (text == "editor") return Result<EvaDependencyKind>::success(EvaDependencyKind::Editor);
    if (text == "source") return Result<EvaDependencyKind>::success(EvaDependencyKind::Source);
    if (text == "platform") return Result<EvaDependencyKind>::success(EvaDependencyKind::Platform);
    return Result<EvaDependencyKind>::failure(Diagnostic::error(DiagnosticCode::Unsupported, "unknown dependency kind",
                                                                std::move(path), {}, "asset.eva.manifest"));
}

Value assetRefValue(const AssetRef& reference) { return Value(reference.format()); }

bool canonicalSha256(std::string_view hash) {
    if (hash.size() != 71 || !hash.starts_with("sha256:")) return false;
    return std::all_of(hash.begin() + 7, hash.end(), [](unsigned char character) {
        return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f');
    });
}

bool canonicalExpectedType(std::string_view type) {
    const auto separator = type.rfind('/');
    if (separator == std::string_view::npos || separator == 0 || separator + 1 == type.size()) return false;
    if (type.substr(0, separator).find('/') != std::string_view::npos) return false;
    if (type[separator + 1] == '0') return false;
    return std::all_of(type.begin() + static_cast<std::ptrdiff_t>(separator + 1), type.end(),
                       [](unsigned char value) { return value >= '0' && value <= '9'; });
}

Result<Value::Object> parseFallback(const Value::Object& entry, EvaDependencyKind kind,
                                    const std::string& prefix) {
    const Value* value = required(entry, "fallback");
    if (!value) {
        if (kind == EvaDependencyKind::RuntimeOptional)
            return Result<Value::Object>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "runtime-optional dependency requires an explicit fallback policy",
                prefix + "fallback", {}, "asset.eva.manifest"));
        return Result<Value::Object>::success({});
    }
    const auto* object = value->getIf<Value::Object>();
    const Value* behavior = object ? required(*object, "behavior") : nullptr;
    const Value* observableCode = object ? required(*object, "observableCode") : nullptr;
    if (!object || !behavior || !behavior->isString() ||
        (behavior->asString() != "omit-feature" && behavior->asString() != "use-default" &&
         behavior->asString() != "use-asset") || !observableCode ||
        !observableCode->isString() || observableCode->asString().empty())
        return Result<Value::Object>::failure(Diagnostic::error(
            DiagnosticCode::ParseError, "fallback requires a supported behavior and non-empty observableCode",
            prefix + "fallback", {}, "asset.eva.manifest"));
    const Value* asset = required(*object, "asset");
    if (behavior->asString() == "use-asset") {
        if (!asset)
            return Result<Value::Object>::failure(
                Diagnostic::error(DiagnosticCode::ParseError, "use-asset fallback requires asset",
                                  prefix + "fallback.asset", {}, "asset.eva.manifest"));
        auto parsed = parseAssetRef(*asset, prefix + "fallback.asset");
        if (!parsed) return Result<Value::Object>::failure(parsed.status());
    } else if (asset) {
        return Result<Value::Object>::failure(Diagnostic::error(DiagnosticCode::ParseError,
                                                                "only use-asset fallback may declare asset",
                                                                prefix + "fallback.asset", {}, "asset.eva.manifest"));
    }
    return Result<Value::Object>::success(*object);
}

}  // namespace

std::string_view evaDependencyKindName(EvaDependencyKind kind) noexcept {
    switch (kind) {
        case EvaDependencyKind::RuntimeRequired: return "runtime-required";
        case EvaDependencyKind::RuntimeOptional: return "runtime-optional";
        case EvaDependencyKind::Build: return "build";
        case EvaDependencyKind::Editor: return "editor";
        case EvaDependencyKind::Source: return "source";
        case EvaDependencyKind::Platform: return "platform";
    }
    return "unknown";
}

Result<EvaManifest> parseEvaManifest(std::string_view json) {
    auto parsed = Value::fromJson(json);
    if (!parsed) return Result<EvaManifest>::failure(parsed.status());
    Value root = std::move(parsed).takeValue();
    const auto* object = root.getIf<Value::Object>();
    if (!object)
        return Result<EvaManifest>::failure(Diagnostic::error(
            DiagnosticCode::ParseError, "manifest root must be an object", "$", {}, "asset.eva.manifest"));

    auto schema = parseString(*object, "schema", "$");
    if (!schema) return Result<EvaManifest>::failure(schema.status());
    if (std::move(schema).takeValue() != EvaManifest::kSchema)
        return Result<EvaManifest>::failure(Diagnostic::error(
            DiagnosticCode::Unsupported, "unsupported manifest schema", "$.schema", {}, "asset.eva.manifest"));

    auto envelopeVersion = parseVersion(*object, "schemaVersion", "$");
    if (!envelopeVersion) return Result<EvaManifest>::failure(envelopeVersion.status());
    if (std::move(envelopeVersion).takeValue().value() != EvaManifest::kVersion)
        return Result<EvaManifest>::failure(Diagnostic::error(DiagnosticCode::UnknownVersion,
                                                              "unsupported manifest version", "$.schemaVersion", {},
                                                              "asset.eva.manifest"));

    auto packageName = parseString(*object, "packageName", "$");
    if (!packageName) return Result<EvaManifest>::failure(packageName.status());
    auto packageVersion = parseString(*object, "packageVersion", "$");
    if (!packageVersion) return Result<EvaManifest>::failure(packageVersion.status());
    auto packageIdText = parseString(*object, "packageId", "$");
    if (!packageIdText) return Result<EvaManifest>::failure(packageIdText.status());
    auto packageId = PersistentId::parse(std::move(packageIdText).takeValue());
    if (!packageId || packageId->isNil())
        return Result<EvaManifest>::failure(Diagnostic::error(DiagnosticCode::ParseError,
                                                              "packageId must be a non-nil canonical UUID",
                                                              "$.packageId", {}, "asset.eva.manifest"));

    const Value* unknownPolicy = required(*object, "unknownFields");
    if (!unknownPolicy || !unknownPolicy->isString() || unknownPolicy->asString() != "preserve")
        return Result<EvaManifest>::failure(Diagnostic::error(DiagnosticCode::Unsupported,
                                                              "eva v1 requires unknownFields=preserve",
                                                              "$.unknownFields", {}, "asset.eva.manifest"));

    EvaManifest manifest;
    manifest.packageId      = *packageId;
    manifest.packageName    = std::move(packageName).takeValue();
    manifest.packageVersion = std::move(packageVersion).takeValue();

    const Value* assets = required(*object, "assets");
    const auto* assetArray = assets ? assets->getIf<Value::Array>() : nullptr;
    if (!assetArray)
        return Result<EvaManifest>::failure(Diagnostic::error(DiagnosticCode::ParseError, "assets must be an array",
                                                              "$.assets", {}, "asset.eva.manifest"));
    std::set<PersistentId> identities;
    for (std::size_t index = 0; index < assetArray->size(); ++index) {
        const std::string prefix = "$.assets[" + std::to_string(index) + "].";
        const auto* entry = (*assetArray)[index].getIf<Value::Object>();
        if (!entry)
            return Result<EvaManifest>::failure(
                Diagnostic::error(DiagnosticCode::ParseError, "asset entry must be an object",
                                  prefix.substr(0, prefix.size() - 1), {}, "asset.eva.manifest"));
        const Value* refValue = required(*entry, "asset");
        if (!refValue)
            return Result<EvaManifest>::failure(Diagnostic::error(DiagnosticCode::ParseError, "asset field is required",
                                                                  prefix + "asset", {}, "asset.eva.manifest"));
        auto reference = parseAssetRef(*refValue, prefix + "asset");
        if (!reference) return Result<EvaManifest>::failure(reference.status());
        AssetRef assetRef = std::move(reference).takeValue();
        if (!identities.emplace(assetRef.id()).second)
            return Result<EvaManifest>::failure(Diagnostic::error(DiagnosticCode::Conflict, "duplicate asset identity",
                                                                  prefix + "asset", {}, "asset.eva.manifest"));
        auto type = parseString(*entry, "type", prefix);
        if (!type) return Result<EvaManifest>::failure(type.status());
        auto version = parseVersion(*entry, "schemaVersion", prefix);
        if (!version) return Result<EvaManifest>::failure(version.status());
        auto definition = parseString(*entry, "definition", prefix);
        if (!definition) return Result<EvaManifest>::failure(definition.status());
        auto contentHash = parseString(*entry, "contentHash", prefix);
        if (!contentHash) return Result<EvaManifest>::failure(contentHash.status());
        if (!canonicalSha256(contentHash.value()))
            return Result<EvaManifest>::failure(Diagnostic::error(DiagnosticCode::ParseError,
                                                                  "contentHash must be canonical lowercase sha256",
                                                                  prefix + "contentHash", {}, "asset.eva.manifest"));

        std::vector<std::string> tags;
        if (const Value* tagValue = required(*entry, "tags")) {
            const auto* tagArray = tagValue->getIf<Value::Array>();
            if (!tagArray)
                return Result<EvaManifest>::failure(Diagnostic::error(
                    DiagnosticCode::ParseError, "tags must be an array", prefix + "tags", {}, "asset.eva.manifest"));
            for (std::size_t tagIndex = 0; tagIndex < tagArray->size(); ++tagIndex) {
                if (!(*tagArray)[tagIndex].isString())
                    return Result<EvaManifest>::failure(
                        Diagnostic::error(DiagnosticCode::ParseError, "tag must be a string",
                                          prefix + "tags[" + std::to_string(tagIndex) + "]", {}, "asset.eva.manifest"));
                tags.push_back((*tagArray)[tagIndex].asString());
            }
        }
        manifest.assets.push_back({std::move(assetRef), std::move(type).takeValue(),
                                   std::move(version).takeValue(), std::move(definition).takeValue(),
                                   std::move(contentHash).takeValue(), std::move(tags)});
    }

    const Value* dependencies = required(*object, "dependencies");
    const auto* dependencyArray = dependencies ? dependencies->getIf<Value::Array>() : nullptr;
    if (!dependencyArray)
        return Result<EvaManifest>::failure(Diagnostic::error(
            DiagnosticCode::ParseError, "dependencies must be an array", "$.dependencies", {}, "asset.eva.manifest"));
    for (std::size_t index = 0; index < dependencyArray->size(); ++index) {
        const std::string prefix = "$.dependencies[" + std::to_string(index) + "].";
        const auto* entry = (*dependencyArray)[index].getIf<Value::Object>();
        if (!entry)
            return Result<EvaManifest>::failure(
                Diagnostic::error(DiagnosticCode::ParseError, "dependency must be an object",
                                  prefix.substr(0, prefix.size() - 1), {}, "asset.eva.manifest"));
        const Value* fromValue = required(*entry, "from");
        const Value* toValue   = required(*entry, "to");
        const Value* kindValue = required(*entry, "kind");
        if (!fromValue || !toValue || !kindValue)
            return Result<EvaManifest>::failure(Diagnostic::error(
                DiagnosticCode::ParseError, "dependency requires from, to and kind", prefix, {}, "asset.eva.manifest"));
        auto from = parseAssetRef(*fromValue, prefix + "from");
        if (!from) return Result<EvaManifest>::failure(from.status());
        auto to = parseAssetRef(*toValue, prefix + "to");
        if (!to) return Result<EvaManifest>::failure(to.status());
        auto kind = parseDependencyKind(*kindValue, prefix + "kind");
        if (!kind) return Result<EvaManifest>::failure(kind.status());
        std::string path;
        if (const Value* pathValue = required(*entry, "path")) {
            if (!pathValue->isString())
                return Result<EvaManifest>::failure(Diagnostic::error(DiagnosticCode::ParseError,
                                                                      "dependency path must be a string",
                                                                      prefix + "path", {}, "asset.eva.manifest"));
            path = pathValue->asString();
        }
        Value::Object predicate;
        if (const Value* predicateValue = required(*entry, "predicate")) {
            const auto* predicateObject = predicateValue->getIf<Value::Object>();
            if (!predicateObject)
                return Result<EvaManifest>::failure(Diagnostic::error(DiagnosticCode::ParseError,
                                                                      "dependency predicate must be an object",
                                                                      prefix + "predicate", {}, "asset.eva.manifest"));
            predicate = *predicateObject;
        }
        if (kind.value() == EvaDependencyKind::Platform && predicate.empty())
            return Result<EvaManifest>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                  "platform dependency requires a predicate",
                                                                  prefix + "predicate", {}, "asset.eva.manifest"));
        std::string expectedType;
        if (const Value* expected = required(*entry, "expectedType")) {
            if (!expected->isString() || !canonicalExpectedType(expected->asString()))
                return Result<EvaManifest>::failure(
                    Diagnostic::error(DiagnosticCode::ParseError, "expectedType must be a canonical type/version pair",
                                      prefix + "expectedType", {}, "asset.eva.manifest"));
            expectedType = expected->asString();
        }
        auto fallback = parseFallback(*entry, kind.value(), prefix);
        if (!fallback) return Result<EvaManifest>::failure(fallback.status());
        manifest.dependencies.push_back({std::move(from).takeValue(), std::move(to).takeValue(),
                                         std::move(kind).takeValue(), std::move(path), std::move(predicate),
                                         std::move(expectedType), std::move(fallback).takeValue()});
    }

    if (const Value* entrypoints = required(*object, "entrypoints")) {
        const auto* entries = entrypoints->getIf<Value::Object>();
        if (!entries)
            return Result<EvaManifest>::failure(Diagnostic::error(DiagnosticCode::ParseError,
                                                                  "entrypoints must be an object", "$.entrypoints", {},
                                                                  "asset.eva.manifest"));
        for (const auto& [name, referenceValue] : *entries) {
            auto reference = parseAssetRef(referenceValue, "$.entrypoints." + name);
            if (!reference) return Result<EvaManifest>::failure(reference.status());
            manifest.entrypoints.emplace(name, std::move(reference).takeValue());
        }
    }
    if (const Value* provenance = required(*object, "provenance")) {
        const auto* value = provenance->getIf<Value::Object>();
        if (!value)
            return Result<EvaManifest>::failure(Diagnostic::error(
                DiagnosticCode::ParseError, "provenance must be an object", "$.provenance", {}, "asset.eva.manifest"));
        manifest.provenance = *value;
    }

    static const std::set<std::string> known = {"schema",       "schemaVersion", "packageId",
                                                "packageName",  "packageVersion", "unknownFields",
                                                "assets",       "dependencies",  "entrypoints",
                                                "provenance"};
    for (const auto& [key, value] : *object)
        if (!known.contains(key)) manifest.extensions.emplace(key, value);
    return Result<EvaManifest>::success(std::move(manifest));
}

Result<std::string> serializeEvaManifest(const EvaManifest& manifest) {
    Value::Object root = manifest.extensions;
    root["schema"]         = Value(std::string(EvaManifest::kSchema));
    root["schemaVersion"]  = Value(static_cast<std::int64_t>(EvaManifest::kVersion));
    root["packageId"]      = Value(manifest.packageId.format());
    root["packageName"]    = Value(manifest.packageName);
    root["packageVersion"] = Value(manifest.packageVersion);
    root["unknownFields"]  = Value("preserve");

    Value::Array assets;
    for (const EvaAssetEntry& entry : manifest.assets) {
        Value::Object object;
        object["asset"]         = assetRefValue(entry.asset);
        object["type"]          = Value(entry.type);
        object["schemaVersion"] = Value(static_cast<std::int64_t>(entry.schemaVersion.value()));
        object["definition"]    = Value(entry.definition);
        object["contentHash"]   = Value(entry.contentHash);
        Value::Array tags;
        for (const std::string& tag : entry.tags) tags.emplace_back(tag);
        object["tags"] = Value(std::move(tags));
        assets.emplace_back(std::move(object));
    }
    root["assets"] = Value(std::move(assets));

    Value::Array dependencies;
    for (const EvaDependency& dependency : manifest.dependencies) {
        Value::Object object;
        object["from"] = assetRefValue(dependency.from);
        object["to"]   = assetRefValue(dependency.to);
        object["kind"] = Value(std::string(evaDependencyKindName(dependency.kind)));
        if (!dependency.path.empty()) object["path"] = Value(dependency.path);
        if (!dependency.predicate.empty()) object["predicate"] = Value(dependency.predicate);
        if (!dependency.expectedType.empty()) object["expectedType"] = Value(dependency.expectedType);
        if (!dependency.fallback.empty()) object["fallback"] = Value(dependency.fallback);
        dependencies.emplace_back(std::move(object));
    }
    root["dependencies"] = Value(std::move(dependencies));

    Value::Object entrypoints;
    for (const auto& [name, reference] : manifest.entrypoints) entrypoints[name] = assetRefValue(reference);
    root["entrypoints"] = Value(std::move(entrypoints));
    root["provenance"]  = Value(manifest.provenance);
    return Value(std::move(root)).toJson();
}

}  // namespace eve::asset
