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
    implicitHeight: 48
    leftPadding: 10
    rightPadding: 10
    topPadding: 5
    bottomPadding: 5
    hoverEnabled: AppMode.isDesktop

    HoverHandler { cursorShape: Qt.PointingHandCursor }

    background: Rectangle {
        radius: 11
        color: root.hovered || root.checked ? Theme.color.neutral2 : Theme.color.neutral0
        FocusBorder { visible: root.visualFocus }
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
                font: Theme.text.subheading.font
                color: Theme.color.neutral9
                horizontalAlignment: Text.AlignLeft
                wrap: false
                elide: Text.ElideRight
            }
            CoreText {
                objectName: "walletBadgeBalanceText"
                Layout.fillWidth: true
                visible: !root.loading && !root.noWalletLoaded
                text: optionsModel.displayUnit === 0
                    ? optionsModel.displayUnitLabelForAmount(root.balanceSatoshi) + " " + root.balance
                    : root.balance + " " + optionsModel.displayUnitLabelForAmount(root.balanceSatoshi)
                color: Theme.color.neutral8
                font.family: optionsModel.moneyFont.family
                font.weight: optionsModel.moneyFont.weight
                font.pixelSize: 13
                horizontalAlignment: Text.AlignLeft
                wrap: false
                elide: Text.ElideRight
            }
        }

    }
}
