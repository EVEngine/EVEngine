#include "zeroerr/unittest.h"

int main(int argc, char** argv) {
    zeroerr::UnitTest test;
    test.parseArgs(argc, const_cast<const char**>(argv));
    return test.run();
}
