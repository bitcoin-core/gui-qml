# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Compile the same application QML into every executable that loads it. Keep
# resource URLs identical so tests exercise the production generated units.
function(bitcoin_qml_add_application_sources target)
  cmake_parse_arguments(arg "SCAN_IMPORTS" "" "" ${ARGN})
  foreach(qml_file IN LISTS BITCOIN_QML_FILES_ABSOLUTE)
    file(RELATIVE_PATH resource_alias "${PROJECT_SOURCE_DIR}" "${qml_file}")
    if(resource_alias MATCHES "^qml/compat/[^/]+/(.+)$")
      set(resource_alias "qml/controls/${CMAKE_MATCH_1}")
    endif()
    set_source_files_properties("${qml_file}" PROPERTIES QT_RESOURCE_ALIAS "${resource_alias}")
  endforeach()

  set(output_directory "${CMAKE_CURRENT_BINARY_DIR}/${target}/BitcoinCore/Gui")
  # Cachegen consumes this build-only descriptor even with qmldir generation off.
  file(GENERATE OUTPUT "${output_directory}/qmldir" CONTENT "module BitcoinCore.Gui\n")
  set(module_options)
  if(Qt6_VERSION VERSION_GREATER_EQUAL 6.3 AND NOT arg_SCAN_IMPORTS)
    list(APPEND module_options NO_IMPORT_SCAN)
  endif()
  qt6_add_qml_module(${target}
    URI BitcoinCore.Gui
    VERSION 1.0
    OUTPUT_DIRECTORY "${output_directory}"
    RESOURCE_PREFIX /
    NO_RESOURCE_TARGET_PATH
    NO_GENERATE_QMLDIR
    NO_GENERATE_QMLTYPES
    ${module_options}
    NO_LINT
    QML_FILES ${BITCOIN_QML_FILES_ABSOLUTE}
  )
endfunction()
