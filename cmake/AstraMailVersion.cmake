# CMake 3.7-compatible version reader; does not change compiler/filesystem flags.
file(READ "${CMAKE_SOURCE_DIR}/app-version.json" _astra_version_json)
foreach(_part major minor patch)
    string(REGEX MATCH "\"${_part}\"[ \t\r\n]*:[ \t\r\n]*([0-9]+)" _match "${_astra_version_json}")
    if(NOT _match)
        message(FATAL_ERROR "Missing numeric ${_part} in app-version.json")
    endif()
    set(_astra_${_part} "${CMAKE_MATCH_1}")
endforeach()
set(ASTRA_MAIL_APP_VERSION "${_astra_major}.${_astra_minor}.${_astra_patch}")
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/generated")
configure_file("${CMAKE_SOURCE_DIR}/app-version.json"
               "${CMAKE_BINARY_DIR}/generated/app-version.json" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/AstraMailVersion.h.in"
               "${CMAKE_BINARY_DIR}/generated/AstraMailVersion.h" @ONLY)
include_directories("${CMAKE_BINARY_DIR}/generated")
