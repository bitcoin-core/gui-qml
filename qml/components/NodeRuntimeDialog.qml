// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Window 2.15
import org.bitcoincore.qt 1.0

import "../controls"

// Shows the node's message boxes. Each popup keeps the prompt it was created
// for, so a delayed action can never answer a successor prompt.
AlertPopup {
    id: root
    objectName: "nodeRuntimeDialog"
    property RuntimePromptModel prompt: ApplicationModels.nodeModel.runtimePrompt
    property bool componentComplete: false
    property bool showCompleteDiagnostics: false

    component RuntimeAction: AlertAction {
        required property int button
        required property string name
        buttonObjectName: "nodeRuntimeDialogButton" + name
        closesPopup: false
    }

    // Answers the prompt this popup was created for; closes once it accepts.
    function answer(button) {
        if (root.prompt !== null && root.prompt.answer(button)) root.close()
    }

    // Cancel-first order. No, Abort and Cancel are Cancel actions, so Escape
    // answers with them.
    readonly property list<AlertAction> runtimeActions: [
        RuntimeAction { button: DialogButtonBox.No; name: "No"; text: qsTr("No"); role: AlertAction.Cancel; onTriggered: root.answer(DialogButtonBox.No) },
        RuntimeAction { button: DialogButtonBox.Abort; name: "Abort"; text: qsTr("Abort"); role: AlertAction.Cancel; onTriggered: root.answer(DialogButtonBox.Abort) },
        RuntimeAction { button: DialogButtonBox.Cancel; name: "Cancel"; text: qsTr("Cancel"); role: AlertAction.Cancel; onTriggered: root.answer(DialogButtonBox.Cancel) },
        RuntimeAction { button: DialogButtonBox.Close; name: "Close"; text: qsTr("Close"); role: AlertAction.Neutral; onTriggered: root.answer(DialogButtonBox.Close) },
        RuntimeAction { button: DialogButtonBox.Help; name: "Help"; text: qsTr("Help"); role: AlertAction.Neutral; onTriggered: root.answer(DialogButtonBox.Help) },
        RuntimeAction { button: DialogButtonBox.Reset; name: "Reset"; text: qsTr("Reset"); role: AlertAction.Neutral; onTriggered: root.answer(DialogButtonBox.Reset) },
        RuntimeAction { button: DialogButtonBox.Discard; name: "Discard"; text: qsTr("Discard"); role: AlertAction.Neutral; onTriggered: root.answer(DialogButtonBox.Discard) },
        RuntimeAction { button: DialogButtonBox.Ignore; name: "Ignore"; text: qsTr("Ignore"); role: AlertAction.Neutral; onTriggered: root.answer(DialogButtonBox.Ignore) },
        RuntimeAction { button: DialogButtonBox.Retry; name: "Retry"; text: qsTr("Retry"); onTriggered: root.answer(DialogButtonBox.Retry) },
        RuntimeAction { button: DialogButtonBox.Apply; name: "Apply"; text: qsTr("Apply"); onTriggered: root.answer(DialogButtonBox.Apply) },
        RuntimeAction { button: DialogButtonBox.Yes; name: "Yes"; text: qsTr("Yes"); onTriggered: root.answer(DialogButtonBox.Yes) },
        RuntimeAction { button: DialogButtonBox.Ok; name: "Ok"; text: qsTr("OK"); onTriggered: root.answer(DialogButtonBox.Ok) }
    ]

    // Not gated on opened: an answer given during the enter transition must
    // still close the popup, and open() and close() are no-ops when settled.
    function syncOpenState() {
        if (root.prompt !== null) {
            const window = root.parent !== null ? root.parent.Window.window : null
            if (window === null) {
                root.prompt.unavailable()
                return
            }
            window.show()
            window.requestActivate()
            open()
        } else {
            close()
        }
    }

    onPromptChanged: if (componentComplete) syncOpenState()
    onOpened: {
        const window = root.parent !== null ? root.parent.Window.window : null
        if (root.prompt !== null && window !== null) {
            window.show()
            window.requestActivate()
            root.prompt.presented()
        } else if (root.prompt !== null) {
            root.prompt.unavailable()
        }
    }
    Component.onDestruction: if (root.prompt !== null) root.prompt.unavailable()
    Connections {
        target: root.parent !== null ? root.parent.Window.window : null
        function onVisibleChanged() {
            const window = root.parent !== null ? root.parent.Window.window : null
            if (window !== null && !window.visible && root.prompt !== null) root.prompt.unavailable()
        }
    }

    Component.onCompleted: {
        componentComplete = true
        syncOpenState()
    }

    title: root.prompt !== null ? root.prompt.title : ""
    message: root.prompt !== null && !root.showCompleteDiagnostics ? root.prompt.message : ""
    messageObjectName: "nodeRuntimeDialogMessage"
    messageAlignment: Text.AlignLeft
    iconSource: root.prompt !== null ? root.prompt.icon : ""
    tone: iconSource.toString().indexOf("error") >= 0 ? AlertPopup.Error
        : iconSource.toString().indexOf("alert") >= 0 ? AlertPopup.Warning
        : AlertPopup.Default
    implicitWidth: Math.max(480, horizontalActionsWidth + 40)
    content: [
        MonospaceOutputView {
            objectName: "nodeRuntimeCompleteDiagnostics"
            Layout.fillWidth: true
            Layout.preferredHeight: 240
            visible: root.showCompleteDiagnostics
            listModel: root.prompt !== null ? root.prompt.diagnostics : null
            autoScrollToBottom: false
        },
        TextButton {
            objectName: "nodeRuntimeDetailsButton"
            Layout.alignment: Qt.AlignHCenter
            visible: root.prompt !== null && root.prompt.diagnostics !== null
            text: root.showCompleteDiagnostics ? qsTr("Show summary") : qsTr("Show complete diagnostics")
            onClicked: root.showCompleteDiagnostics = !root.showCompleteDiagnostics
        }
    ]
    actionList: {
        const buttons = root.prompt !== null ? root.prompt.buttons : DialogButtonBox.NoButton
        const actions = []
        for (let i = 0; i < runtimeActions.length; ++i) {
            if (buttons & runtimeActions[i].button) actions.push(runtimeActions[i])
        }
        return actions
    }
    // A message with a single button is acknowledged by Escape too. Questions
    // prefer an explicit negative answer over AlertPopup's neutral fallback.
    escapeAction: {
        if (actionList.length === 1) return actionList[0]
        for (let i = 0; i < actionList.length; ++i) {
            if (actionList[i].role === AlertAction.Cancel) return actionList[i]
        }
        return null
    }
}
