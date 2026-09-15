// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import org.bitcoincore.qt 1.0

import "../../controls"

Popup {
    id: root
    objectName: "walletSelectPopup"

    property alias model: listView.model
    property real verticalOffset: 0
    property var activeContextMenu: null
    implicitWidth: 360
    implicitHeight: layout.implicitHeight + 2 * padding
    padding: 6
    focus: true
    dim: true
    transformOrigin: Popup.TopLeft
    y: verticalOffset

    // Keep the trigger clickable while dimming the rest of the window.
    Overlay.modeless: Rectangle {
        color: Qt.rgba(0, 0, 0, 0.5)
        Behavior on opacity {
            NumberAnimation { duration: 250; easing.type: Easing.OutCubic }
        }
    }

    enter: Transition {
        NumberAnimation {
            property: "opacity"
            from: 0
            to: 1
            duration: 300
            easing.type: Easing.OutCubic
        }
        NumberAnimation {
            property: "verticalOffset"
            from: -12
            to: 0
            duration: 300
            easing.type: Easing.OutCubic
        }
        NumberAnimation {
            property: "scale"
            from: 0.96
            to: 1
            duration: 300
            easing.type: Easing.OutCubic
        }
    }

    exit: Transition {
        NumberAnimation {
            property: "opacity"
            from: 1
            to: 0
            duration: 250
            easing.type: Easing.InCubic
        }
        NumberAnimation {
            property: "verticalOffset"
            from: 0
            to: -12
            duration: 250
            easing.type: Easing.InCubic
        }
        NumberAnimation {
            property: "scale"
            from: 1
            to: 0.96
            duration: 250
            easing.type: Easing.InCubic
        }
    }

    signal walletSettingsRequested(string name, string format)

    signal addWallet()
    signal closeWalletRequested(string name)

    function closeLoadedWallet(name) {
        root.close()
        root.closeWalletRequested(name)
    }

    Connections {
        target: walletController
        function onWalletLoadSucceeded() {
            root.close()
        }
        function onWalletMigrationRequired() {
            root.close()
        }
        // Intentionally no handler for the LoadError branch of
        // walletLoadStateChanged — the failing row's inline LoadError state
        // stays visible until the user picks again.
    }

    background: Rectangle {
        color: Theme.color.neutral1
        border.color: Theme.dark ? Theme.color.neutral2 : Theme.color.neutral3
        border.width: 1
        radius: 5
    }

    ButtonGroup {
        id: buttonGroup
    }

    contentItem: ColumnLayout {
        id: layout
        spacing: 0
        ListView {
            objectName: "walletSelectList"
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(listView.contentHeight, 384)
            id: listView
            interactive: root.activeContextMenu === null
            clip: true
            spacing: 0
            ScrollBar.vertical: ScrollBar {
                enabled: listView.interactive
                interactive: listView.interactive
            }
            model: walletListModel
            section.property: "walletSection"
            section.criteria: ViewSection.FullString
            section.delegate: CoreText {
                required property string section
                objectName: "walletSelectSection_" + section
                width: listView.width
                height: 34
                leftPadding: 10
                text: section === "open" ? qsTr("Open wallets") : qsTr("Closed wallets")
                horizontalAlignment: Text.AlignLeft
                font: Theme.text.caption.font
                color: Theme.color.neutral6
            }

            delegate: ItemDelegate {
                id: delegate
                required property int index;
                required property string name;
                required property string displayName;
                required property string format;
                required property int loadState;
                required property string errorMessage;
                required property string balance;
                required property int keySchemeKind;

                readonly property string iconSource: {
                    if (loadState !== WalletListModel.Open) return "image://images/wallet"
                    const filled = delegate.checked
                    if (keySchemeKind === WalletQmlModel.WatchOnly) {
                        return filled ? "image://images/visible-filled" : "image://images/visible"
                    }
                    if (keySchemeKind === WalletQmlModel.MultiKey) {
                        return filled ? "image://images/two-keys-filled" : "image://images/two-keys"
                    }
                    return filled ? "image://images/key-filled" : "image://images/key"
                }
                readonly property string statusText: {
                    switch (loadState) {
                    case WalletListModel.Loading:
                        return qsTr("Loading…")
                    case WalletListModel.LoadError:
                        return qsTr("Failed to open wallet")
                    case WalletListModel.Open:
                        return optionsModel.displayUnit === 0
                            ? optionsModel.displayUnitLabelForAmount(0) + " " + balance
                            : balance + " " + optionsModel.displayUnitLabel
                    case WalletListModel.Closed:
                    default:
                        return ""
                    }
                }
                readonly property color statusColor: {
                    if (loadState === WalletListModel.LoadError) return Theme.color.red
                    return delegate.checked ? Theme.color.orange : Theme.color.neutral6
                }

                objectName: "walletSelectItem_" + name.replace(/[^A-Za-z0-9_]/g, "_")
                width: listView.width
                height: loadState === WalletListModel.Open ? 64 : 48
                checked: loadState === WalletListModel.Open && walletController.selectedWallet.name === name
                enabled: !walletController.walletLoadInProgress
                ButtonGroup.group: buttonGroup
                leftPadding: 10
                rightPadding: 10
                topPadding: 0
                bottomPadding: 0

                background: Rectangle {
                    radius: 6
                    color: delegate.hovered || delegate.visualFocus ? Theme.color.neutral3
                        : delegate.checked ? Theme.color.neutral2 : "transparent"
                    Rectangle {
                        visible: delegate.loadState === WalletListModel.Open
                            && delegate.index > 0 && !delegate.checked && !delegate.hovered
                        width: parent.width
                        height: 1
                        color: Theme.color.neutral2
                    }
                }

                HoverHandler {
                    cursorShape: Qt.PointingHandCursor
                }

                contentItem: RowLayout {
                    spacing: 10

                    Icon {
                        Layout.alignment: Qt.AlignVCenter
                        Layout.preferredWidth: 24
                        Layout.preferredHeight: 24
                        size: 24
                        source: delegate.iconSource
                        color: delegate.checked
                            ? Theme.color.orange
                            : Theme.color.neutral6
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignVCenter
                        spacing: 3

                        CoreText {
                            id: nameText
                            objectName: "walletSelectName_" + delegate.name.replace(/[^A-Za-z0-9_]/g, "_")
                            Layout.fillWidth: true
                            text: delegate.displayName
                            horizontalAlignment: Text.AlignLeft
                            font: Theme.text.menuItem.font
                            color: delegate.checked
                                ? Theme.color.orange
                                : (delegate.hovered ? Theme.color.neutral9 : Theme.color.neutral8)
                            wrap: false
                            elide: Text.ElideRight

                            ToolTip {
                                id: nameTooltip
                                text: delegate.displayName
                                visible: delegate.hovered && nameText.truncated
                                delay: 500
                                padding: 8
                                background: Rectangle {
                                    color: Theme.color.neutral0
                                    border.color: Theme.color.neutral4
                                    border.width: 1
                                    radius: 5
                                }
                                contentItem: CoreText {
                                    text: nameTooltip.text
                                    color: (delegate.hovered ? Theme.color.neutral9 : Theme.color.neutral8)
                                    font.pixelSize: 13
                                    horizontalAlignment: Text.AlignLeft
                                    wrapMode: Text.WordWrap
                                    width: Math.min(implicitWidth, 220)
                                }
                            }
                        }

                        CoreText {
                            objectName: "walletSelectStatus_" + delegate.name.replace(/[^A-Za-z0-9_]/g, "_")
                            Layout.fillWidth: true
                            text: delegate.statusText
                            horizontalAlignment: Text.AlignLeft
                            font: delegate.loadState === WalletListModel.Open
                                ? Theme.text.monoCaption.font : Theme.text.caption.font
                            color: delegate.statusColor
                            wrap: false
                            elide: Text.ElideRight
                            visible: text.length > 0
                        }
                    }

                    Item {
                        Layout.preferredWidth: 58
                        Layout.preferredHeight: 36

                        Rectangle {
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            visible: delegate.checked
                            width: 20
                            height: 20
                            radius: width / 2
                            color: Theme.color.orange
                            Icon {
                                anchors.centerIn: parent
                                size: 14
                                source: "image://images/check-bold"
                                color: Theme.color.neutral0
                            }
                        }

                        IconButton {
                            id: actionsButton
                            objectName: "walletSelectActions_" + delegate.name.replace(/[^A-Za-z0-9_]/g, "_")
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            visible: delegate.loadState !== WalletListModel.Loading
                            enabled: visible && !walletController.walletLoadInProgress
                            size: 24
                            iconSize: 18
                            iconSource: "image://images/ellipsis"
                            iconColor: Theme.color.neutral6
                            hoverColor: Theme.color.neutral9
                            Accessible.name: qsTr("Actions for %1").arg(delegate.displayName)
                            background: Rectangle {
                                radius: 6
                                color: actionsButton.hovered ? Theme.color.neutral3 : "transparent"
                            }
                            onClicked: actionsMenu.visible ? actionsMenu.close() : actionsMenu.open()

                            ContextMenu {
                                id: actionsMenu
                                onVisibleChanged: {
                                    if (visible) {
                                        if (root.activeContextMenu && root.activeContextMenu !== actionsMenu) {
                                            root.activeContextMenu.close()
                                        }
                                        root.activeContextMenu = actionsMenu
                                        listView.cancelFlick()
                                    } else if (root.activeContextMenu === actionsMenu) {
                                        root.activeContextMenu = null
                                    }
                                }
                                Component.onDestruction: {
                                    if (root.activeContextMenu === actionsMenu) root.activeContextMenu = null
                                }
                                closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
                                backgroundColor: Theme.color.neutral2
                                // Close immediately before the owning row can be removed or reordered.
                                exit: null
                                objectName: "walletSelectActionsMenu_" + delegate.name.replace(/[^A-Za-z0-9_]/g, "_")
                                y: actionsButton.height
                                x: actionsButton.width - width
                                ContextMenuButton {
                                    objectName: "walletSelectOpen_" + delegate.name.replace(/[^A-Za-z0-9_]/g, "_")
                                    visible: delegate.loadState === WalletListModel.Closed || delegate.loadState === WalletListModel.LoadError
                                    text: qsTr("Open wallet")
                                    iconSource: "image://images/arrow-diagonal-square"
                                    onTriggered: {
                                        actionsMenu.close()
                                        delegate.clicked()
                                    }
                                }
                                ContextMenuButton {
                                    objectName: "walletSelectSettings_" + delegate.name.replace(/[^A-Za-z0-9_]/g, "_")
                                    visible: delegate.loadState === WalletListModel.Open
                                    text: qsTr("Wallet settings")
                                    iconSource: "image://images/gear"
                                    onTriggered: {
                                        actionsMenu.close()
                                        root.close()
                                        root.walletSettingsRequested(delegate.name, delegate.format)
                                    }
                                }
                                ContextMenuButton {
                                    objectName: "walletSelectClose_" + delegate.name.replace(/[^A-Za-z0-9_]/g, "_")
                                    visible: delegate.loadState === WalletListModel.Open
                                    text: qsTr("Close wallet")
                                    iconSource: "image://images/cross"
                                    onTriggered: {
                                        actionsMenu.close()
                                        root.closeLoadedWallet(delegate.name)
                                    }
                                }
                            }
                        }

                        BusyIndicator {
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            width: 24
                            height: 24
                            running: delegate.loadState === WalletListModel.Loading
                            visible: running
                        }
                    }
                }

                Connections {
                    target: root
                    function onAboutToHide() { actionsMenu.close() }
                }

                onClicked: {
                    if (walletController.walletLoadInProgress) {
                        return
                    }
                    walletController.setSelectedWallet(name, format)
                    if (walletController.isWalletOpen(name)) {
                        root.close()
                    }
                    // Otherwise: stay open until walletLoadSucceeded or
                    // walletMigrationRequired arrives via Connections above.
                }
            }
        }

        ContextMenuDivider {
            horizontalInset: 0
            verticalMargin: 3
        }
        ContextMenuButton {
            objectName: "walletSelectAddWalletButton"
            text: qsTr("Add wallet")
            textColor: Theme.color.orange
            hoverTextColor: Theme.color.orange
            iconSource: "image://images/plus-filled"
            enabled: !walletController.walletLoadInProgress
            onTriggered: {
                root.addWallet()
                root.close()
            }
        }
    }
}
