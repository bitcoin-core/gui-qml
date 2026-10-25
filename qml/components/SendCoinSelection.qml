// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../controls"
Popup {
    id: root
    objectName: "coinSelectionPopup"
    readonly property color modalOverlayColor: Qt.rgba(0, 0, 0, 0.4)
    property real verticalOffset: 0
    property var wallet
    property var editingCoins: null
    parent: Overlay.overlay
    implicitWidth: 1000
    width: Math.min(1000, parent.width - 40)
    height: Math.min(800, parent.height - 40)
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) + verticalOffset : verticalOffset
    leftPadding: SizeClass.isCompact(width) ? Theme.space.pageCompact : Theme.space.pageRegular
    rightPadding: SizeClass.isCompact(width) ? Theme.space.pageCompact : Theme.space.pageRegular
    topPadding: SizeClass.isCompact(width) ? 20 : 30
    bottomPadding: SizeClass.isCompact(width) ? 20 : 30
    modal: true; dim: true; focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

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

    Overlay.modal: Rectangle {
        color: root.modalOverlayColor
        opacity: root.opacity
    }

    background: Rectangle {
        radius: Theme.radius.popup
        color: Theme.color.neutral1
        border.color: Theme.color.neutral3
        SurfaceGradientBorder {
            anchors.fill: parent
            surfaceColor: parent.color
            cornerRadius: parent.radius
        }
    }
    onAboutToShow: { editingCoins = wallet.coinsListModel; browser.fallbackTarget = wallet.sendTotalSatoshi; editingCoins.beginSelection(); wallet.scheduleFeeEstimates() }
    onClosed: { if (editingCoins) editingCoins.cancelSelection(); editingCoins = null }
    onWalletChanged: if (opened) close()
    contentItem: ColumnLayout {
        spacing: Theme.space.xl
        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                Layout.fillWidth: true; spacing: 8
                //: Title of the manual transaction-input selection dialog.
                CoreText { Layout.fillWidth: true; text: qsTr("View coins"); font: Theme.text.title.font; horizontalAlignment: Text.AlignLeft }
                //: Description of the manual input selection task.
                CoreText { Layout.fillWidth: true; text: qsTr("Choose which coins to use for this payment."); font: Theme.text.description.font; color: Theme.color.neutral7; horizontalAlignment: Text.AlignLeft }
            }
            CloseButton { objectName: "coinSelectionCloseButton"; onClicked: root.close() }
        }
        CoinsList { id: browser; objectName: "sendCoinsList"; Layout.fillWidth: true; Layout.fillHeight: true; wallet: root.wallet; coins: root.editingCoins }
        RowLayout {
            Layout.fillWidth: true; spacing: 12
            CoreText {
                objectName: "coinSelectionTotalSelectedText"
                Layout.fillWidth: true; horizontalAlignment: Text.AlignLeft; font: Theme.text.description.font; color: Theme.color.neutral7
                //: The count includes selected coins hidden by filters or search.
                text: root.editingCoins ? (root.editingCoins.selectedCoinsCount === 1 ? qsTr("1 input selected") : qsTr("%1 inputs selected").arg(root.editingCoins.selectedCoinsCount)) : ""
            }
            //: Discard changes to the input selection.
            NeutralButton { objectName: "coinSelectionCancelButton"; text: qsTr("Cancel"); implicitHeight: Theme.size.controlLarge; onClicked: root.close() }
            ContinueButton {
                objectName: "coinSelectionDoneButton"
                //: Apply these selected coins to the transaction.
                text: qsTr("Use selected inputs")
                enabled: root.editingCoins && root.editingCoins.selectedCoinsCount > 0
                onClicked: { root.editingCoins.applySelection(); root.close() }
            }
        }
    }
}
