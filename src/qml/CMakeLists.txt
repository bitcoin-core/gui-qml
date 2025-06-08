# Copyright (c) 2025-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

set(CMAKE_AUTOMOC ON)

add_compile_definitions(
  QT_NO_KEYWORDS
  QT_USE_QSTRINGBUILDER
)

add_subdirectory(BitcoinApp)

add_executable(bitcoin-qml WIN32
  main.cpp
  ${PROJECT_SOURCE_DIR}/src/init/bitcoin-qt.cpp
)

# TODO: Switch to own resource file.
add_windows_resources(bitcoin-qml ${PROJECT_SOURCE_DIR}/src/qt/res/bitcoin-qt-res.rc)
add_windows_application_manifest(bitcoin-qml)

target_link_libraries(bitcoin-qml
  core_interface
  bitcoin_qml
  bitcoinqt
  bitcoin_node
)

install_binary_component(bitcoin-qml)
