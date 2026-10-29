# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Marek Cingel

# This script also runs at build time so switching tags or editing sources does
# not leave configure-time version metadata in an installed rock.
cmake_minimum_required(VERSION 3.24)
set(_version "scm+unknown")
set(_revision "")
set(_tag "")
set(_dirty "false")
set(_release_pattern "^v(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)$")
# A versioned rockspec declares its release tag because LuaRocks deletes .git
# from the tag it clones. It identifies only a source tree without Git; a Git
# checkout keeps reporting its own state and must be at that same tag.
if(NOT "${OPENTLV_LUA_RELEASE_TAG}" STREQUAL "" AND NOT OPENTLV_LUA_RELEASE_TAG MATCHES "${_release_pattern}")
    message(FATAL_ERROR "OPENTLV_LUA_RELEASE_TAG must be a vMAJOR.MINOR.PATCH Lua release tag")
endif()
execute_process(COMMAND git rev-parse --show-toplevel
    WORKING_DIRECTORY "${LUA_SOURCE}" OUTPUT_VARIABLE _root
    OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
file(REAL_PATH "${LUA_SOURCE}" _source)
if(_root)
    file(REAL_PATH "${_root}" _root)
endif()
# An archive nested inside another repository must never inherit its tags.
if(_root STREQUAL _source)
    execute_process(COMMAND git rev-parse HEAD WORKING_DIRECTORY "${LUA_SOURCE}"
        OUTPUT_VARIABLE _revision RESULT_VARIABLE _revision_result
        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    execute_process(COMMAND git status --porcelain WORKING_DIRECTORY "${LUA_SOURCE}"
        OUTPUT_VARIABLE _status RESULT_VARIABLE _status_result
        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    execute_process(COMMAND git tag --points-at HEAD WORKING_DIRECTORY "${LUA_SOURCE}"
        OUTPUT_VARIABLE _tags RESULT_VARIABLE _tags_result
        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(NOT _revision_result EQUAL 0 OR NOT _status_result EQUAL 0 OR NOT _tags_result EQUAL 0)
        message(FATAL_ERROR "Cannot read Lua repository version metadata")
    endif()
    string(REPLACE "\n" ";" _tags "${_tags}")
    foreach(_candidate IN LISTS _tags)
        # Release tags include v; the binding version below is plain SemVer.
        if(_candidate MATCHES "${_release_pattern}")
            if(_tag)
                message(FATAL_ERROR "Multiple Lua release tags point at HEAD")
            endif()
            set(_tag "${_candidate}")
        endif()
    endforeach()
    string(SUBSTRING "${_revision}" 0 12 _short)
    if(_tag)
        string(SUBSTRING "${_tag}" 1 -1 _version)
    else()
        set(_version "scm+g${_short}")
    endif()
    if(_status)
        set(_dirty "true")
        if(_tag)
            string(APPEND _version "+g${_short}")
        endif()
        string(APPEND _version ".dirty")
    endif()
    if(NOT "${OPENTLV_LUA_RELEASE_TAG}" STREQUAL "" AND NOT _tag STREQUAL OPENTLV_LUA_RELEASE_TAG)
        message(FATAL_ERROR "The rockspec builds Lua release ${OPENTLV_LUA_RELEASE_TAG}, "
            "but the Lua checkout reports ${_version}")
    endif()
elseif(NOT "${OPENTLV_LUA_RELEASE_TAG}" STREQUAL "")
    # Declared, not verified: no revision or worktree status is available.
    set(_tag "${OPENTLV_LUA_RELEASE_TAG}")
    string(SUBSTRING "${_tag}" 1 -1 _version)
endif()
configure_file("${CMAKE_CURRENT_LIST_DIR}/lua-version.json.in" "${LUA_BINARY}/lua-version.json" @ONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/lua-version.h.in" "${LUA_BINARY}/generated/lua-version.h" @ONLY)
