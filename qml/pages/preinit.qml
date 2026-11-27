// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import org.bitcoincore.qt 1.0
import "../controls"
import "./onboarding"

ApplicationWindow {
    id: appWindow
    objectName: "preInitWindow"
    title: qsTr("Bitcoin Core App")
    minimumWidth: 800
    minimumHeight: 665
    // Without a size, the window starts 1 x 1 on some platforms, and the main
    // window takes over this window's geometry.
    width: minimumWidth
    height: minimumHeight
    color: Theme.color.background
    palette.highlight: Theme.color.selection
    palette.highlightedText: Theme.color.selectedText
    visible: true

    readonly property bool completed: ApplicationModels.onboardingOptionsModel.setupCommitted
    readonly property bool starting: ApplicationModels.onboardingOptionsModel.setupCommitPending || completed
    signal finished()
    signal startupErrorAcknowledged()

    // Startup errors while this window is open show here instead of in a
    // second window.
    function showStartupError(text) {
        startupErrorAlert.message = text
        startupErrorAlert.open()
    }

    AppPreferences { id: preferences }

    Popup {
        objectName: "preInitPreferencesError"
        parent: Overlay.overlay
        anchors.centerIn: parent
        visible: !appWindow.starting && preferences.error.length > 0
        closePolicy: Popup.NoAutoClose
        Column {
            spacing: 12
            CoreText { text: preferences.error; wrapMode: Text.WordWrap; width: 360 }
            Button {
                objectName: "preInitPreferencesRetry"
                text: qsTr("Retry preferences")
                enabled: !preferences.pending
                onClicked: preferences.retry()
            }
        }
    }

    OnboardingWizard {
        objectName: "preInitSetupWizard"
        anchors.fill: parent
        preInit: true
        settingsModel: ApplicationModels.onboardingOptionsModel
        assumedBlockchainSize: ApplicationModels.onboardingOptionsModel.assumedBlockchainSize
        assumedChainstateSize: ApplicationModels.onboardingOptionsModel.assumedChainstateSize
        enabled: !ApplicationModels.onboardingOptionsModel.setupCommitPending && !ApplicationModels.onboardingOptionsModel.setupCommitted && !ApplicationModels.onboardingOptionsModel.setupPartial && !ApplicationModels.onboardingOptionsModel.setupUncertain
        onFinished: ApplicationModels.onboardingOptionsModel.commitSettings()
    }

    Connections {
        target: ApplicationModels.onboardingOptionsModel
        function onSettingsCommitted() {
            appWindow.finished()
        }
    }
    Row {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 65
        spacing: 12
        Button {
            objectName: "preInitRetrySetupChecks"
            text: qsTr("Retry checks")
            visible: ApplicationModels.onboardingOptionsModel.setupCommitError.length > 0 && !ApplicationModels.onboardingOptionsModel.setupPartial && !ApplicationModels.onboardingOptionsModel.setupUncertain
            enabled: !ApplicationModels.onboardingOptionsModel.setupCommitPending
            onClicked: ApplicationModels.onboardingOptionsModel.retrySetupChecks()
        }
        Button {
            objectName: "preInitQuerySetupOutcome"
            text: qsTr("Check setup")
            visible: ApplicationModels.onboardingOptionsModel.setupUncertain
            enabled: !ApplicationModels.onboardingOptionsModel.setupCommitPending
            onClicked: ApplicationModels.onboardingOptionsModel.querySetupOutcome()
        }
        Button {
            objectName: "preInitCompleteSavedSetup"
            text: qsTr("Complete saved setup")
            visible: ApplicationModels.onboardingOptionsModel.setupPartial && !ApplicationModels.onboardingOptionsModel.setupUncertain
            enabled: !ApplicationModels.onboardingOptionsModel.setupCommitPending
            onClicked: ApplicationModels.onboardingOptionsModel.completeSavedSetup()
        }
    }
    CoreText {
        objectName: "preInitSettingsCommitError"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        width: parent.width - 40
        visible: !appWindow.starting && text.length > 0
        text: ApplicationModels.onboardingOptionsModel.setupCommitError
        color: Theme.color.dangerText
        wrap: true
    }
    CoreText {
        objectName: "preInitPreviewErrorText"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 20
        visible: !appWindow.starting && ApplicationModels.onboardingOptionsModel.previewError.length > 0
        text: ApplicationModels.onboardingOptionsModel.previewError
        color: Theme.color.dangerText
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        textStyle: Theme.text.description
    }

    Item {
        objectName: "preInitStartupOverlay"
        anchors.fill: parent
        visible: appWindow.starting
        z: 100

        Rectangle {
            anchors.fill: parent
            color: Theme.color.background
        }

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
            hoverEnabled: true
        }

        SpinningIndicator {
            objectName: "preInitStartupBusyIndicator"
            anchors.centerIn: parent
            width: 32
            height: 32
            strokeWidth: 3
            running: appWindow.starting
        }

        focus: visible
        onVisibleChanged: {
            if (visible) {
                forceActiveFocus()
            }
        }
        Keys.onPressed: (event) => {
            event.accepted = true
        }
    }

    AlertPopup {
        id: startupErrorAlert
        objectName: "preInitStartupErrorPopup"
        title: qsTr("There was an issue starting up.")
        messageObjectName: "preInitStartupErrorMessage"
        messageTextFormat: Text.PlainText

        AlertAction {
            text: qsTr("Close")
            buttonObjectName: "preInitStartupErrorCloseButton"
        }

        onClosed: appWindow.startupErrorAcknowledged()
    }
}
