# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

if(Qt6_VERSION VERSION_LESS 6.10)
  message(FATAL_ERROR "ENABLE_QML_LINT requires Qt 6.10 or newer")
endif()

# Extract the actual C++ properties, methods and signals. The foreign type
# declarations describe the application's imperative registrations without
# registering another copy or changing the lifetime of its singleton objects.
qt6_extract_metatypes(bitcoinqml)
add_library(bitcoinqml_tooling STATIC EXCLUDE_FROM_ALL)
target_link_libraries(bitcoinqml_tooling PRIVATE bitcoinqml)
target_include_directories(bitcoinqml_tooling PRIVATE
  ${PROJECT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/bitcoin/src
  ${PROJECT_BINARY_DIR}/bitcoin/src
)
qt6_add_qml_module(bitcoinqml_tooling
  URI org.bitcoincore.qt
  VERSION 1.0
  NO_PLUGIN
  NO_CACHEGEN
  RESOURCE_PREFIX /
  OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/qml-imports/org/bitcoincore/qt
  DEPENDENCIES QtQuick QtQml.Models
  SOURCES ${PROJECT_SOURCE_DIR}/cmake/qmltypes.h
)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_custom_target(qml_lint
  COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/test/test_lint_qml.py
    --qmllint $<TARGET_FILE:Qt6::qmllint>
    --imports ${PROJECT_BINARY_DIR}/qml-imports
    --build-dir ${PROJECT_BINARY_DIR}/qml-lint/gate-tests
  COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/test/lint_qml.py
    --qmllint $<TARGET_FILE:Qt6::qmllint>
    --imports ${PROJECT_BINARY_DIR}/qml-imports
    --build-dir ${PROJECT_BINARY_DIR}/qml-lint/qt65
    --resource ${PROJECT_SOURCE_DIR}/qml/bitcoin_qml.qrc
    --resource ${PROJECT_SOURCE_DIR}/qml/compat/qt65/bitcoin_compat.qrc
  COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/test/lint_qml.py
    --qmllint $<TARGET_FILE:Qt6::qmllint>
    --imports ${PROJECT_BINARY_DIR}/qml-imports
    --build-dir ${PROJECT_BINARY_DIR}/qml-lint/qt62
    --resource ${PROJECT_SOURCE_DIR}/qml/bitcoin_qml.qrc
    --resource ${PROJECT_SOURCE_DIR}/qml/compat/qt62/bitcoin_compat.qrc
  DEPENDS bitcoinqml_tooling_qmltyperegistration
  COMMENT "Checking both QML compatibility variants with zero warnings allowed"
  VERBATIM
)
