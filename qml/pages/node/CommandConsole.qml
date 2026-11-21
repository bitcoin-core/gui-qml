// Copyright (c) 2024-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0
import "../../controls"

Page {
    id: root
    objectName: "commandConsole"
    signal back()
    background: Rectangle {
        color: Theme.color.surface1
        radius: Theme.radius.card
    }
    clip: true

    // Theme-aware palette for model-generated inline spans (welcome links and
    // JSON keys). Row-level colours are applied by QML so the
    // output follows the design-system tokens.
    readonly property color consoleRequestColor: Theme.color.infoText
    readonly property color consoleReplyColor:   Theme.color.neutral9
    readonly property color consoleTimeColor:    Theme.color.neutral7
    readonly property color consoleKeyColor:     Theme.color.syntaxKey
    readonly property int minimumOutputFontPixelSize: 10
    readonly property int maximumOutputFontPixelSize: 18
    property int outputFontPixelSize: Theme.text.caption.pixelSize
    property string searchText: ""
    property string commandDraft: ""
    readonly property alias searchResultCount: outputView.searchResultCount
    readonly property alias currentSearchResultIndex: outputView.currentSearchResultIndex

    function showNextSearchResult() {
        outputView.showNextSearchResult()
    }

    function showPreviousSearchResult() {
        outputView.showPreviousSearchResult()
    }

    // True while this view's tab is the selected one. As a persistent StackLayout
    // child, the console is never destroyed on tab changes, and its autocomplete
    // Popup renders in the window overlay layer, so it is not hidden along with
    // the view. The embedder binds this to its tab's checked state; when the tab
    // is left, dismiss the popup so it cannot linger over the newly selected tab.
    property bool tabActive: true
    onTabActiveChanged: if (!tabActive) autocompletePopup.close()

    Binding { target: ApplicationModels.rpcConsoleModel; property: "requestColor"; value: root.consoleRequestColor }
    Binding { target: ApplicationModels.rpcConsoleModel; property: "replyColor"; value: root.consoleReplyColor }
    Binding { target: ApplicationModels.rpcConsoleModel; property: "errorColor"; value: Theme.color.dangerText }
    Binding { target: ApplicationModels.rpcConsoleModel; property: "keyColor"; value: root.consoleKeyColor }
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
        Qt.callLater(function() { if (root.visible) root.focusInput() })
    }

    readonly property string clearOutputShortcut: Qt.platform.os === "osx" ? "Meta+L" : "Ctrl+L"
    Shortcut {
        enabled: root.visible && !autocompletePopup.visible
        sequence: root.clearOutputShortcut
        onActivated: ApplicationModels.rpcConsoleModel.clear()
    }

    Shortcut {
        enabled: root.visible
        sequence: Qt.platform.os === "osx" ? "Meta+Shift+C" : "Ctrl+Shift+C"
        onActivated: outputView.copyAll()
    }

    // Exposed for E2E output-content verification.
    property int outputCount: ApplicationModels.rpcConsoleModel.outputModel.count

    // Exposed for E2E execution-complete detection.
    property bool executing: ApplicationModels.rpcConsoleModel.executing

    // Guard flag: true while Up/Down navigation is setting inputField.text
    // programmatically, so onTextChanged does not reset the history cursor.
    // Kept in a QtObject to avoid leaking this internal flag into the public API.
    QtObject {
        id: internal
        property bool navigatingHistory: false
    }

    property bool showHeader: true

    // The default wallet has an empty name. Endpoint kind is a separate fact.
    property string walletName: ""
    property bool walletContext: false
    property bool targetAvailable: true

    header: NavigationBar2 {
        visible: root.showHeader
        showBackButton: true
        backButtonObjectName: "consoleBackButton"
        onBackClicked: root.back()
        title: qsTr("Console")
        TapHandler {
            grabPermissions: PointerHandler.TakeOverForbidden
            onTapped: inputField.focus = false
        }
    }

    // Output uses virtualized, bounded text chunks. Search also includes rows
    // without instantiated delegates; each visible chunk supports select/copy.
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

        header: Component {
            Text {
                text: root.targetAvailable ? ApplicationModels.rpcConsoleModel.completionError : qsTr("The selected wallet is not ready for RPC commands.")
                visible: text.length > 0
                height: visible ? implicitHeight : 0
                color: Theme.color.dangerText
                wrapMode: Text.Wrap
                font.pixelSize: root.outputFontPixelSize
            }
        }
        listModel: ApplicationModels.rpcConsoleModel.outputModel
        contentRole: "content"
        contentTextFormat: Text.RichText
        leftColumnRole: "timestamp"
        leftColumnSample: "00:00:00"
        rightColumnRole: ""
        categoryRole: "category"
        fontPixelSize: root.outputFontPixelSize
        textLineHeight: Math.round(root.outputFontPixelSize * 1.4)
        contentColor: Theme.color.neutral9
        leftColumnColor: root.consoleTimeColor
        requestContentColor: root.consoleRequestColor
        replyContentColor: root.consoleReplyColor
        errorContentColor: Theme.color.dangerText
        requestLeftColumnColor: root.consoleRequestColor
        replyLeftColumnColor: root.consoleTimeColor
        errorLeftColumnColor: root.consoleTimeColor
        accessibleName: qsTr("Console output")
        autoScrollToBottom: true
        searchText: root.searchText
        horizontalPadding: 16
        topPadding: 16
        bottomPadding: 16
        rowSpacing: 5
        columnSpacing: 20
        leftColumnWidth: 60
    }

    // Autocomplete menu, aligned to the command field's left edge and styled
    // like the shared application context menus.
    ContextMenu {
        id: autocompletePopup
        objectName: "consoleAutocompletePopup"
        parent: inputArea
        x: inputContent.x + inputField.x
        y: -height - 4
        z: 10
        width: Math.min(inputField.width, 360)
        height: Math.min(autocompleteList.contentHeight + 2 * menuPadding, 228)
        minMenuWidth: 0
        backgroundColor: Theme.color.surface2
        focus: false
        // Modeless: the user keeps typing in the field while it is open.
        modal: false
        margins: 6
        // Deliberately NOT CloseOnPressOutside: a real mouse press on the submit
        // button would otherwise close this popup (and clear filteredCommands)
        // before the button's onClicked fires, so the button could never act on
        // the highlighted suggestion. Dismissal on outside taps is handled by the
        // input losing focus (see the TapHandler and inputField.onActiveFocusChanged).
        closePolicy: Popup.CloseOnEscape
        onClosed: if (root.filteredCommands.length > 0) root.completionDismissed = true

        // Qt's CloseOnEscape popup blocks WindowShortcuts outside its content,
        // even with focus:false. Keep this one intent inside the active menu;
        // the mutually exclusive outer shortcut owns the ordinary view case.
        Shortcut {
            enabled: root.visible && autocompletePopup.visible
            sequence: root.clearOutputShortcut
            onActivated: ApplicationModels.rpcConsoleModel.clear()
        }

        ListView {
            id: autocompleteList
            objectName: "consoleAutocompleteList"
            Layout.preferredWidth: autocompletePopup.width - 2 * autocompletePopup.menuPadding
            Layout.preferredHeight: Math.min(contentHeight, 216)
            clip: true
            currentIndex: root.autocompleteIndex
            highlightFollowsCurrentItem: true
            model: root.filteredCommands
            delegate: ContextMenuButton {
                required property string modelData
                required property int index
                objectName: "consoleAutocomplete_" + index
                width: autocompleteList.width
                text: modelData
                autoClose: false
                selected: root.completionSelected && index === root.autocompleteIndex
                focusPolicy: Qt.NoFocus
                // Apply the suggestion without stealing focus from the
                // input field — per MarnixCroes PR #540 feedback.
                onTriggered: root.applySuggestion(modelData)
            }
        }
    }

    // Apply an autocomplete suggestion without disturbing the input field's
    // focus / selection state.
    function applySuggestion(cmd) {
        inputField.text = cmd + " "
        inputField.cursorPosition = inputField.text.length
        completionDismissed = true
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
        height: 64
        color: Theme.color.surface1
        radius: Theme.radius.card

        Separator {
            id: inputDivider
            objectName: "consoleInputDivider"
            anchors {
                top: parent.top
                left: parent.left
                right: parent.right
            }
        }

        RowLayout {
            id: inputContent
            objectName: "consoleInputContent"
            anchors {
                fill: parent
                leftMargin: 12
                rightMargin: 12
                topMargin: 12
                bottomMargin: 12
            }
            spacing: 8

            Icon {
                objectName: "consolePromptIcon"
                source: "image://images/caret-right"
                color: Theme.color.accent
                size: 16
                Layout.preferredWidth: 16
                Layout.preferredHeight: 16
                Layout.alignment: Qt.AlignVCenter
            }

            TextField {
                id: inputField
                objectName: "consoleInput"
                Layout.fillWidth: true
                Layout.fillHeight: true
                font: Theme.text.monoCaption.font
                color: Theme.color.neutral9
                placeholderText: qsTr("Enter command…")
                placeholderTextColor: Theme.color.placeholder
                leftPadding: 12
                rightPadding: 12
                topPadding: 0
                bottomPadding: 0
                verticalAlignment: TextInput.AlignVCenter
                background: Rectangle {
                    color: Theme.color.surface2
                    radius: Theme.radius.field

                    FieldBorderRings {
                        objectName: "consoleInputFocusRing"
                        anchors.fill: parent
                        visible: inputField.activeFocus
                        cornerRadius: parent.radius
                    }
                }

                maximumLength: 65536
                // Only keyboard navigation selects a completion for Enter.
                // Hover changes its appearance; Tab/click explicitly fill input.
                Keys.onReturnPressed: (event) => root.acceptHighlightedOrSubmit(event)
                Keys.onEnterPressed: (event) => root.acceptHighlightedOrSubmit(event)

                // Up/Down: navigate autocomplete when popup is open,
                // otherwise browse command history.
                Keys.onUpPressed: {
                    if (autocompletePopup.visible && root.filteredCommands.length > 0) {
                        root.autocompleteIndex = root.completionSelected ? Math.max(0, root.autocompleteIndex - 1) : 0
                        root.completionSelected = true
                    } else {
                        internal.navigatingHistory = true
                        var result = ApplicationModels.rpcConsoleModel.browseHistory(1, inputField.text)
                        inputField.text = result
                        inputField.cursorPosition = result.length
                        internal.navigatingHistory = false
                    }
                }
                Keys.onDownPressed: {
                    if (autocompletePopup.visible && root.filteredCommands.length > 0) {
                        root.autocompleteIndex = root.completionSelected ? Math.min(root.filteredCommands.length - 1, root.autocompleteIndex + 1) : 0
                        root.completionSelected = true
                    } else {
                        internal.navigatingHistory = true
                        var result = ApplicationModels.rpcConsoleModel.browseHistory(-1, inputField.text)
                        inputField.text = result
                        inputField.cursorPosition = result.length
                        internal.navigatingHistory = false
                    }
                }

                // Autocomplete keeps focus in the draft. Popup.CloseOnEscape
                // only handles Escape with popup focus, so route this local
                // presentation intent from the focused field as well.
                Keys.onEscapePressed: (event) => {
                    if (autocompletePopup.visible)
                        autocompletePopup.close()
                    else
                        event.accepted = false
                }

                // Tab key: accept the top autocomplete suggestion.
                Keys.onTabPressed: (event) => {
                    if (autocompletePopup.visible && root.filteredCommands.length > 0) {
                        root.applySuggestion(root.filteredCommands[root.autocompleteIndex])
                        event.accepted = true
                    }
                }

                onTextChanged: {
                    root.commandDraft = inputField.text
                    if (!internal.navigatingHistory) {
                        ApplicationModels.rpcConsoleModel.resetHistoryNavigation()
                    }
                    root.completionDismissed = internal.navigatingHistory
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
        }
    }

    // Matching is owned by the background model. The view owns dismissal and
    // keyboard selection; no full command-catalog scan happens in JavaScript.
    property bool completionDismissed: false
    readonly property var filteredCommands: completionDismissed || ApplicationModels.rpcConsoleModel.completionBusy
        ? [] : ApplicationModels.rpcConsoleModel.completionSuggestions
    property int autocompleteIndex: 0
    property bool completionSelected: false
    Binding {
        target: ApplicationModels.rpcConsoleModel
        property: "completionPrefix"
        value: root.completionDismissed ? "" : inputField.text
    }
    onFilteredCommandsChanged: {
        autocompleteIndex = 0
        completionSelected = false
        if (filteredCommands.length > 0 && !internal.navigatingHistory && tabActive && inputField.activeFocus)
            autocompletePopup.open()
        else autocompletePopup.close()
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

    function changeOutputFontSize(delta) {
        root.outputFontPixelSize = Math.max(root.minimumOutputFontPixelSize,
                                            Math.min(root.maximumOutputFontPixelSize,
                                                     root.outputFontPixelSize + delta))
    }

    function clearInputOrOutput() {
        if (inputField.text.length > 0) {
            inputField.text = ""
            inputField.forceActiveFocus()
            return
        }
        ApplicationModels.rpcConsoleModel.clear()
        inputField.forceActiveFocus()
    }

    // Resolve an explicit keyboard selection without changing the draft before
    // admission. A busy/refused Enter preserves exactly what the user typed.
    function runHighlightedOrSubmit() {
        const command = autocompletePopup.visible && completionSelected && filteredCommands.length > 0
            ? filteredCommands[root.autocompleteIndex] : inputField.text
        submitCommand(command)
    }

    function acceptHighlightedOrSubmit(event) {
        runHighlightedOrSubmit()
        event.accepted = true
    }

    function submitCommand(cmd = inputField.text) {
        if (cmd.length === 0) return
        // Only clear the input if the command was accepted. submitCommand()
        // returns false when another command is still executing, so a refused
        // command stays in the field instead of silently vanishing.
        const accepted = ApplicationModels.rpcConsoleModel.submitInput(cmd, root.walletContext,
                                                                       root.walletName, root.targetAvailable)
        if (accepted) {
            inputField.text = ""
            completionDismissed = true
        }
    }
}
