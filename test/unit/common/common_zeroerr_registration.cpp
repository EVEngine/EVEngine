#include <zeroerr/unittest.h>
#if defined(ZEROERR_COMPILED_DIAGNOSTICS)
namespace {
void testCallback(zeroerr::TestContext*) {}
}  // namespace
TEST_CASE("common.zeroerrRegistration.borrowedDecoratorsKeepOrder") {
    zeroerr::Decorator first, second;
    auto metadata = zeroerr::detail::makeTestCaseFromDecorators("probe", "source", 37, testCallback, {&first, &second});
    CHECK(metadata.decorators.size() == 2u);
    CHECK(metadata.decorators.at(0) == &first);
    CHECK(metadata.decorators.at(1) == &second);
    CHECK(metadata.name == "probe");
    CHECK(metadata.file == "source");
    CHECK(metadata.line == 37u);
    CHECK(static_cast<bool>(metadata.func));
    auto copied = metadata;
    metadata.decorators.clear();
    CHECK(copied.decorators.size() == 2u);
    CHECK(copied.decorators.at(0) == &first);
}
TEST_CASE("common.zeroerrRegistration.borrowedNamesBecomeOwned") {
    std::string name("name\0suffix", 11);
    std::string file("file\0suffix", 11);
    auto        metadata = zeroerr::detail::makeTestCaseFromDecorators(name, file, 91, testCallback, {});
    name.assign("changed");
    file.assign("changed");
    CHECK(metadata.name.size() == 11u);
    CHECK(metadata.file.size() == 11u);
    CHECK(metadata.name.substr(5) == "suffix");
    CHECK(metadata.file.substr(5) == "suffix");
    auto transient = zeroerr::detail::makeTestCaseFromDecorators(std::string(4096, 'n'), std::string(4096, 'f'), 92,
                                                                 testCallback, {});
    CHECK(transient.name.size() == 4096u);
    CHECK(transient.file.size() == 4096u);
    CHECK(transient.name.back() == 'n');
    CHECK(transient.file.back() == 'f');
    CHECK(transient.decorators.empty());
}
TEST_CASE("common.zeroerrRegistration.skippedCase", zeroerr::skip()) { CHECK(false); }
#endif

struct ZeroerrRegistrationFixture {
    int state = 73;
};
TEST_CASE_FIXTURE(ZeroerrRegistrationFixture, "common.zeroerrRegistration.fixtureFactory") { CHECK(state == 73); }
