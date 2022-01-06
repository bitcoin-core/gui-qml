# Copyright (c) 2025-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

set_source_files_properties(
  Theme.qml
  PROPERTIES QT_QML_SINGLETON_TYPE TRUE
)

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
    PageIndicator.qml
    ProgressIndicator.qml
    Setting.qml
    TextButton.qml
    Theme.qml
)
