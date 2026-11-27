// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Layouts 1.15
import "../controls"

RowLayout {
    id: warningRow
    // Qt 6.2 does not inject `modelData`/`index` into this
    // delegate; declare them explicitly.
    required property var modelData
    required property int index

    readonly property int warningLineCount: warningText.lineCount
    readonly property int warningWrapMode: warningText.wrapMode

    spacing: Theme.space.ml

    Icon {
        Layout.preferredWidth: Theme.size.icon.l
        Layout.preferredHeight: Theme.size.icon.l
        Layout.alignment: Qt.AlignTop
        source: "image://images/alert-filled"
        color: Theme.color.warning
        size: Theme.size.icon.l
    }

    CoreText {
        id: warningText
        objectName: "nodeWarningText_" + warningRow.index
        Layout.fillWidth: true
        text: warningRow.modelData
        color: Theme.color.neutral8
        textStyle: Theme.text.description
        wrapMode: Text.WordWrap
        horizontalAlignment: Text.AlignLeft
    }
}
