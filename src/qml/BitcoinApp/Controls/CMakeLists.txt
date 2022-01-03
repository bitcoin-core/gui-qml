# Copyright (c) 2025-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

qt6_add_qml_module(bitcoin_qml_controls
  URI BitcoinApp.Controls
  VERSION 1.0
  STATIC
  RESOURCE_PREFIX /qt/qml/
  QML_FILES
    ContinueButton.qml
    Header.qml
    OptionButton.qml
    OptionSwitch.qml
    ProgressIndicator.qml
    Setting.qml
)
