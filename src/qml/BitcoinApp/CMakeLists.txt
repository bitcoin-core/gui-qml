# Copyright (c) 2025-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

qt6_add_qml_module(bitcoin_qml
  URI BitcoinApp
  VERSION 1.0
  STATIC
  SOURCES
    bitcoin.cpp
    nodemodel.cpp
  RESOURCE_PREFIX /qt/qml/
  QML_FILES
    stub.qml
)

target_link_libraries(bitcoin_qml
  INTERFACE
    bitcoin_qmlplugin
  PUBLIC
    Qt6::Widgets
    Qt6::Quick
)
