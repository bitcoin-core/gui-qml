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

    // Named color properties — theme-aware, single source of truth.
    readonly property color consoleRequestColor: Theme.dark ? "#888888" : "#666666"
    readonly property color consoleReplyColor:   Theme.dark ? "#CCCCCC" : "#333333"
    readonly property color consoleKeyColor:     Theme.dark ? "#98C379" : "#3A7D2C"

    // Exposed for E2E output-content verification.
    property int outputCount: outputModel.count

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

    // Measure the timestamp column width once for all output delegates.
    TextMetrics {
        id: timestampMetrics
        font.family: "monospace"
        font.pixelSize: 12
        text: "[00:00:00]"
    }

    // Output area — ListView virtualizes delegates so only visible rows are
    // instantiated, preventing layout churn during long console sessions.
    ListView {
        id: outputList
        objectName: "consoleOutputArea"
        anchors {
            top: parent.top
            left: parent.left
            right: parent.right
            bottom: inputArea.top
            bottomMargin: 4
        }
        clip: true
        spacing: 2
        // 16 px left/right padding mirrors the old Column { padding: 16 } layout.
        leftMargin: 16
        rightMargin: 16
        topMargin: 16
        bottomMargin: 0

        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        // Static header: hint + security warning — scrolls with content so it
        // naturally disappears as the output grows.
        header: Column {
            // Width = ListView width minus its left+right margins (16+16 = 32).
            width: outputList.width - 32
            spacing: 4

            // Welcome / hint text
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

            // Security warning
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

        model: ListModel { id: outputModel }

        delegate: RowLayout {
            required property string timestamp
            required property string content
            // Same effective content width as old outputColumn.width - 32.
            width: outputList.width - 32
            spacing: 16

            Text {
                text: timestamp
                font.family: "monospace"
                font.pixelSize: 12
                color: Theme.color.neutral5
                Layout.preferredWidth: timestampMetrics.width
                Layout.alignment: Qt.AlignTop
            }

            TextEdit {
                text: content
                font.family: "monospace"
                font.pixelSize: 12
                // color applies to unstyled text only; styled content is
                // colored via <span style='color:...'> HTML from onCommandResultReceived.
                color: Theme.color.neutral9
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                wrapMode: Text.WrapAnywhere
                textFormat: Text.RichText
                readOnly: true
                selectByMouse: true
                activeFocusOnPress: true
            }
        }

        // Scroll to bottom after each new row is appended. Qt.callLater defers
        // the call until after the layout pass so positionViewAtEnd() sees the
        // updated content height.
        onCountChanged: Qt.callLater(positionViewAtEnd)
    }

    // Unfocus inputField on any click in the output area. Placed outside the
    // ListView so Flickable's childMouseEventFilter does not interfere.
    // z:1 puts it above the ListView (z:0); mouse.accepted = false passes the
    // press through to the ListView so scrolling still works.
    MouseArea {
        z: 1
        anchors {
            top: parent.top
            left: parent.left
            right: parent.right
            bottom: inputArea.top
            bottomMargin: 4
        }
        onPressed: {
            inputField.focus = false
            mouse.accepted = false
        }
    }

    // Format a CMD_REPLY result: convert \n to <br>, preserve indentation
    // with &nbsp;, and colorize JSON object keys.
    function formatResult(html) {
        var result = html.replace(/\n/g, "<br>")
                         .replace(/(^|<br>)([ ]+)/g, function(_, br, spaces) {
                             return br + spaces.replace(/ /g, "&nbsp;")
                         })
        result = result.replace(/(&quot;)([^&]*)(&quot;)(\s*:)/g,
                     "$1<span style='color:" + root.consoleKeyColor + "'>$2</span>$3$4")
        return result
    }

    // Scroll to bottom whenever a new output line is appended.
    Connections {
        target: rpcConsoleModel
        function onCommandResultReceived(time, category, escapedHtml) {
            var prefix, colorHex
            if (category === RpcConsoleModel.CMD_REQUEST) {
                prefix = "&gt;&gt; "
                colorHex = root.consoleRequestColor
            } else if (category === RpcConsoleModel.CMD_ERROR) {
                prefix = "!! "
                colorHex = Theme.color.red
            } else {
                prefix = ""
                colorHex = root.consoleReplyColor
            }
            var content = (category === RpcConsoleModel.CMD_REPLY)
                ? formatResult(escapedHtml)
                : escapedHtml
            outputModel.append({
                "timestamp": "[" + time + "]",
                "content": "<span style='color:" + colorHex + "'>" + prefix + content + "</span>"
            })
        }
        function onClearRequested() {
            outputModel.clear()
        }
    }

    // Autocomplete popup (anchored above the input area)
    Popup {
        id: autocompletePopup
        objectName: "consoleAutocompletePopup"
        parent: inputArea
        x: 0
        y: -height - 4
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
                    color: Theme.color.neutral9
                    elide: Text.ElideRight
                }
                onClicked: {
                    inputField.text = modelData + " "
                    inputField.forceActiveFocus()
                    inputField.cursorPosition = inputField.text.length
                }
            }
        }
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
        height: 56
        color: Theme.color.neutral1

        RowLayout {
            anchors {
                fill: parent
                leftMargin: 12
                rightMargin: 12
                topMargin: 8
                bottomMargin: 8
            }
            spacing: 8

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
                leftPadding: 10
                rightPadding: 10
                background: Rectangle {
                    color: Theme.color.neutral2
                    border.color: inputField.activeFocus ? Theme.color.orange : Theme.color.neutral4
                    border.width: 1
                    radius: 4
                }

                // Accept Enter key to submit.
                Keys.onReturnPressed: submitCommand()
                Keys.onEnterPressed: submitCommand()

                // History navigation with Up/Down arrows.
                Keys.onUpPressed: {
                    internal.navigatingHistory = true
                    var result = rpcConsoleModel.browseHistory(1, inputField.text)
                    inputField.text = result
                    inputField.cursorPosition = result.length
                    internal.navigatingHistory = false
                }
                Keys.onDownPressed: {
                    internal.navigatingHistory = true
                    var result = rpcConsoleModel.browseHistory(-1, inputField.text)
                    inputField.text = result
                    inputField.cursorPosition = result.length
                    internal.navigatingHistory = false
                }

                // Tab key: accept the top autocomplete suggestion.
                Keys.onTabPressed: {
                    if (autocompletePopup.visible && filteredCommands.length > 0) {
                        inputField.text = filteredCommands[0] + " "
                        inputField.cursorPosition = inputField.text.length
                        autocompletePopup.close()
                        filteredCommands = []
                        event.accepted = true
                    }
                }

                onTextChanged: {
                    if (!internal.navigatingHistory) {
                        rpcConsoleModel.resetHistoryNavigation()
                    }
                    updateFilteredCommands()
                }

                onActiveFocusChanged: {
                    if (!activeFocus) {
                        autocompletePopup.close()
                    }
                }
            }

            ContinueButton {
                id: submitButton
                objectName: "consoleSubmitButton"
                text: qsTr("Run")
                leftPadding: 16
                rightPadding: 16
                enabled: !rpcConsoleModel.executing && inputField.text.trim().length > 0
                onClicked: submitCommand()
            }
        }
    }

    // Filtered autocomplete model. Note: list<string> requires Qt 6.5+;
    // use var for compatibility with Qt 6.2.
    property var filteredCommands: []

    function updateFilteredCommands() {
        var t = inputField.text.toLowerCase()
        if (t.length === 0) {
            filteredCommands = []
            autocompletePopup.close()
            return
        }
        var cmds = rpcConsoleModel.availableCommands
        var matches = []
        for (var i = 0; i < cmds.length && matches.length < 10; ++i) {
            if (cmds[i].toLowerCase().startsWith(t)) {
                matches.push(cmds[i])
            }
        }
        filteredCommands = matches
        if (matches.length > 0 && inputField.activeFocus) {
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
