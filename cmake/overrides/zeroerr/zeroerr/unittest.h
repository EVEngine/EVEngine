#pragma once

// Keep the upstream header as the implementation, then replace only its
// fixture-registration macro. Upstream generates external class/method names
// solely from __COUNTER__, which restarts in every translation unit. Release
// LTO can therefore merge unrelated fixture methods and run them against the
// wrong fixture layout. Including the fixture token makes all EVEngine fixture
// types unique across its test translation units.
#include "../../../../external/zeroerr/include/zeroerr/unittest.h"

#undef TEST_CASE_FIXTURE
#define TEST_CASE_FIXTURE(fixture, ...)                                              \
    ZEROERR_EXPAND(ZEROERR_CREATE_TEST_CLASS(                                         \
        fixture, ZEROERR_NAMEGEN(ZEROERR_CAT(fixture, _zeroerr_class)),               \
        ZEROERR_NAMEGEN(ZEROERR_CAT(fixture, _zeroerr_test_method)), __VA_ARGS__))
