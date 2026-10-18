# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Marek Cingel

include(FetchContent)

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
# installed package, then the exact migration baseline; never a moving branch.
FetchContent_Declare(opentlv_core
    GIT_REPOSITORY https://github.com/OpenTLV/opentlv.git
    GIT_TAG d87dcf96b2707e817dd51163a9f9efa36651e929
    FIND_PACKAGE_ARGS CONFIG NAMES OpenTLV
)
FetchContent_MakeAvailable(opentlv_core)
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
file(STRINGS "${_core_config}" _core_definitions REGEX "^#define OPENTLV_[A-Z_]+ [01]$")
foreach(_feature IN ITEMS READER WRITER QUERY SCHEMA CODEC DOCUMENT
        FORMAT_BER FORMAT_CER FORMAT_DER BLUETOOTH EMV NFC LLDP)
    set(_definition "${_core_definitions}")
    list(FILTER _definition INCLUDE REGEX "^#define OPENTLV_${_feature} [01]$")
    list(LENGTH _definition _count)
    if(NOT _count EQUAL 1)
        message(FATAL_ERROR "Selected Core config is missing a unique OPENTLV_${_feature} definition.")
    endif()
    string(REGEX REPLACE ".* ([01])$" "\\1" OPENTLV_LUA_CORE_${_feature} "${_definition}")
endforeach()
foreach(_required IN ITEMS READER WRITER QUERY SCHEMA CODEC)
    if(NOT OPENTLV_LUA_CORE_${_required})
        message(FATAL_ERROR "The existing Lua API requires Core capability OPENTLV_${_required}=ON.")
    endif()
endforeach()
message(STATUS "Lua Core configuration: ${_core_config}")
