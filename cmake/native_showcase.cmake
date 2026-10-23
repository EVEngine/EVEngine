# Native example consumers use the same composed module/dependency closure as
# the host. Keep these tools opt-in so normal builds do not compile galleries.
function(eve_add_native_showcase target source)
    add_executable(${target} EXCLUDE_FROM_ALL ${source})
    eve_append_system_libraries(_showcase_system)
    eve_thirdparty_libs(_showcase_thirdparty ${EVE_THIRDPARTY_GROUPS})
    set(_showcase_modules EVScripts ${EVE_LINK_TARGETS})
    if(TARGET eve_imgui)
        list(APPEND _showcase_modules eve_imgui)
    endif()
    if(EVENGINE_BUILD_DEMO)
        list(APPEND _showcase_modules EVDemo)
    endif()
    if(EVENGINE_PLATFORM_LIB)
        list(APPEND _showcase_modules ${EVENGINE_PLATFORM_LIB})
    endif()
    target_link_libraries(${target} PRIVATE eve_engine_includes
        ${_showcase_modules} zeroerr ${_showcase_thirdparty} ${_showcase_system}
        ${EVENGINE_VULKAN_LIB} ${EVENGINE_WEBGPU_LIB})
    if(WIN32)
        target_compile_definitions(${target} PRIVATE POCO_STATIC POCO_NO_AUTOMATIC_LIBS NOMINMAX)
    endif()
    set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
endfunction()
