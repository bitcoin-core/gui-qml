// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import org.bitcoincore.qt 1.0

import "../../controls"
import "../../components"

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
        color: Theme.color.modalOverlay
        Behavior on opacity {
            enabled: Theme.motion.animated
            NumberAnimation { duration: Theme.motion.fade(Theme.motion.exit); easing.type: Theme.motion.easing.enter }
        }
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
            from: Theme.motion.reduced ? 0 : -12
            to: 0
            duration: Theme.motion.travel(Theme.motion.slow)
            easing.type: Theme.motion.easing.enter
        }
        NumberAnimation {
            property: "scale"
            from: Theme.motion.popoverScale
            to: 1
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
            to: Theme.motion.reduced ? 0 : -12
            duration: Theme.motion.travel(Theme.motion.exit)
            easing.type: Theme.motion.easing.exit
        }
        NumberAnimation {
            property: "scale"
            to: Theme.motion.popoverScale
            duration: Theme.motion.travel(Theme.motion.exit)
            easing.type: Theme.motion.easing.exit
        }
    }

    signal walletSettingsRequested(string name, string format)

    signal addWallet()
    signal closeWalletRequested(string name)

    function closeLoadedWallet(name) {
        root.close()
        root.closeWalletRequested(name)
    }

    function focusRow(index) {
        if (index < 0 || index >= listView.count) return
        listView.positionViewAtIndex(index, ListView.Contain)
        const row = listView.itemAtIndex(index)
        if (row) row.forceActiveFocus(Qt.TabFocusReason)
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
        color: Theme.color.surface1
        border.color: Theme.color.menuBorder
        border.width: 1
        radius: Theme.radius.m
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
                textStyle: Theme.text.caption
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
                required property var balanceSatoshi;
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
                        return balance + " " + (optionsModel.displayUnit === BitcoinAmount.SAT
                            ? optionsModel.displayUnitLabelForAmount(balanceSatoshi) : optionsModel.displayUnitLabel)
                    case WalletListModel.Closed:
                    default:
                        return ""
                    }
                }
                readonly property color statusColor: {
                    if (loadState === WalletListModel.LoadError) return Theme.color.dangerText
                    return delegate.checked ? Theme.color.accentText : Theme.color.neutral9
                }
                readonly property color nameColor: delegate.checked ? Theme.color.accentText : Theme.color.neutral9

                objectName: "walletSelectItem_" + name.replace(/[^A-Za-z0-9_]/g, "_")
                width: listView.width
                height: loadState === WalletListModel.Open ? 64 : 48
                checked: loadState === WalletListModel.Open && walletController.selectedWallet.name === name
                enabled: !walletController.walletLoadInProgress
                ButtonGroup.group: buttonGroup
                focusPolicy: Qt.StrongFocus
                leftPadding: 10
                rightPadding: 10
                topPadding: 0
                bottomPadding: 0
                Accessible.role: Accessible.ListItem
                Accessible.name: delegate.displayName
                Accessible.description: delegate.loadState === WalletListModel.Open && optionsModel.hideAmounts
                    ? qsTr("Hidden amount") : delegate.statusText
                Accessible.selected: delegate.checked

                Keys.onUpPressed: root.focusRow(delegate.index - 1)
                Keys.onDownPressed: root.focusRow(delegate.index + 1)

                background: StateBackground {
                    radius: Theme.radius.sidebarRow
                    stateColor: delegate.down ? Theme.color.surfacePressed
                        : delegate.checked ? Theme.color.surfaceSelected
                        : delegate.hovered ? Theme.color.surfaceHover : "transparent"
                    Separator {
                        visible: delegate.loadState === WalletListModel.Open
                            && delegate.index > 0 && !delegate.checked && !delegate.hovered
                        width: parent.width
                    }

                    FocusBorder {
                        objectName: delegate.objectName + "FocusBorder"
                        visible: delegate.visualFocus
                        targetRadius: parent.radius
                        topMargin: 2
                        bottomMargin: 2
                        leftMargin: 2
                        rightMargin: 2
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
                        color: delegate.nameColor
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
                            textStyle: Theme.text.menuItemStrong
                            color: delegate.nameColor
                            wrap: false
                            elide: Text.ElideRight

                            HoverToolTip {
                                text: delegate.displayName
                                visible: delegate.hovered && nameText.truncated
                            }
                        }

                        BitcoinAmountDisplayLabel {
                            objectName: "walletSelectStatus_" + delegate.name.replace(/[^A-Za-z0-9_]/g, "_")
                            Layout.fillWidth: true
                            text: delegate.statusText
                            horizontalAlignment: Text.AlignLeft
                            textStyle: delegate.loadState === WalletListModel.Open
                                ? Theme.text.amountCaption : Theme.text.caption
                            color: delegate.statusColor
                            wrap: false
                            elide: Text.ElideRight
                            visible: text.length > 0
                            animateUnitChanges: delegate.loadState === WalletListModel.Open
                            hidesAmount: delegate.loadState === WalletListModel.Open
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
                            color: Theme.color.accent
                            Icon {
                                anchors.centerIn: parent
                                size: 14
                                source: "image://images/check-bold"
                                color: Theme.color.background
                            }
                        }

                        OverflowMenuButton {
                            id: actionsButton
                            objectName: "walletSelectActions_" + delegate.name.replace(/[^A-Za-z0-9_]/g, "_")
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            visible: delegate.loadState !== WalletListModel.Loading
                            enabled: visible && !walletController.walletLoadInProgress
                            size: Theme.size.controlSmall
                            iconColor: delegate.nameColor
                            activeIconColor: Theme.color.neutral9
                            backgroundColor: "transparent"
                            hoverBackgroundColor: actionsButton.down ? Theme.color.surfacePressed : Theme.color.surfaceHover
                            Accessible.name: qsTr("Actions for %1").arg(delegate.displayName)
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
                                // Modeless, so a second press on the trigger reaches it and toggles.
                                modal: false
                                closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
                                backgroundColor: Theme.color.surface2
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
                                    text: qsTr("Close wallet…")
                                    iconSource: "image://images/cross"
                                    onTriggered: {
                                        actionsMenu.close()
                                        root.closeLoadedWallet(delegate.name)
                                    }
                                }
                            }
                        }

                        SpinningIndicator {
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            size: Theme.size.icon.l
                            color: Theme.color.neutral7
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
            text: qsTr("Add wallet…")
            textColor: Theme.color.accentText
            hoverTextColor: Theme.color.accentText
            iconSource: "image://images/plus-filled"
            enabled: !walletController.walletLoadInProgress
            onTriggered: {
                root.addWallet()
                root.close()
            }
        }
    }
}
