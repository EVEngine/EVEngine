# Windows crash reporting is shared by the host and standalone profile consumers.
if(NOT WIN32 OR TARGET EVBacktrace)
    return()
endif()
add_library(EVBacktrace INTERFACE)
target_link_libraries(EVBacktrace INTERFACE eve_engine_includes)
if(EVENGINE_THIRD_PARTY_BINARY_DIR)
    target_include_directories(EVBacktrace INTERFACE ${EVENGINE_THIRD_PARTY_BINARY_DIR}/include)
    target_link_libraries(EVBacktrace INTERFACE
        ${EVENGINE_THIRD_PARTY_BINARY_DIR}/lib/backward$<$<CONFIG:Debug>:mdd>$<$<NOT:$<CONFIG:Debug>>:md>.lib)
else()
    target_link_libraries(EVBacktrace INTERFACE backward$<$<CONFIG:Debug>:mdd>$<$<NOT:$<CONFIG:Debug>>:md>)
endif()
