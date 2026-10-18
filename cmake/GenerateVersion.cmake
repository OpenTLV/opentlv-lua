# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Marek Cingel

# This script also runs at build time so switching tags or editing sources does
# not leave configure-time version metadata in an installed rock.
cmake_minimum_required(VERSION 3.24)
set(_version "scm+unknown")
set(_revision "")
set(_tag "")
set(_dirty "false")
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
        # Numeric release tags match the existing Lua workflow convention.
        if(_candidate MATCHES "^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)$")
            if(_tag)
                message(FATAL_ERROR "Multiple Lua release tags point at HEAD")
            endif()
            set(_tag "${_candidate}")
        endif()
    endforeach()
    string(SUBSTRING "${_revision}" 0 12 _short)
    if(_tag)
        set(_version "${_tag}")
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
endif()
configure_file("${CMAKE_CURRENT_LIST_DIR}/lua-version.json.in" "${LUA_BINARY}/lua-version.json" @ONLY)
