# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

include_guard(GLOBAL)
if(NOT WIN32)
  return()
endif()

# Use the pinned Core manifest and the same MSVC/MinGW resource attachment.
# Explicit paths let this apply to outer executables defined in subdirectories.
function(app_windows_manifest target)
  foreach(component MAJOR MINOR BUILD)
    get_directory_property(CLIENT_VERSION_${component}
      DIRECTORY "${CMAKE_SOURCE_DIR}/bitcoin" DEFINITION CLIENT_VERSION_${component})
    if(NOT DEFINED CLIENT_VERSION_${component} OR "${CLIENT_VERSION_${component}}" STREQUAL "")
      message(FATAL_ERROR "Missing pinned Core CLIENT_VERSION_${component}")
    endif()
  endforeach()
  set(manifest "${CMAKE_CURRENT_BINARY_DIR}/${target}.manifest")
  configure_file("${CMAKE_SOURCE_DIR}/bitcoin/cmake/windows-app.manifest.in" "${manifest}" USE_SOURCE_PERMISSIONS)
  if(MSVC)
    target_sources("${target}" PRIVATE "${manifest}")
  else()
    set(resource "${CMAKE_CURRENT_BINARY_DIR}/${target}-manifest.rc")
    file(CONFIGURE OUTPUT "${resource}"
      CONTENT "1 /* CREATEPROCESS_MANIFEST_RESOURCE_ID */ 24 /* RT_MANIFEST */ \"${manifest}\"")
    target_sources("${target}" PRIVATE "${resource}")
  endif()
endfunction()

function(app_windows_manifests directory)
  if(directory MATCHES "^${CMAKE_SOURCE_DIR}/bitcoin($|/)")
    return()
  endif()
  get_property(targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
  foreach(target IN LISTS targets)
    get_target_property(type "${target}" TYPE)
    if(type STREQUAL "EXECUTABLE")
      app_windows_manifest("${target}")
    endif()
  endforeach()
  get_property(children DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
  foreach(child IN LISTS children)
    app_windows_manifests("${child}")
  endforeach()
endfunction()

cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL app_windows_manifests "${CMAKE_SOURCE_DIR}")
