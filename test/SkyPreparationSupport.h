#pragma once
#include <cstdio>
#include "common/Result.h"
#include "zeroerr/unittest.h"

// Resource programs retain the standard mesh descriptor set. A device may support
// the two atmosphere images but not the seven cloud/celestial images in addition
// to that set. Exercise the documented, atomic Unsupported outcome on such devices;
// parse errors, shader failures and other Unsupported diagnoses must still fail.
// Coverage tracking: owner=graphics; issue=https://github.com/EVEngine/EVEngine/pull/509;
// reason=the standard mesh set consumes 13 sampler slots before cloud resources;
// removal=run the cloud pixel assertions on these devices once the resource ABI
// no longer reserves the unused standard-material samplers.
template <class T>
bool skyPreparationAvailable(const eve::Result<T>& result) {
    if (result.ok()) return true;
    for (const auto& diagnostic : result.diagnostics())
        std::fprintf(stderr, "Sky preparation: %s\n", diagnostic.message().c_str());
    REQUIRE(result.diagnostics().size() == 1);
    REQUIRE(result.diagnostics().front().code() == eve::DiagnosticCode::Unsupported);
    REQUIRE(result.diagnostics().front().message() == "Resource program exceeds device descriptor or uniform limits");
    return false;
}
