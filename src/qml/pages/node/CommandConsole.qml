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

    // Theme-aware palette for model-generated inline spans (welcome links,
    // warning text, and JSON keys). Row-level colours are applied by QML so the
    // output follows the design-system tokens.
    readonly property color consoleRequestColor: Theme.color.blue
    readonly property color consoleReplyColor:   Theme.color.neutral9
    readonly property color consoleTimeColor:    Theme.color.neutral7
    readonly property color consoleKeyColor:     Theme.dark ? "#98C379" : "#3A7D2C"
    readonly property int minimumOutputFontPixelSize: 10
    readonly property int maximumOutputFontPixelSize: 18
    property int outputFontPixelSize: Theme.text.caption.pixelSize
    property bool searchMode: false
    property string commandDraft: ""
    property string searchDraft: ""

    // True while this view's tab is the selected one. As a persistent StackLayout
    // child, the console is never destroyed on tab changes, and its autocomplete
    // Popup renders in the window overlay layer, so it is not hidden along with
    // the view. The embedder binds this to its tab's checked state; when the tab
    // is left, dismiss the popup so it cannot linger over the newly selected tab.
    property bool tabActive: true
    onTabActiveChanged: if (!tabActive) autocompletePopup.close()

    function _pushPalette() {
        rpcConsoleModel.requestColor = consoleRequestColor
        rpcConsoleModel.replyColor   = consoleReplyColor
        rpcConsoleModel.errorColor   = Theme.color.red
        rpcConsoleModel.keyColor     = consoleKeyColor
    }
    // Focus the command input when the console becomes the active view, so the
    // user can type immediately. Mirrors Core's RPCConsole, which focuses its
    // line edit when the Console tab is activated. Desktop only: on mobile this
    // would raise the on-screen keyboard over the output before the user chose to
    // type. onVisibleChanged covers the desktop nav-bar tab (a StackLayout child
    // whose visibility toggles); the deferred check in onCompleted covers being
    // pushed onto a StackView, where visible is already true at completion. The
    // deferral lets a StackLayout apply its hidden-tab visibility first, so an
    // inactive tab cannot steal focus at startup.
    function focusInput() {
        if (AppMode.isDesktop)
            inputField.forceActiveFocus()
    }
    onVisibleChanged: if (visible) focusInput()

    Component.onCompleted: {
        _pushPalette()
        rpcConsoleModel.ensureWelcomeMessage()
        Qt.callLater(function() { if (root && root.visible) root.focusInput() })
    }
    Connections {
        target: Theme
        function onDarkChanged() { root._pushPalette() }
    }

    Shortcut {
        enabled: root.visible
        sequence: Qt.platform.os === "osx" ? "Meta+L" : "Ctrl+L"
        onActivated: rpcConsoleModel.clear()
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
        property bool switchingInputMode: false
    }

    property bool showHeader: true

    header: NavigationBar2 {
        visible: root.showHeader
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
        leftColumnSample: "00:00:00"
        rightColumnRole: ""
        categoryRole: "category"
        fontPixelSize: root.outputFontPixelSize
        fontFamily: Theme.text.caption.family
        fontStyleName: Theme.text.caption.styleName
        textLineHeight: Math.round(root.outputFontPixelSize * 1.4)
        contentColor: Theme.color.neutral9
        leftColumnColor: root.consoleTimeColor
        requestContentColor: root.consoleRequestColor
        replyContentColor: root.consoleReplyColor
        errorContentColor: Theme.color.red
        requestLeftColumnColor: root.consoleRequestColor
        replyLeftColumnColor: root.consoleTimeColor
        errorLeftColumnColor: root.consoleTimeColor
        selectionColor: Theme.color.orange
        accessibleName: qsTr("Console output")
        autoScrollToBottom: true
        filterText: root.searchMode ? root.searchDraft : ""
        horizontalPadding: 20
        topPadding: 15
        bottomPadding: 15
        rowSpacing: 5
        columnSpacing: 20
        leftColumnWidth: 60
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
        x: inputField.mapToItem(inputArea, 0, 0).x
        y: -height - 4
        z: 10
        width: Math.min(inputField.width, 300)
        height: Math.min(autocompleteList.contentHeight + 8, 200)
        padding: 4
        // Deliberately NOT CloseOnPressOutside: a real mouse press on the submit
        // button would otherwise close this popup (and clear filteredCommands)
        // before the button's onClicked fires, so the button could never act on
        // the highlighted suggestion. Dismissal on outside taps is handled by the
        // input losing focus (see the TapHandler and inputField.onActiveFocusChanged).
        closePolicy: Popup.CloseOnEscape
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

    component ConsoleIconButton: AbstractButton {
        id: consoleIconButton
        required property url iconSource
        required property string accessibleName

        Layout.preferredWidth: 20
        Layout.preferredHeight: 20
        implicitWidth: 20
        implicitHeight: 20
        padding: 0
        hoverEnabled: AppMode.isDesktop
        focusPolicy: Qt.TabFocus

        Accessible.role: Accessible.Button
        Accessible.name: accessibleName

        background: Item {}

        contentItem: Icon {
            source: consoleIconButton.iconSource
            color: consoleIconButton.enabled ? Theme.color.neutral9 : Theme.color.neutral4
            size: 20
            opacity: consoleIconButton.hovered && consoleIconButton.enabled ? 0.75 : 1
        }

        HoverHandler {
            cursorShape: consoleIconButton.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
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
            leftMargin: 20
            rightMargin: 20
        }
        height: 41
        color: "transparent"

        Rectangle {
            id: inputDivider
            objectName: "consoleInputDivider"
            anchors {
                top: parent.top
                left: parent.left
                right: parent.right
            }
            height: 1
            color: Theme.color.neutral5
        }

        RowLayout {
            id: inputContent
            objectName: "consoleInputContent"
            anchors {
                top: parent.top
                left: parent.left
                right: parent.right
                topMargin: 11
                leftMargin: 55
            }
            height: 20
            spacing: 5

            Icon {
                objectName: "consolePromptIcon"
                source: root.searchMode ? "image://images/search" : "image://images/caret-right"
                color: Theme.color.neutral9
                size: 20
                Layout.preferredWidth: 20
                Layout.preferredHeight: 20
                Layout.alignment: Qt.AlignVCenter
            }

            TextField {
                id: inputField
                objectName: "consoleInput"
                Layout.fillWidth: true
                Layout.preferredHeight: 20
                font: Theme.text.caption.font
                color: Theme.color.neutral9
                placeholderText: root.searchMode ? qsTr("Search...") : qsTr("Enter command...")
                placeholderTextColor: Theme.color.neutral6
                leftPadding: 0
                rightPadding: 0
                topPadding: 0
                bottomPadding: 0
                background: Item {}

                // Enter accepts the highlighted autocomplete suggestion when the
                // popup is open; otherwise it submits the command. This mirrors the
                // Tab behaviour and avoids submitting the raw half-typed text while a
                // completion is highlighted.
                Keys.onReturnPressed: root.acceptHighlightedOrSubmit(event)
                Keys.onEnterPressed: root.acceptHighlightedOrSubmit(event)

                // Up/Down: navigate autocomplete when popup is open,
                // otherwise browse command history.
                Keys.onUpPressed: {
                    if (!root.searchMode) {
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
                }
                Keys.onDownPressed: {
                    if (!root.searchMode) {
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
                }

                // Tab key: accept the top autocomplete suggestion.
                Keys.onTabPressed: {
                    if (!root.searchMode && autocompletePopup.visible && filteredCommands.length > 0) {
                        applySuggestion(filteredCommands[autocompleteIndex])
                        event.accepted = true
                    }
                }

                onTextChanged: {
                    if (internal.switchingInputMode) return
                    if (root.searchMode) {
                        root.searchDraft = inputField.text
                        filteredCommands = []
                        autocompletePopup.close()
                        return
                    }
                    root.commandDraft = inputField.text
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

            RowLayout {
                id: inputActions
                objectName: "consoleInputActions"
                spacing: 5
                Layout.preferredWidth: 95
                Layout.preferredHeight: 20
                Layout.alignment: Qt.AlignVCenter

                ConsoleIconButton {
                    objectName: "consoleModeToggleButton"
                    iconSource: root.searchMode ? "image://images/console" : "image://images/search"
                    accessibleName: root.searchMode ? qsTr("Switch to command input") : qsTr("Search console output")
                    onClicked: root.toggleSearchMode()
                }

                ConsoleIconButton {
                    objectName: "consoleFontIncreaseButton"
                    iconSource: "image://images/plus"
                    accessibleName: qsTr("Increase console text size")
                    enabled: root.outputFontPixelSize < root.maximumOutputFontPixelSize
                    onClicked: root.changeOutputFontSize(1)
                }

                ConsoleIconButton {
                    objectName: "consoleFontDecreaseButton"
                    iconSource: "image://images/minus"
                    accessibleName: qsTr("Decrease console text size")
                    enabled: root.outputFontPixelSize > root.minimumOutputFontPixelSize
                    onClicked: root.changeOutputFontSize(-1)
                }

                ConsoleIconButton {
                    objectName: "consoleClearButton"
                    iconSource: "image://images/cross"
                    accessibleName: qsTr("Clear console input or output")
                    onClicked: root.clearInputOrOutput()
                }
            }
        }
    }

    // Filtered autocomplete model. Note: list<string> requires Qt 6.5+;
    // use var for compatibility with Qt 6.2.
    property var filteredCommands: []
    property int autocompleteIndex: 0

    function updateFilteredCommands() {
        if (root.searchMode) {
            filteredCommands = []
            autocompletePopup.close()
            return
        }
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
            if (inputField.contains(inputField.mapFromItem(root, point.position.x, point.position.y)))
                return
            // Keep focus when tapping any control in the console input bar.
            if (inputArea.contains(inputArea.mapFromItem(root, point.position.x, point.position.y)))
                return
            inputField.focus = false
        }
    }

    function toggleSearchMode() {
        if (root.searchMode) {
            root.searchDraft = inputField.text
        } else {
            root.commandDraft = inputField.text
            filteredCommands = []
            autocompletePopup.close()
        }
        internal.switchingInputMode = true
        root.searchMode = !root.searchMode
        inputField.text = root.searchMode ? root.searchDraft : root.commandDraft
        internal.switchingInputMode = false
        inputField.forceActiveFocus()
    }

    function changeOutputFontSize(delta) {
        root.outputFontPixelSize = Math.max(root.minimumOutputFontPixelSize,
                                            Math.min(root.maximumOutputFontPixelSize,
                                                     root.outputFontPixelSize + delta))
        inputField.forceActiveFocus()
    }

    function clearInputOrOutput() {
        if (inputField.text.length > 0) {
            inputField.text = ""
            inputField.forceActiveFocus()
            return
        }
        rpcConsoleModel.clear()
        inputField.forceActiveFocus()
    }

    // Run the highlighted autocomplete suggestion when the popup is open,
    // otherwise submit whatever is typed. Shared by Enter and test automation;
    // Tab only fills the suggestion in (for adding arguments). To run a different
    // command, dismiss the menu first (click away or type past the matches).
    function runHighlightedOrSubmit() {
        if (root.searchMode) return
        if (autocompletePopup.visible && filteredCommands.length > 0) {
            inputField.text = filteredCommands[autocompleteIndex]
            autocompletePopup.close()
        }
        submitCommand()
    }

    function acceptHighlightedOrSubmit(event) {
        runHighlightedOrSubmit()
        event.accepted = true
    }

    function submitCommand() {
        var cmd = inputField.text.trim()
        if (cmd.length === 0) return
        // Only clear the input if the command was accepted. submitCommand()
        // returns false when another command is still executing, so a refused
        // command stays in the field instead of silently vanishing.
        if (rpcConsoleModel.submitCommand(cmd)) {
            inputField.text = ""
            filteredCommands = []
        }
    }
}
