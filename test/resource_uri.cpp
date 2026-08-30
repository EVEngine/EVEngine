#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/ResourceRef.h"
#include "filesystem/ResourceReader.h"

#include <map>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

class MemoryFileSystem final : public eve::service::IFileSystem {
public:
    bool readFile(const std::string& path, std::vector<std::uint8_t>& out) override {
        lastPath         = path;
        const auto found = files.find(path);
        if (found == files.end()) return false;
        out = found->second;
        return true;
    }

    bool writeFile(const std::string&, const void*, size_t) override { return false; }

    bool fileExists(const std::string& path) override { return files.find(path) != files.end(); }

    std::map<std::string, std::vector<std::uint8_t>> files;
    std::string                                      lastPath;
};

template <typename T>
T take(eve::Result<T> result) {
    REQUIRE(result.ok());
    return std::move(result).takeValue();
}

}  // namespace

TEST_CASE("resourceUri.acceptsOnlyKnownVirtualSchemes") {
    for (const auto text :
         {"asset://01020304-0506-0708-090a-0b0c0d0e0f10", "project://textures/wood.png?quality=high#albedo",
          "builtin://ui/default-font", "generated://terrain/seed-1", "memory://session/preview"}) {
        auto uri = eve::ResourceUri::parse(text);
        REQUIRE(uri.ok());
        CHECK(!uri.value().format().empty());
    }

    CHECK(!eve::ResourceUri::parse("textures/wood.png").ok());
    CHECK(!eve::ResourceUri::parse("file://textures/wood.png").ok());
    CHECK(!eve::ResourceUri::parse("http://example.com/a").ok());
    CHECK(!eve::ResourceUri::parse("project:///absolute").ok());
    CHECK(!eve::ResourceUri::parse("project://../escape").ok());
    CHECK(!eve::ResourceUri::parse("project://a\\b").ok());
}

TEST_CASE("resourceUri.canonicalizesSchemeAndLegacyPathExplicitly") {
    const auto uri = take(eve::ResourceUri::parse("PROJECT://textures/wood.png"));
    CHECK_EQ(uri.format(), std::string("project://textures/wood.png"));

    const auto legacy = take(eve::ResourceUri::fromLegacyProjectPath("./textures\\wood.png"));
    CHECK_EQ(legacy.format(), std::string("project://textures/wood.png"));
    CHECK(!eve::ResourceUri::fromLegacyProjectPath("file://textures/wood.png").ok());
}

TEST_CASE("resourceRef.keepsIdentityDomainsDistinct") {
    static_assert(!std::is_convertible_v<eve::AssetRef, eve::ObjectRef>);
    static_assert(!std::is_convertible_v<eve::AssetRef, eve::ResourceUri>);
    static_assert(!std::is_convertible_v<eve::DefinitionRef, eve::ResourceUri>);

    const auto asset = take(eve::AssetRef::parse("asset://01020304-0506-0708-090a-0b0c0d0e0f10"));
    CHECK_EQ(asset.format(), std::string("asset://01020304-0506-0708-090a-0b0c0d0e0f10"));
    CHECK(!eve::AssetRef::parse("asset://textures/wood.png").ok());
    CHECK(!eve::AssetRef::parse("project://textures/wood.png").ok());

    const auto definition = take(eve::DefinitionRef::parse("rpg:skill.fire"));
    CHECK_EQ(definition.format(), std::string("rpg:skill.fire"));
    CHECK(!eve::DefinitionRef::parse("rpg/skill.fire").ok());

    const auto object = take(eve::ObjectRef::parse("11111111-2222-3333-4444-555555555555"));
    CHECK_EQ(object.format(), std::string("11111111-2222-3333-4444-555555555555"));

    const auto genericAsset = take(eve::ResourceRef::parse(asset.format()));
    CHECK(genericAsset.asset() != nullptr);
    CHECK(genericAsset.uri() == nullptr);
    CHECK_EQ(genericAsset.kind(), eve::ResourceRefKind::Asset);

    const auto genericUri = take(eve::ResourceRef::parse("project://textures/wood.png"));
    CHECK(genericUri.asset() == nullptr);
    CHECK(genericUri.uri() != nullptr);
    CHECK_EQ(genericUri.kind(), eve::ResourceRefKind::Uri);
}

TEST_CASE("resourceReader.typedUriIsSourceOfTruth") {
    MemoryFileSystem filesystem;
    filesystem.files["textures/wood.bin"] = {1, 2, 3};
    eve::filesystem::ResourceReader reader(filesystem);

    const auto uri   = take(eve::ResourceUri::parse("project://textures/wood.bin"));
    const auto bytes = take(reader.read(uri));
    CHECK_EQ(bytes, std::vector<std::uint8_t>({1, 2, 3}));
    CHECK_EQ(filesystem.lastPath, std::string("textures/wood.bin"));

    const auto legacyBytes = take(reader.read("./textures/wood.bin"));
    CHECK_EQ(legacyBytes, std::vector<std::uint8_t>({1, 2, 3}));
    CHECK(!reader.read("file://textures/wood.bin").ok());
}

TEST_CASE("resourceReader.doesNotSilentlyFallbackAcrossNamespaces") {
    MemoryFileSystem filesystem;
    filesystem.files["textures/wood.bin"] = {1};
    eve::filesystem::ResourceReader reader(filesystem);

    const auto builtin = take(eve::ResourceUri::parse("builtin://textures/wood.bin"));
    auto       result  = reader.read(builtin);
    CHECK(!result.ok());
    CHECK_EQ(result.code(), eve::StatusCode::Unsupported);
    CHECK(filesystem.lastPath.empty());

    auto missing = reader.read("project://missing.bin");
    CHECK(!missing.ok());
    CHECK_EQ(missing.code(), eve::StatusCode::Failed);
}
