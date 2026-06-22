# Copyright (c) 2025-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

set(CMAKE_AUTOMOC ON)

option(ENABLE_TEST_AUTOMATION "Enable test automation bridge for QML UI testing" OFF)

set(QML_QRC "${CMAKE_CURRENT_SOURCE_DIR}/bitcoin_qml.qrc")
qt6_add_resources(QML_QRC_CPP ${QML_QRC})

file(GLOB_RECURSE QML_SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)
list(FILTER QML_SOURCES EXCLUDE REGEX "/main\\.cpp$")
list(FILTER QML_SOURCES EXCLUDE REGEX "/androidnotifier\\.(cpp|h)$")
if(NOT ENABLE_TEST_AUTOMATION)
  list(FILTER QML_SOURCES EXCLUDE REGEX "/test/")
endif()
list(APPEND QML_SOURCES ${QML_QRC_CPP})

add_library(bitcoinqml STATIC ${QML_SOURCES})

target_compile_definitions(bitcoinqml
  PUBLIC
    QT_NO_KEYWORDS
    QT_USE_QSTRINGBUILDER
)
if(ENABLE_TEST_AUTOMATION)
  target_compile_definitions(bitcoinqml PUBLIC ENABLE_TEST_AUTOMATION)
endif()

target_include_directories(bitcoinqml
  PUBLIC
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_BINARY_DIR}/src
)

target_link_libraries(bitcoinqml
  PUBLIC
    core_interface
    bitcoin_node
    univalue
    Boost::headers
    $<TARGET_NAME_IF_EXISTS:QRencode::QRencode>
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Network
    Qt6::Widgets
)

add_executable(bitcoin-qml WIN32
  main.cpp
  ${PROJECT_SOURCE_DIR}/src/init/bitcoin-qt.cpp
)

# TODO: Switch to own resource file.
add_windows_resources(bitcoin-qml ${PROJECT_SOURCE_DIR}/src/qt/res/bitcoin-qt-res.rc)
add_windows_application_manifest(bitcoin-qml)

target_link_libraries(bitcoin-qml
  PRIVATE
    bitcoinqml
    bitcoinqt
)

install_binary_component(bitcoin-qml)

# Use the `bitcoin-qml` target as a drop-in replacement
# for the legacy QtWidgets-based `bitcoin-qt`.
set_target_properties(bitcoin-qt PROPERTIES
  OUTPUT_NAME "bitcoin-qt-legacy"
  EXCLUDE_FROM_ALL TRUE
)
set_target_properties(bitcoin-qml PROPERTIES
  OUTPUT_NAME "bitcoin-qt"
)
