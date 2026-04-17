// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0
import "../../controls"
import "../../components"

Page {
    id: root
    objectName: "commandConsole"
    signal back()
    background: null
    clip: true

    // Theme-aware palette — pushed into RpcConsoleModel so it renders rows in
    // the currently-active colours. Already-rendered rows keep their baked-in
    // colours when the theme toggles, which matches the Qt GUI console.
    readonly property color consoleRequestColor: Theme.dark ? "#888888" : "#666666"
    readonly property color consoleReplyColor:   Theme.dark ? "#CCCCCC" : "#333333"
    readonly property color consoleKeyColor:     Theme.dark ? "#98C379" : "#3A7D2C"

    function _pushPalette() {
        rpcConsoleModel.requestColor = consoleRequestColor
        rpcConsoleModel.replyColor   = consoleReplyColor
        rpcConsoleModel.errorColor   = Theme.color.red
        rpcConsoleModel.keyColor     = consoleKeyColor
    }
    Component.onCompleted: _pushPalette()
    Connections {
        target: Theme
        function onDarkChanged() { root._pushPalette() }
    }

    // Exposed for E2E output-content verification.
    property int outputCount: rpcConsoleModel.outputModel.count

    // Exposed for E2E execution-complete detection.
    property bool executing: rpcConsoleModel.executing

    // Guard flag: true while Up/Down navigation is setting inputField.text
    // programmatically, so onTextChanged does not reset the history cursor.
    // Kept in a QtObject to avoid leaking this internal flag into the public API.
    QtObject {
        id: internal
        property bool navigatingHistory: false
    }

    header: NavigationBar2 {
        leftItem: NavButton {
            objectName: "consoleBackButton"
            iconSource: "image://images/caret-left"
            text: qsTr("Back")
            onClicked: root.back()
        }
        centerItem: Header {
            headerBold: true
            headerSize: 18
            header: qsTr("Console")
        }
        TapHandler {
            grabPermissions: PointerHandler.TakeOverForbidden
            onTapped: inputField.focus = false
        }
    }

    // Header scrolls with content (hint + security warning).
    Component {
        id: consoleHeaderComponent
        Column {
            spacing: 4

            Text {
                width: parent.width
                text: qsTr("Use ↑↓ arrows to navigate history. Type <b>help</b> for an overview of available commands. Type <b>help-console</b> for console syntax help.")
                font.family: "Inter"
                font.pixelSize: 12
                color: Theme.color.neutral6
                wrapMode: Text.WordWrap
                textFormat: Text.RichText
                bottomPadding: 4
            }

            Text {
                width: parent.width
                text: qsTr("<b>WARNING:</b> Scammers and thieves will request that you type commands here to steal your coins. Do not type any commands unless you fully understand them.")
                font.family: "Inter"
                font.pixelSize: 12
                color: Theme.color.red
                wrapMode: Text.WordWrap
                textFormat: Text.RichText
                bottomPadding: 8
            }
        }
    }

    // Output area — MonospaceOutputView (Flickable + Column + Repeater)
    // keeps contentHeight exact (no ListView estimation) and gives each
    // row a TextEdit for single-row select + copy.
    MonospaceOutputView {
        id: outputView
        objectName: "consoleOutputArea"
        anchors {
            top: parent.top
            left: parent.left
            right: parent.right
            bottom: inputArea.top
            bottomMargin: 4
        }

        listModel: rpcConsoleModel.outputModel
        contentRole: "content"
        contentTextFormat: Text.RichText
        leftColumnRole: "timestamp"
        leftColumnSample: "[00:00:00]"
        rightColumnRole: ""
        fontPixelSize: 12
        contentColor: Theme.color.neutral9
        leftColumnColor: Theme.color.neutral5
        selectionColor: Theme.color.orange
        accessibleName: qsTr("Console output")
        autoScrollToBottom: true
        header: consoleHeaderComponent
    }

    // Autocomplete popup (anchored above the input area).
    // Sizing: width matches the input field; height hugs the suggestion
    // ListView's contentHeight so the opaque background cannot spill over
    // the output area (fixes the "display disappears on typing" regression).
    // z is raised so the popup reliably renders above neighbours.
    Popup {
        id: autocompletePopup
        objectName: "consoleAutocompletePopup"
        parent: inputArea
        x: inputField.x
        y: -height - 4
        z: 10
        width: Math.min(inputField.width, 300)
        height: Math.min(autocompleteList.contentHeight + 8, 200)
        padding: 4
        closePolicy: Popup.CloseOnPressOutside | Popup.CloseOnEscape
        onClosed: filteredCommands = []
        background: Rectangle {
            color: Theme.color.neutral1
            border.color: Theme.color.neutral3
            radius: 4
        }

        ListView {
            id: autocompleteList
            objectName: "consoleAutocompleteList"
            anchors.fill: parent
            clip: true
            currentIndex: autocompleteIndex
            highlightFollowsCurrentItem: true
            model: filteredCommands
            delegate: ItemDelegate {
                required property string modelData
                required property int index
                objectName: "consoleAutocomplete_" + index
                width: autocompleteList.width
                height: 28
                leftPadding: 8
                rightPadding: 8
                background: Rectangle {
                    color: parent.hovered ? Theme.color.neutral2 : "transparent"
                    radius: 2
                }
                contentItem: Text {
                    text: modelData
                    font.family: "monospace"
                    font.pixelSize: 13
                    color: index === autocompleteIndex ? Theme.color.orange : Theme.color.neutral9
                    elide: Text.ElideRight
                }
                // Apply the suggestion without stealing focus from the
                // input field — per MarnixCroes PR #540 feedback.
                onClicked: applySuggestion(modelData)
            }
        }
    }

    // Apply an autocomplete suggestion without disturbing the input field's
    // focus / selection state.
    function applySuggestion(cmd) {
        inputField.text = cmd + " "
        inputField.cursorPosition = inputField.text.length
        autocompletePopup.close()
        inputField.forceActiveFocus()
    }

    // Command input area
    Rectangle {
        id: inputArea
        objectName: "consoleInputRow"
        anchors {
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }
        height: 48
        color: "transparent"

        Rectangle {
            anchors {
                top: parent.top
                left: parent.left
                right: parent.right
                leftMargin: 12
                rightMargin: 12
            }
            height: 1
            color: Theme.color.neutral4
        }

        RowLayout {
            anchors {
                fill: parent
                leftMargin: 12
                rightMargin: 12
            }
            spacing: 8

            Icon {
                source: "image://images/console"
                color: Theme.color.neutral5
                size: 20
                Layout.alignment: Qt.AlignVCenter
            }

            TextField {
                id: inputField
                objectName: "consoleInput"
                Layout.fillWidth: true
                Layout.fillHeight: true
                font.family: "monospace"
                font.pixelSize: 14
                color: Theme.color.neutral9
                placeholderText: qsTr("Enter command...")
                placeholderTextColor: Theme.color.neutral5
                leftPadding: 0
                rightPadding: 0
                background: Item {}

                // Accept Enter key to submit.
                Keys.onReturnPressed: submitCommand()
                Keys.onEnterPressed: submitCommand()

                // Up/Down: navigate autocomplete when popup is open,
                // otherwise browse command history.
                Keys.onUpPressed: {
                    if (autocompletePopup.visible && filteredCommands.length > 0) {
                        autocompleteIndex = Math.max(0, autocompleteIndex - 1)
                    } else {
                        internal.navigatingHistory = true
                        var result = rpcConsoleModel.browseHistory(1, inputField.text)
                        inputField.text = result
                        inputField.cursorPosition = result.length
                        internal.navigatingHistory = false
                    }
                }
                Keys.onDownPressed: {
                    if (autocompletePopup.visible && filteredCommands.length > 0) {
                        autocompleteIndex = Math.min(filteredCommands.length - 1, autocompleteIndex + 1)
                    } else {
                        internal.navigatingHistory = true
                        var result = rpcConsoleModel.browseHistory(-1, inputField.text)
                        inputField.text = result
                        inputField.cursorPosition = result.length
                        internal.navigatingHistory = false
                    }
                }

                // Tab key: accept the top autocomplete suggestion.
                Keys.onTabPressed: {
                    if (autocompletePopup.visible && filteredCommands.length > 0) {
                        applySuggestion(filteredCommands[autocompleteIndex])
                        event.accepted = true
                    }
                }

                onTextChanged: {
                    if (!internal.navigatingHistory) {
                        rpcConsoleModel.resetHistoryNavigation()
                    }
                    updateFilteredCommands()
                }

                // Auto-select existing text when the field gains focus
                // (MarnixCroes PR #540 feedback). Close the popup on blur.
                onActiveFocusChanged: {
                    if (activeFocus) {
                        selectAll()
                    } else {
                        autocompletePopup.close()
                    }
                }
            }

            AbstractButton {
                id: submitButton
                objectName: "consoleSubmitButton"
                Layout.preferredWidth: 32
                Layout.preferredHeight: 32
                Layout.alignment: Qt.AlignVCenter
                hoverEnabled: true
                enabled: !rpcConsoleModel.executing && inputField.text.trim().length > 0
                onClicked: submitCommand()

                background: Rectangle {
                    id: submitBg
                    radius: 5
                    color: "transparent"
                    Behavior on color { ColorAnimation { duration: 150 } }
                }

                contentItem: Icon {
                    id: submitIcon
                    source: "image://images/caret-right"
                    size: 20
                    color: submitButton.enabled ? Theme.color.neutral9 : Theme.color.neutral4
                    Behavior on color { ColorAnimation { duration: 150 } }
                }

                MouseArea {
                    id: submitHoverArea
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.NoButton
                    cursorShape: submitButton.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                }

                states: [
                    State {
                        name: "HOVER"; when: submitButton.hovered && submitButton.enabled
                        PropertyChanges { target: submitBg; color: Theme.color.neutral2 }
                    },
                    State {
                        name: "PRESSED"; when: submitButton.pressed
                        PropertyChanges { target: submitBg; color: Theme.color.neutral3 }
                    }
                ]
            }
        }
    }

    // Filtered autocomplete model. Note: list<string> requires Qt 6.5+;
    // use var for compatibility with Qt 6.2.
    property var filteredCommands: []
    property int autocompleteIndex: 0

    function updateFilteredCommands() {
        // Guard: availableCommands is empty until the node is initialised.
        // Calling this before init used to throw and abort the textChanged
        // handler, which (combined with the oversized popup) made the
        // whole console appear blank on first keystroke.
        var cmds = rpcConsoleModel ? rpcConsoleModel.availableCommands : null
        if (!cmds || cmds.length === 0) {
            filteredCommands = []
            autocompletePopup.close()
            return
        }
        var t = inputField.text.toLowerCase()
        if (t.length === 0) {
            filteredCommands = []
            autocompletePopup.close()
            return
        }
        var matches = []
        for (var i = 0; i < cmds.length && matches.length < 10; ++i) {
            if (cmds[i].toLowerCase().startsWith(t)) {
                matches.push(cmds[i])
            }
        }
        filteredCommands = matches
        autocompleteIndex = 0
        if (matches.length > 0 && !internal.navigatingHistory) {
            autocompletePopup.open()
        } else {
            autocompletePopup.close()
        }
    }

    // Unfocus inputField when the user taps anywhere outside it.
    // TakeOverForbidden gives a passive grab on every tap in the page —
    // interactive children (TextField, Button, TextEdit, Flickable) keep their
    // exclusive grabs and work normally; we just observe and react.
    TapHandler {
        grabPermissions: PointerHandler.TakeOverForbidden
        onTapped: {
            var pos = inputField.mapFromItem(root, point.position.x, point.position.y)
            if (!inputField.contains(pos)) {
                inputField.focus = false
            }
        }
    }

    function submitCommand() {
        var cmd = inputField.text.trim()
        if (cmd.length === 0) return
        rpcConsoleModel.submitCommand(cmd)
        inputField.text = ""
        filteredCommands = []
    }
}
