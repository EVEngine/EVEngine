add_executable(render_benchmark main.cpp)

eve_append_system_libraries(_render_bench_sys)
eve_thirdparty_libs(_render_bench_tp ${EVE_THIRDPARTY_GROUPS})
set(_render_bench_libs EVScripts ${EVE_MODULE_LIBS})
if(TARGET eve_imgui)
    list(APPEND _render_bench_libs eve_imgui)
endif()
if(EVENGINE_BUILD_DEMO)
    list(APPEND _render_bench_libs EVDemo)
endif()
if(EVENGINE_PLATFORM_LIB)
    list(APPEND _render_bench_libs ${EVENGINE_PLATFORM_LIB})
endif()

target_link_libraries(render_benchmark PRIVATE
    eve_engine_includes
    ${_render_bench_libs}
    zeroerr
    ${_render_bench_tp}
    ${_render_bench_sys}
    ${EVENGINE_VULKAN_LIB}
    ${EVENGINE_WEBGPU_LIB})

eve_module_enabled(particles _render_bench_particles)
if(_render_bench_particles)
    target_compile_definitions(render_benchmark PRIVATE EVENGINE_BENCH_PARTICLES=1)
endif()

if(WIN32)
    target_compile_definitions(render_benchmark PRIVATE
        POCO_STATIC POCO_NO_AUTOMATIC_LIBS NOMINMAX)
    target_link_libraries(render_benchmark PRIVATE EVBacktrace)
endif()
