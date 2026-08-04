#include "zeroerr/unittest.h"

int main(int argc, const char** argv) {
    return zeroerr::UnitTest().parseArgs(argc, argv).run();
}
