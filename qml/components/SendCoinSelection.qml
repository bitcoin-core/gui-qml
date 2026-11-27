// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../controls"
DialogPopup {
    id: root
    objectName: "coinSelectionPopup"
    property var wallet
    // The coins of the wallet the dialog opened for; a wallet change while
    // it is open closes it and cancels that wallet's selection.
    property var editingCoins: null
    Binding on editingCoins {
        when: !root.visible
        value: root.wallet ? root.wallet.coinsListModel : null
        restoreMode: Binding.RestoreNone
    }
    //: Title of the manual transaction-input selection dialog.
    title: qsTr("View coins")
    implicitWidth: 1000
    implicitHeight: 800

    onAboutToShow: { if (editingCoins) editingCoins.beginSelection(); if (wallet) wallet.scheduleFeeEstimates() }
    onAboutToHide: if (editingCoins) editingCoins.cancelSelection()
    onWalletChanged: if (opened) close()
    contentItem: ColumnLayout {
        spacing: Theme.space.xl
        DialogHeader {
            Layout.fillWidth: true
            title: root.title
            //: Description of the manual input selection task.
            description: qsTr("Choose which coins to use for this payment.")
            closeButtonObjectName: "coinSelectionCloseButton"
            onCloseRequested: root.close()
        }
        ScrollView {
            id: bodyScroll
            objectName: "coinSelectionBodyScroll"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 0
            contentWidth: availableWidth
            contentHeight: browser.implicitHeight
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            CoinsList {
                id: browser
                objectName: "sendCoinsList"
                width: bodyScroll.availableWidth
                // Qt 6.2 can recalculate the implicit height during layout.
                Binding on height {
                    value: Math.max(browser.implicitHeight, bodyScroll.availableHeight)
                    delayed: true
                }
                wallet: root.wallet
                coins: root.editingCoins
            }
        }
        RowLayout {
            Layout.fillWidth: true; spacing: 12
            CoreText {
                objectName: "coinSelectionTotalSelectedText"
                Layout.fillWidth: true; horizontalAlignment: Text.AlignLeft; textStyle: Theme.text.description; color: Theme.color.neutral7
                //: The count includes selected coins hidden by filters or search.
                text: root.editingCoins ? (root.editingCoins.selectedCoinsCount === 1 ? qsTr("1 input selected") : qsTr("%1 inputs selected").arg(root.editingCoins.selectedCoinsCount)) : ""
            }
            //: Discard changes to the input selection.
            NeutralButton { objectName: "coinSelectionCancelButton"; text: qsTr("Cancel"); implicitHeight: Theme.size.controlLarge; onClicked: root.close() }
            PrimaryButton {
                objectName: "coinSelectionDoneButton"
                //: Apply these selected coins to the transaction.
                text: qsTr("Use selected inputs")
                enabled: root.editingCoins && root.editingCoins.selectedCoinsCount > 0
                onClicked: { root.editingCoins.applySelection(); root.close() }
            }
        }
    }
}
