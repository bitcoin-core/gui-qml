pragma ComponentBehavior: Bound

// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15

LabeledTextField {
    id: root

    property bool passwordVisible: false
    property string visibilityToggleObjectName: root.objectName.length > 0
        ? root.objectName + "VisibilityToggle"
        : ""

    echoMode: passwordVisible ? TextInput.Normal : TextInput.Password
    inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText

    trailingItem: Icon {
        objectName: root.visibilityToggleObjectName
        source: root.passwordVisible ? "qrc:/icons/hidden" : "qrc:/icons/visible"
        color: enabled ? Theme.color.neutral7 : Theme.color.neutral4
        size: 22
        enabled: root.enabled
        Accessible.name: root.passwordVisible ? qsTr("Hide password") : qsTr("Show password")
        onClicked: root.passwordVisible = !root.passwordVisible

        HoverHandler {
            cursorShape: Qt.PointingHandCursor
        }
    }
}
