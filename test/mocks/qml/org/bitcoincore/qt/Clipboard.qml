// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

pragma Singleton
import QtQuick 2.15

QtObject {
    property string currentText: ""
    property string currentSelectionText: ""
    property double m_revision: 0
    property double m_selectionRevision: 0
    readonly property double revision: m_revision
    readonly property double selectionRevision: m_selectionRevision
    readonly property bool primarySelectionSupported: true
    onCurrentTextChanged: { m_revision += 1; dataChanged() }
    onCurrentSelectionTextChanged: { m_selectionRevision += 1; selectionChanged() }

    signal dataChanged()
    signal selectionChanged()

    function text() { return currentText }
    function selectionText() { return currentSelectionText }
    function setText(value) {
        currentText = value
    }
}
