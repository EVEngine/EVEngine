# The existing native macOS SDK profiles require 13.3 for libc++ to_chars.
# Apply that same baseline to direct and legacy desktop configurations once
# project() has resolved the target system. iOS and other cross targets keep
# their own policies; an explicit desktop deployment target remains intact.
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin" AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "13.3" CACHE STRING
        "Minimum macOS version for engine and dependency binaries" FORCE)
endif()
