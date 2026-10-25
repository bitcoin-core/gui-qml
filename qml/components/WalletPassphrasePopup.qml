// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import "../controls"

Popup {
    id: root

    property string popupObjectName: ""
    property string titleText: qsTr("Enter wallet password")
    property string descriptionText: ""
    property string confirmText: qsTr("Continue")
    property string busyConfirmText: qsTr("Working...")
    property string errorText: ""
    property string passphraseFieldObjectName: ""
    property string errorTextObjectName: ""
    property string cancelButtonObjectName: ""
    property string confirmButtonObjectName: ""
    property bool busy: false
    property real verticalOffset: 0

    signal submitted(string passphrase)

    objectName: popupObjectName
    modal: true
    dim: true
    padding: 0
    implicitWidth: 420
    implicitHeight: columnLayout.implicitHeight
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) + verticalOffset : verticalOffset

    Overlay.modal: Rectangle {
        color: Qt.rgba(0, 0, 0, 0.5)
    }

    enter: Transition {
        NumberAnimation {
            property: "opacity"
            from: 0
            to: 1
            duration: Theme.motion.fade(Theme.motion.slow)
            easing.type: Theme.motion.easing.enter
        }
        NumberAnimation {
            property: "verticalOffset"
            from: -Theme.motion.dialogOffset
            to: 0
            duration: Theme.motion.travel(Theme.motion.slow)
            easing.type: Theme.motion.easing.enter
        }
    }

    exit: Transition {
        NumberAnimation {
            property: "opacity"
            to: 0
            duration: Theme.motion.fade(Theme.motion.exit)
            easing.type: Theme.motion.easing.exit
        }
        NumberAnimation {
            property: "verticalOffset"
            to: -Theme.motion.dialogOffset
            duration: Theme.motion.travel(Theme.motion.exit)
            easing.type: Theme.motion.easing.exit
        }
    }

    function clearPassphraseField() {
        if (passphraseField.text.length > 0) {
            passphraseField.text = Array(passphraseField.text.length + 1).join(" ")
            passphraseField.text = ""
        }
    }

    function submitPassphrase() {
        const passphrase = passphraseField.text
        clearPassphraseField()
        root.submitted(passphrase)
    }

    onOpened: {
        clearPassphraseField()
        passphraseField.forceActiveFocus()
    }
    onAboutToHide: clearPassphraseField()
    onClosed: clearPassphraseField()
    Component.onDestruction: clearPassphraseField()

    background: Rectangle {
        color: Theme.color.neutral1
        radius: Theme.radius.popup
        border.color: Theme.color.neutral2
        border.width: 1
        SurfaceGradientBorder {
            anchors.fill: parent
            surfaceColor: parent.color
            cornerRadius: parent.radius
        }
    }

    ColumnLayout {
        id: columnLayout
        anchors.fill: parent
        spacing: 0

        CoreText {
            Layout.fillWidth: true
            Layout.preferredHeight: 56
            text: root.titleText
            bold: true
            font.pixelSize: 24
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }

        Separator {
            Layout.fillWidth: true
            color: Theme.color.neutral2
        }

        Header {
            Layout.fillWidth: true
            Layout.margins: Theme.space.xl
            Layout.topMargin: Theme.space.xl
            header: root.descriptionText
            headerBold: false
            headerSize: 16
        }

        CoreTextField {
            id: passphraseField
            objectName: root.passphraseFieldObjectName
            Layout.fillWidth: true
            Layout.leftMargin: Theme.space.xl
            Layout.rightMargin: Theme.space.xl
            hideText: true
            placeholderText: qsTr("Enter password...")
            background: Rectangle {
                color: Theme.color.neutral2
                radius: Theme.radius.s
                border.color: Theme.color.neutral2
                border.width: 1
            }
            onAccepted: {
                if (confirmButton.enabled) {
                    root.submitPassphrase()
                }
            }
        }

        CoreText {
            objectName: root.errorTextObjectName
            Layout.fillWidth: true
            Layout.leftMargin: Theme.space.xl
            Layout.rightMargin: Theme.space.xl
            Layout.topMargin: Theme.space.ml
            visible: text.length > 0
            text: root.errorText
            color: Theme.color.red
            font.pixelSize: 15
            wrapMode: Text.WordWrap
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.space.xl
            Layout.topMargin: Theme.space.xl
            spacing: Theme.space.l

            OutlineButton {
                objectName: root.cancelButtonObjectName
                Layout.fillWidth: true
                Layout.minimumWidth: 120
                enabled: !root.busy
                text: qsTr("Cancel")
                onClicked: root.close()
            }

            ContinueButton {
                id: confirmButton
                objectName: root.confirmButtonObjectName
                Layout.fillWidth: true
                Layout.minimumWidth: 120
                enabled: !root.busy && passphraseField.text.length > 0
                text: root.busy ? root.busyConfirmText : root.confirmText
                onClicked: root.submitPassphrase()
            }
        }
    }
}
