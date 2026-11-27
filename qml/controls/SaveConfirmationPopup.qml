// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQml 2.15
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0

// Asks before a file is written to its final destination. It shows while
// `operation` waits for confirmation and the caller's view is `active`, and
// answers the operation; the operation's state closes it. Replacing a file is
// destructive, so Return only confirms a new file.
AlertPopup {
    id: root

    property FileSaveOperation operation: null
    property bool active: true
    property bool replacing: operation !== null && operation.replacing
    property string finalPath: operation !== null ? operation.finalPath : ""
    // Whole sentences, never a joined noun.
    property string saveTitle: qsTr("Save file?")
    property string replaceTitle: qsTr("Replace file?")
    property string pathObjectName: "saveConfirmationPath"
    property string approveObjectName: "saveConfirmationApproveButton"
    property string refuseObjectName: "saveConfirmationRefuseButton"

    signal approved()
    signal refused()

    visible: active && operation !== null && operation.confirmationRequired
    title: replacing ? replaceTitle : saveTitle
    message: replacing ? qsTr("The existing file at this location will be replaced.")
                       : qsTr("The file will be saved at this location.")

    // A long path scrolls; the title and actions stay inside the window.
    content: Flickable {
        id: pathArea
        objectName: root.pathObjectName + "Area"
        Layout.fillWidth: true
        // At most 40% of the window, so the title and actions always fit.
        Layout.preferredHeight: Math.min(pathText.implicitHeight, root.parent ? Math.max(48, root.parent.height * 0.4) : 240)
        contentWidth: width
        contentHeight: pathText.implicitHeight
        clip: true
        interactive: contentHeight > height
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {
            policy: pathArea.interactive ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
        }

        SelectableText {
            id: pathText
            objectName: root.pathObjectName
            width: pathArea.width
            text: root.finalPath
            textFormat: TextEdit.PlainText
            textStyle: Theme.text.monoDescription
            wrapMode: TextEdit.WrapAnywhere
            horizontalAlignment: TextEdit.AlignHCenter
        }
    }

    AlertAction {
        text: qsTr("Cancel")
        role: AlertAction.Cancel
        buttonObjectName: root.refuseObjectName
        closesPopup: false
        onTriggered: {
            if (root.operation) root.operation.cancel()
            root.refused()
        }
    }
    AlertAction {
        text: root.replacing ? qsTr("Replace file") : qsTr("Save file")
        role: root.replacing ? AlertAction.Destructive : AlertAction.Normal
        buttonObjectName: root.approveObjectName
        closesPopup: false
        onTriggered: {
            if (root.operation) root.operation.confirm()
            root.approved()
        }
    }
}
