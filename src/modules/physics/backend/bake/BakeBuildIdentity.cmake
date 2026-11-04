# Producer-cache identity is build metadata, not a second module manifest.
# Source edits (including uncommitted edits) invalidate the next configured bake
# producer. No wall time, checkout path or graphics-device identity enters it.
function(eve_configure_collapse_bake_identity output_directory)
  set(_root "${CMAKE_SOURCE_DIR}")
  set(_inputs)
  foreach(_tree src/engine src/scripts src/modules/physics src/modules/particles src/modules/asset src/modules/schema)
    file(GLOB_RECURSE _tree_inputs CONFIGURE_DEPENDS
      "${_root}/${_tree}/*.cpp" "${_root}/${_tree}/*.h" "${_root}/${_tree}/*.hpp"
      "${_root}/${_tree}/*.nut" "${_root}/${_tree}/*.cmake" "${_root}/${_tree}/*.in")
    list(APPEND _inputs ${_tree_inputs})
  endforeach()
  file(GLOB_RECURSE _patches CONFIGURE_DEPENDS
    "${_root}/third-party/cmake/*.patch" "${_root}/third-party/cmake/*.cmake"
    "${_root}/cmake/*.cmake")
  list(APPEND _inputs ${_patches} "${_root}/cmake/third_party_pin.cmake" "${_root}/CMakeLists.txt")
  list(REMOVE_DUPLICATES _inputs)
  list(SORT _inputs)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_inputs})
  set(_identity "eve-collapse-producer-1\n")
  foreach(_input IN LISTS _inputs)
    file(RELATIVE_PATH _relative "${_root}" "${_input}")
    file(SHA256 "${_input}" _digest)
    string(APPEND _identity "${_relative}:${_digest}\n")
  endforeach()
  # Compiler/ABI/optimization affect the producing solver, not a playback-only
  # consumer. Explicit asset loads remain portable and do not require this key.
  string(TOUPPER "${CMAKE_BUILD_TYPE}" _configuration)
  foreach(_variable CMAKE_CXX_COMPILER_ID CMAKE_CXX_COMPILER_VERSION CMAKE_SYSTEM_NAME
      CMAKE_SYSTEM_PROCESSOR CMAKE_SIZEOF_VOID_P CMAKE_BUILD_TYPE CMAKE_CXX_FLAGS
      CMAKE_CXX_FLAGS_${_configuration} EVENGINE_THIRD_PARTY_PIN EVENGINE_TP_DESC
      EVENGINE_BUILD_JOLT EVENGINE_ENABLE_ASSERTS EVENGINE_MODULE_LINKAGE)
    string(APPEND _identity "${_variable}=${${_variable}}\n")
  endforeach()
  string(SHA256 _producer "${_identity}")
  string(SHA256 _backend "box3d:${EVENGINE_THIRD_PARTY_PIN}:${EVENGINE_TP_DESC}")
  set(_output "${output_directory}")
  file(MAKE_DIRECTORY "${_output}")
  file(CONFIGURE OUTPUT "${_output}/CollapseBakeBuildIdentity.h" CONTENT
    "#pragma once\n#define EVENGINE_COLLAPSE_PRODUCER_REVISION \"${_producer}\"\n#define EVENGINE_COLLAPSE_BOX3D_REVISION \"box3d:${_backend}\"\n" @ONLY)
endfunction()
