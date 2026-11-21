// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQml 2.15
import QtQuick 2.15
import QtQuick.Layouts 1.15

// Asks before a file is written to its final destination. The caller binds
// `visible` to the operation's confirmation state and answers approved() and
// refused(); the actions don't close the popup themselves. Replacing a file is
// destructive, so Return only confirms a new file.
AlertPopup {
    id: root

    property bool replacing: false
    property string finalPath: ""
    // Whole sentences, never a joined noun.
    property string saveTitle: qsTr("Save file?")
    property string replaceTitle: qsTr("Replace file?")
    property string pathObjectName: "saveConfirmationPath"
    property string approveObjectName: "saveConfirmationApproveButton"
    property string refuseObjectName: "saveConfirmationRefuseButton"

    signal approved()
    signal refused()

    title: replacing ? replaceTitle : saveTitle
    message: replacing ? qsTr("The existing file at this location will be replaced.")
                       : qsTr("The file will be saved at this location.")

    content: SelectableText {
        objectName: root.pathObjectName
        Layout.fillWidth: true
        text: root.finalPath
        textFormat: TextEdit.PlainText
        textStyle: Theme.text.monoDescription
        wrapMode: TextEdit.WrapAnywhere
        horizontalAlignment: TextEdit.AlignHCenter
    }

    AlertAction {
        text: qsTr("Cancel")
        role: AlertAction.Cancel
        buttonObjectName: root.refuseObjectName
        closesPopup: false
        onTriggered: root.refused()
    }
    AlertAction {
        text: root.replacing ? qsTr("Replace file") : qsTr("Save file")
        role: root.replacing ? AlertAction.Destructive : AlertAction.Normal
        buttonObjectName: root.approveObjectName
        closesPopup: false
        onTriggered: root.approved()
    }
}
