// Copyright (c) 2024-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0
import "../../controls"

Button {
    id: root
    objectName: "walletBadge"

    property string balance: ""
    property var balanceSatoshi: 0
    property bool loading: false
    property bool noWalletLoaded: false
    property bool noWalletsFound: false
    property int keySchemeKind: WalletQmlModel.SingleKey
    property string walletType: ""

    implicitWidth: 240
    implicitHeight: Math.max(48, implicitContentHeight + topPadding + bottomPadding)
    leftPadding: 10
    rightPadding: 10
    topPadding: 5
    bottomPadding: 5
    hoverEnabled: AppMode.isDesktop

    HoverHandler { cursorShape: Qt.PointingHandCursor }

    background: StateBackground {
        radius: Theme.radius.cardInset
        stateColor: root.hovered || root.checked ? Theme.color.surfaceHover : "transparent"
        FocusBorder { visible: root.visualFocus; targetRadius: parent.radius }
    }

    contentItem: RowLayout {
        spacing: 8
        RowLayout {
            objectName: "walletBadgeSkeleton"
            visible: root.loading
            Layout.fillWidth: true
            spacing: 8
            Skeleton {
                Layout.preferredWidth: 24
                Layout.preferredHeight: 24
                loading: root.loading
                radius: 5
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 5
                Skeleton {
                    Layout.fillWidth: true
                    Layout.maximumWidth: 88
                    Layout.preferredHeight: 12
                    loading: root.loading
                }
                Skeleton {
                    Layout.fillWidth: true
                    Layout.maximumWidth: 114
                    Layout.preferredHeight: 10
                    loading: root.loading
                }
            }
        }
        Icon {
            objectName: "walletBadgeTypeIcon"
            visible: !root.loading && !root.noWalletLoaded
            Layout.preferredWidth: 24
            Layout.preferredHeight: 24
            size: 24
            source: {
                if (root.keySchemeKind === WalletQmlModel.WatchOnly) return "image://images/visible-filled"
                if (root.keySchemeKind === WalletQmlModel.MultiKey) return "image://images/two-keys-filled"
                return "image://images/key-filled"
            }
            color: Theme.color.neutral9
        }
        ColumnLayout {
            visible: !root.loading
            Layout.fillWidth: true
            spacing: 3
            CoreText {
                Layout.fillWidth: true
                text: root.noWalletLoaded
                    ? (root.noWalletsFound ? qsTr("Add wallet") : qsTr("Select wallet")) : root.text
                textStyle: Theme.text.subheading
                color: Theme.color.neutral9
                horizontalAlignment: Text.AlignLeft
                wrap: false
                elide: Text.ElideRight
            }
            BitcoinAmountDisplayLabel {
                objectName: "walletBadgeBalanceText"
                Layout.fillWidth: true
                visible: !root.loading && !root.noWalletLoaded
                amount: root.balance
                unit: optionsModel.displayUnit === BitcoinAmount.SAT
                    ? optionsModel.displayUnitLabelForAmount(root.balanceSatoshi) : optionsModel.displayUnitLabel
                color: Theme.color.neutral8
                textStyle: Theme.text.amountCaption
                horizontalAlignment: Text.AlignLeft
                wrap: false
                elide: Text.ElideRight
            }
        }

    }
}
