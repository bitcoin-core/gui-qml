# Copyright (c) 2025-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

qt6_add_qml_module(bitcoin_qml_components
  URI BitcoinApp.Components
  VERSION 1.0
  STATIC
  SOURCES
    blockclockdial.cpp
  RESOURCE_PREFIX /qt/qml/
  QML_FILES
    AboutOptions.qml
    BlockClock.qml
    BlockCounter.qml
    ConnectionOptions.qml
    ConnectionSettings.qml
    DeveloperOptions.qml
    StorageLocations.qml
    StorageOptions.qml
    StorageSettings.qml
)

target_link_libraries(bitcoin_qml_components
  PRIVATE
    Qt6::Quick
)
