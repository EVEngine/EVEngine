#include "asset/import/ImportCommon.h"
#include "asset/import/UnityImporter.h"
#include "asset/import/UnitySourceInternal.h"

#include <cctype>

namespace eve::asset_import {
namespace {

std::string_view trim(std::string_view value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) value.remove_prefix(1);
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.remove_suffix(1);
    return value;
}

Value::Array words(std::string_view value) {
    Value::Array result;
    while (!value.empty()) {
        const auto split = value.find_first_of(" \t");
        result.emplace_back(std::string(value.substr(0, split)));
        if (split == std::string_view::npos) break;
        value.remove_prefix(split);
        value = trim(value);
    }
    return result;
}

struct Parser {
    const UnitySourceAsset&       source;
    const AssetImportLimits&      limits;
    std::vector<std::string_view> lines;
    std::size_t                   at                      = 0;
    std::uint64_t                 statements              = 0;
    std::uint32_t                 depth                   = 0;
    std::uint32_t                 recoveredConditionTypos = 0;

    Result<Value::Array> block(bool nested) {
        if (++depth > 64)
            return Result<Value::Array>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                   "TVE preset condition nesting exceeds budget",
                                                                   source.path, {}, "asset.import.unity"));
        Value::Array result;
        while (at < lines.size()) {
            auto line = trim(lines[at++]);
            if (line.empty() || line.starts_with("//") || line.front() == '*') continue;
            if (line == "}") {
                --depth;
                if (!nested)
                    return Result<Value::Array>::failure(Diagnostic::error(DiagnosticCode::ParseError,
                                                                           "unexpected TVE preset closing brace",
                                                                           source.path, {}, "asset.import.unity"));
                return Result<Value::Array>::success(std::move(result));
            }
            if (++statements > limits.maximumAssets * 64ull)
                return Result<Value::Array>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                       "TVE preset statement count exceeds budget",
                                                                       source.path, {}, "asset.import.unity"));
            if (line.starts_with("if ") || line.starts_with("f ")) {
                const bool recoveredTypo = line.starts_with("f ");
                if (recoveredTypo) ++recoveredConditionTypos;
                auto tokens = words(trim(line.substr(recoveredTypo ? 2 : 3)));
                if (tokens.empty() || at >= lines.size() || trim(lines[at++]) != "{")
                    return Result<Value::Array>::failure(
                        Diagnostic::error(DiagnosticCode::ParseError, "TVE preset condition requires a following block",
                                          source.path, {}, "asset.import.unity"));
                auto body = block(true);
                if (!body) return body;
                auto       predicate = tokens.front().asString();
                const bool negated   = !predicate.empty() && predicate.front() == '!';
                if (negated) predicate.erase(predicate.begin());
                tokens.erase(tokens.begin());
                result.emplace_back(Value::Object{{"kind", Value("condition")},
                                                  {"predicate", Value(predicate)},
                                                  {"negated", Value(negated)},
                                                  {"arguments", Value(std::move(tokens))},
                                                  {"statements", Value(std::move(body).takeValue())}});
                continue;
            }
            if (line == "{")
                return Result<Value::Array>::failure(Diagnostic::error(DiagnosticCode::ParseError,
                                                                       "unexpected TVE preset opening brace",
                                                                       source.path, {}, "asset.import.unity"));
            const auto split          = line.find_first_of(" \t");
            const auto domain         = line.substr(0, split);
            auto       remainder      = split == std::string_view::npos ? std::string_view{} : trim(line.substr(split));
            const auto operationSplit = remainder.find_first_of(" \t");
            const auto operation      = remainder.substr(0, operationSplit);
            auto       arguments      = operationSplit == std::string_view::npos ? Value::Array{}
                                                                                 : words(trim(remainder.substr(operationSplit)));
            result.emplace_back(Value::Object{{"kind", Value("command")},
                                              {"domain", Value(std::string(domain))},
                                              {"operation", Value(std::string(operation))},
                                              {"arguments", Value(std::move(arguments))}});
        }
        --depth;
        if (nested)
            return Result<Value::Array>::failure(Diagnostic::error(DiagnosticCode::ParseError,
                                                                   "unterminated TVE preset condition block",
                                                                   source.path, {}, "asset.import.unity"));
        return Result<Value::Array>::success(std::move(result));
    }
};

}  // namespace

Result<PreparedAssetImport> prepareUnityVegetationPreset(const UnityProjectImportRequest& request,
                                                         const UnitySourceAsset&          source) {
    const auto&            bytes = request.files.at(source.path);
    const std::string_view raw(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    if (bytes.empty() || bytes.size() > request.limits.maximumSourceBytes || !isValidUtf8(raw, Utf8NullPolicy::Reject))
        return Result<PreparedAssetImport>::failure(Diagnostic::error(DiagnosticCode::ParseError,
                                                                      "TVE preset must be bounded UTF-8 text",
                                                                      source.path, {}, "asset.import.unity"));
    std::string_view text = raw;
    if (text.starts_with("\xef\xbb\xbf")) text.remove_prefix(3);
    Parser parser{source, request.limits};
    for (std::size_t begin = 0; begin <= text.size();) {
        auto end = text.find('\n', begin);
        if (end == std::string_view::npos) end = text.size();
        auto line = text.substr(begin, end - begin);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.size() > request.limits.maximumStringBytes)
            return Result<PreparedAssetImport>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                          "TVE preset line exceeds string budget",
                                                                          source.path, {}, "asset.import.unity"));
        parser.lines.push_back(line);
        if (end == text.size()) break;
        begin = end + 1;
    }
    auto statements = parser.block(false);
    if (!statements) return Result<PreparedAssetImport>::failure(statements.status());
    auto manifest = detail::baseManifest(request.package, "eve.unity-tve-preset/1");
    if (!manifest) return Result<PreparedAssetImport>::failure(manifest.status());
    const auto id        = request.package.packageId.child("unity:" + source.guid).child("vegetation-preset:default");
    auto       reference = detail::assetRef(id);
    if (!reference) return Result<PreparedAssetImport>::failure(reference.status());
    Value definition(Value::Object{{"schema", Value("eve.vegetation-conversion-preset")},
                                   {"schemaVersion", Value(std::int64_t(1))},
                                   {"sourcePath", Value(source.path)},
                                   {"statements", Value(std::move(statements).takeValue())}});
    auto  json = definition.toJson();
    if (!json) return Result<PreparedAssetImport>::failure(json.status());
    std::vector<std::uint8_t> encoded(json.value().begin(), json.value().end());
    const std::string         path = "assets/" + id.format() + "/asset.json";
    PreparedAssetImport       output;
    output.manifest = std::move(manifest).takeValue();
    output.manifest.assets.push_back({reference.value(),
                                      "eve.vegetation-conversion-preset",
                                      SchemaVersion(1),
                                      path,
                                      detail::sha256(encoded),
                                      {"vegetation", "conversion-preset", "source:unity"}});
    output.manifest.entrypoints.emplace("default", reference.value());
    output.entries.push_back({path, std::move(encoded)});
    output.sourceMappings.push_back({"preset", reference.value()});
    output.findings.push_back({source.path, "TVE.preset.syntax", ImportDisposition::Translated,
                               "commands, include directives and conditional blocks parsed into a versioned tree"});
    if (parser.recoveredConditionTypos)
        output.findings.push_back({source.path, "TVE.preset.conditionTypo", ImportDisposition::Translated,
                                   "source 'f' condition typo interpreted as 'if' without changing its predicate"});
    return Result<PreparedAssetImport>::success(std::move(output));
}

}  // namespace eve::asset_import
