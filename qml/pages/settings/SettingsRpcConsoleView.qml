pragma ComponentBehavior: Bound

// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0

import "../../controls"
import "../../components"
import "../node" as NodePages

SettingsPage {
    id: root

    objectName: "rpcConsoleSettingsPage"
    title: qsTr("RPC console")
    showBackButton: false
    maximumContentWidth: width
    contentSpacing: 20
    contentBottomPadding: 20

    property string walletName: ""
    property bool warningVisible: true
    readonly property alias consoleItem: rpcConsole

    component FontSizeButton: AbstractButton {
        id: fontSizeButton

        required property string accessibleName
        required property int labelPixelSize

        implicitWidth: 36
        implicitHeight: 36
        padding: 0
        hoverEnabled: AppMode.isDesktop
        focusPolicy: Qt.TabFocus

        Accessible.role: Accessible.Button
        Accessible.name: accessibleName

        background: Rectangle {
            color: fontSizeButton.hovered || fontSizeButton.pressed
                ? Theme.color.neutral2
                : "transparent"
            radius: 8
        }

        FocusBorder {
            objectName: fontSizeButton.objectName + "FocusBorder"
            visible: fontSizeButton.activeFocus
            borderRadius: 12
            z: 1
        }

        contentItem: CoreText {
            objectName: fontSizeButton.objectName + "Label"
            text: "A"
            color: fontSizeButton.enabled ? Theme.color.neutral9 : Theme.color.neutral4
            font.family: Theme.text.family
            font.pixelSize: fontSizeButton.labelPixelSize
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }

        HoverHandler {
            cursorShape: fontSizeButton.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        }
    }

    PageHeading {
        id: pageHeading
        objectName: "rpcConsolePageHeading"
        Layout.fillWidth: true
        description: qsTr("Execute RPC commands and inspect their responses.")
    }

    ToastBanner {
        id: warningBanner
        objectName: "rpcConsoleWarningBanner"
        Layout.fillWidth: true
        visible: root.warningVisible
        iconSource: "image://images/alert-filled"
        iconColor: Theme.color.red
        textColor: Theme.color.neutral9
        backgroundColor: Qt.rgba(Theme.color.red.r,
                                 Theme.color.red.g,
                                 Theme.color.red.b,
                                 0.12)
        showsCloseButton: true
        text: qsTr("Beware of scammers who may ask you to enter commands here to steal your funds. Only enter commands you fully understand.")
        onDismissed: root.warningVisible = false
    }

    RowLayout {
        id: toolbar
        objectName: "rpcConsoleToolbar"
        Layout.fillWidth: true
        spacing: 16

        SearchBar {
            id: searchBar
            objectName: "rpcConsoleSearchBar"
            fieldObjectName: "rpcConsoleSearchField"
            searchIconObjectName: "rpcConsoleSearchIcon"
            clearButtonObjectName: "rpcConsoleSearchClearButton"
            navigationControlObjectName: "rpcConsoleSearchNavigation"
            previousButtonObjectName: "rpcConsoleSearchPreviousButton"
            nextButtonObjectName: "rpcConsoleSearchNextButton"
            Layout.fillWidth: true
            Layout.minimumWidth: 140
            Layout.maximumWidth: implicitWidth
            placeholderText: qsTr("Search console")
            accessibleName: qsTr("Search RPC console output")
            showNavigationButtons: true
            navigationEnabled: rpcConsole.searchResultCount > 0
            nextTabItem: decreaseButton
            onPreviousRequested: rpcConsole.showPreviousSearchResult()
            onNextRequested: rpcConsole.showNextSearchResult()
        }

        Item { Layout.fillWidth: true }

        Control {
            id: fontStepper
            objectName: "consoleFontStepper"
            implicitWidth: 72
            implicitHeight: 36
            padding: 0
            focusPolicy: Qt.NoFocus

            Accessible.role: Accessible.SpinBox
            Accessible.name: qsTr("Console font size")
            Accessible.description: qsTr("%1 pixels").arg(rpcConsole.outputFontPixelSize)

            background: Rectangle {
                color: Theme.color.neutral1
                radius: 8
            }

            contentItem: RowLayout {
                spacing: 0

                FontSizeButton {
                    id: decreaseButton

                    objectName: "consoleFontDecreaseButton"
                    accessibleName: qsTr("Decrease console text size")
                    labelPixelSize: 11
                    enabled: rpcConsole.outputFontPixelSize > rpcConsole.minimumOutputFontPixelSize
                    KeyNavigation.tab: increaseButton
                    KeyNavigation.backtab: searchBar.nextNavigationButton
                    onClicked: rpcConsole.changeOutputFontSize(-1)
                }

                FontSizeButton {
                    id: increaseButton

                    objectName: "consoleFontIncreaseButton"
                    accessibleName: qsTr("Increase console text size")
                    labelPixelSize: 17
                    enabled: rpcConsole.outputFontPixelSize < rpcConsole.maximumOutputFontPixelSize
                    KeyNavigation.backtab: decreaseButton
                    onClicked: rpcConsole.changeOutputFontSize(1)
                }
            }
        }
    }

    NodePages.CommandConsole {
        id: rpcConsole
        objectName: "rpcConsole"
        Layout.fillWidth: true
        Layout.preferredHeight: Math.max(360, root.height - 342)
            + (warningBanner.visible
               ? 0
               : warningBanner.implicitHeight + root.contentSpacing)
        showHeader: false
        tabActive: root.visible
        walletName: root.walletName
        searchText: searchBar.text
    }

    CoreText {
        id: helpFooter
        objectName: "rpcConsoleHelpFooter"
        Layout.fillWidth: true
        Layout.leftMargin: 12
        Layout.rightMargin: 12
        text: qsTr("Use ↑↓ arrows to navigate history. Type help for an overview of available commands. Type help-console for console syntax help.")
        color: Theme.color.neutral7
        font: Theme.text.caption.font
        horizontalAlignment: Text.AlignHCenter
        wrap: true
    }

    Shortcut {
        objectName: "rpcConsoleFindShortcut"
        enabled: root.visible
        sequences: [StandardKey.Find]
        onActivated: {
            searchBar.focusSearch()
            searchBar.selectAll()
        }
    }

    Component.onCompleted: {
        root.pageHeader.objectName = "rpcConsoleHeader"
        root.contentLayout.objectName = "rpcConsoleContentLayout"
    }
}
