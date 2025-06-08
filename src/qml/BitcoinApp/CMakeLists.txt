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
    util.cpp
  RESOURCE_PREFIX /qt/qml/
  QML_FILES
    initerrormessage.qml
    stub.qml
)

add_subdirectory(Components)

target_link_libraries(bitcoin_qml
  INTERFACE
    bitcoin_qmlplugin
    bitcoin_qml_componentsplugin
  PUBLIC
    Qt6::Widgets
    Qt6::Quick
)
