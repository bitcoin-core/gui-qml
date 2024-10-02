// Copyright (c) 2024-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../../controls"
import "../../components"

Page {
    signal back
    property bool onboarding: false
    property bool snapshotImportCompleted: onboarding ? false : chainModel.isSnapshotActive

    id: root

    background: null
    implicitWidth: 450
    leftPadding: 20
    rightPadding: 20
    topPadding: 30

    header: NavigationBar2 {
        leftItem: NavButton {
            iconSource: "image://images/caret-left"
            text: qsTr("Back")
            onClicked: root.back()
        }
    }
    SnapshotSettings {
        width: Math.min(parent.width, 450)
        anchors.horizontalCenter: parent.horizontalCenter
        onboarding: root.onboarding
        snapshotImportCompleted: root.snapshotImportCompleted
        onBack: root.back()
    }
}
