# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Marek Cingel

include(FetchContent)

# Official Lua builds have one feature configuration. Keep this list shared
# with the verified-binary resolver; these are requirements, not user options.
set(_core_features_file "${CMAKE_CURRENT_LIST_DIR}/core-features.json")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_core_features_file}")
file(READ "${_core_features_file}" _core_features_json)
string(JSON _core_features_count LENGTH "${_core_features_json}")
math(EXPR _core_features_last "${_core_features_count} - 1")
set(_core_features "")
foreach(_index RANGE ${_core_features_last})
    string(JSON _feature GET "${_core_features_json}" ${_index})
    list(APPEND _core_features "${_feature}")
    # Replace stale/command-line OFF values and hide Core feature selection
    # from cmake-gui. Installed packages are checked independently below.
    set(${_feature} ON CACHE INTERNAL "Required by the official Lua binding" FORCE)
    set(${_feature} ON)
endforeach()

# Configure source dependencies as libraries only. In particular, enabling the
# old in-Core Lua binding would create a second opentlv_lua target.
set(OPENTLV_BUILD_CXX OFF)
set(OPENTLV_BUILD_LUA OFF)
set(OPENTLV_BUILD_PYTHON OFF)
set(OPENTLV_BUILD_WASM OFF)
set(OPENTLV_BUILD_TESTS OFF)
set(OPENTLV_BUILD_EXAMPLES OFF)
set(OPENTLV_BUILD_DOCS OFF)
set(OPENTLV_BUILD_BENCHMARKS OFF)
set(OPENTLV_BUILD_FUZZING OFF)
option(OPENTLV_BUILD_SHARED_LIBS "Build the source Core dependency as a shared library" OFF)
# Static Core objects must be usable in the Lua shared module on ELF platforms.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# The source override takes precedence over package discovery. Otherwise use an
# installed package, then the exact released Core baseline; never a moving branch.
# Consumers of a versioned rockspec cannot preconfigure a Core selection, so an
# unrelated installed OpenTLV package must not replace the pin. Declare reads
# this mode; setting it later has no effect.
if(NOT "${OPENTLV_LUA_RELEASE_TAG}" STREQUAL "")
    set(FETCHCONTENT_TRY_FIND_PACKAGE_MODE NEVER)
endif()
FetchContent_Declare(opentlv_core
    GIT_REPOSITORY https://github.com/OpenTLV/opentlv.git
    GIT_TAG 345c77acb29266036df5f27d04ce315bb61853cc
    FIND_PACKAGE_ARGS CONFIG NAMES OpenTLV
)
set(OPENTLV_LUA_CORE_PACKAGE "" CACHE PATH "Explicit Core package directory (no source fallback)")
if(OPENTLV_LUA_CORE_PACKAGE)
    if(FETCHCONTENT_SOURCE_DIR_OPENTLV_CORE)
        message(FATAL_ERROR "An explicit Core package cannot be combined with a source override")
    endif()
    if(NOT EXISTS "${OPENTLV_LUA_CORE_PACKAGE}/OpenTLVConfig.cmake")
        message(FATAL_ERROR "Explicit Core package must contain OpenTLVConfig.cmake")
    endif()
    set(OpenTLV_DIR "${OPENTLV_LUA_CORE_PACKAGE}" CACHE PATH "Selected Core" FORCE)
    find_package(OpenTLV CONFIG REQUIRED PATHS "${OPENTLV_LUA_CORE_PACKAGE}" NO_DEFAULT_PATH)
    file(REAL_PATH "${OpenTLV_DIR}" _selected_package)
    file(REAL_PATH "${OPENTLV_LUA_CORE_PACKAGE}" _requested_package)
    if(NOT _selected_package STREQUAL _requested_package)
        message(FATAL_ERROR "Explicit Core package was shadowed")
    endif()
else()
    FetchContent_MakeAvailable(opentlv_core)
endif()
if(NOT TARGET OpenTLV::tlv)
    message(FATAL_ERROR
        "Selected Core does not provide OpenTLV::tlv. Use Core with the public "
        "CMake integration from Core #620 via FETCHCONTENT_SOURCE_DIR_OPENTLV_CORE "
        "or CMAKE_PREFIX_PATH.")
endif()

# Read only the public generated header exposed by the selected target. Package
# consumers do not inherit Core's CMake cache, and source options may have been
# normalized by Core. Never infer installed capabilities from consumer options.
get_target_property(_core_includes OpenTLV::tlv INTERFACE_INCLUDE_DIRECTORIES)
set(_core_config "")
foreach(_include IN LISTS _core_includes)
    if(_include MATCHES "^\\$<BUILD_INTERFACE:(.*)>$")
        set(_include "${CMAKE_MATCH_1}")
    elseif(_include MATCHES "\\$<")
        continue()
    endif()
    if(EXISTS "${_include}/tlv/config.h")
        set(_core_config "${_include}/tlv/config.h")
        break()
    endif()
endforeach()
if(NOT _core_config)
    message(FATAL_ERROR "Selected Core does not expose its public generated tlv/config.h.")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_core_config}")
file(STRINGS "${_core_config}" _core_definitions REGEX "^#define OPENTLV_[A-Z0-9_]+ [01]$")
foreach(_feature IN LISTS _core_features)
    set(_definition "${_core_definitions}")
    list(FILTER _definition INCLUDE REGEX "^#define ${_feature} [01]$")
    list(LENGTH _definition _count)
    if(NOT _count EQUAL 1)
        message(FATAL_ERROR "Selected Core config is missing a unique ${_feature} definition.")
    endif()
    if(NOT _definition STREQUAL "#define ${_feature} 1")
        message(FATAL_ERROR
            "The official Lua binding requires ${_feature}=ON in the selected Core. "
            "Select a full-featured Core package or build Core from source.")
    endif()
endforeach()
message(STATUS "Lua Core configuration: ${_core_config}")

# The rock installs rock-doc into its documentation directory: the module links
# Core, so it carries Core's license as well as Lua's. Installed Core packages
# keep it in <prefix>/share/doc/OpenTLV beside <libdir>/cmake/OpenTLV.
set(_core_license "")
if(opentlv_core_SOURCE_DIR)
    set(_core_license "${opentlv_core_SOURCE_DIR}/LICENSE")
else()
    # FetchContent's package discovery searches under the dependency name.
    set(_core_package "${opentlv_core_DIR}")
    if(OPENTLV_LUA_CORE_PACKAGE)
        set(_core_package "${OpenTLV_DIR}")
    endif()
    foreach(_parent IN ITEMS ../../.. ../../../..)
        if(NOT _core_license AND EXISTS "${_core_package}/${_parent}/share/doc/OpenTLV/LICENSE")
            set(_core_license "${_core_package}/${_parent}/share/doc/OpenTLV/LICENSE")
        endif()
    endforeach()
endif()
file(REMOVE_RECURSE "${CMAKE_CURRENT_BINARY_DIR}/rock-doc")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/rock-doc")
if(_core_license AND EXISTS "${_core_license}")
    file(COPY_FILE "${_core_license}" "${CMAKE_CURRENT_BINARY_DIR}/rock-doc/LICENSE-Core")
elseif(NOT "${OPENTLV_LUA_RELEASE_TAG}" STREQUAL "")
    message(FATAL_ERROR "The selected Core has no LICENSE for the release rock.")
else()
    message(WARNING "The selected Core's LICENSE was not found; a rock omits LICENSE-Core.")
endif()
