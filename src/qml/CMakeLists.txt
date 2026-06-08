# Copyright (c) 2025-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

set(CMAKE_AUTOMOC ON)

add_library(bitcoinqml STATIC
  bitcoin.cpp
)

target_compile_definitions(bitcoinqml
  PUBLIC
    QT_NO_KEYWORDS
    QT_USE_QSTRINGBUILDER
)

target_include_directories(bitcoinqml
  PUBLIC
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_BINARY_DIR}/src
)

target_link_libraries(bitcoinqml
  PUBLIC
    core_interface
    bitcoin_node
    Qt6::Qml
    Qt6::Widgets
    Qt6::Quick
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
