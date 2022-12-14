# Copyright (c) 2025-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

qt6_add_qml_module(bitcoin_qml
  URI BitcoinApp
  VERSION 1.0
  STATIC
  SOURCES
    appmode.h
    bitcoin.cpp
    imageprovider.cpp
    nodemodel.cpp
    util.cpp
  RESOURCE_PREFIX /qt/qml/
  QML_FILES
    initerrormessage.qml
    main.qml
    OnboardingBlockclock.qml
    OnboardingConnection.qml
    OnboardingCover.qml
    OnboardingStorageAmount.qml
    OnboardingStorageLocation.qml
    OnboardingStrengthen.qml
    SettingsAbout.qml
    SettingsConnection.qml
    SettingsDeveloper.qml
    SettingsStorage.qml
  RESOURCES
    res/fonts/Inter-Regular.otf
    res/fonts/Inter-SemiBold.otf
    res/icons/arrow-down.png
    res/icons/arrow-up.png
    res/icons/bitcoin-circle.png
    res/icons/blocktime-dark.png
    res/icons/blocktime-light.png
    res/icons/caret-left.png
    res/icons/caret-right.png
    res/icons/check.png
    res/icons/cross.png
    res/icons/export.png
    res/icons/gear.png
    res/icons/info.png
    res/icons/network-dark.png
    res/icons/network-light.png
    res/icons/storage-dark.png
    res/icons/storage-light.png
)

add_subdirectory(Components)
add_subdirectory(Controls)

target_link_libraries(bitcoin_qml
  INTERFACE
    bitcoin_qmlplugin
    bitcoin_qml_componentsplugin
    bitcoin_qml_controlsplugin
  PUBLIC
    Qt6::Widgets
    Qt6::Quick
)
