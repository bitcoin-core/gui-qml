// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import org.bitcoincore.qt 1.0

AbstractButton {
    id: root
    property string header
    property alias actionItem: action_loader.sourceComponent
    property alias loadedItem: action_loader.item
    property string description
    property color descriptionColor: Theme.color.neutral7
    property int descriptionSize: 15
    property int descriptionTextFormat: Text.AutoText
    property string errorText: ""
    property bool showErrorText: false
    property string infoText: ""
    property bool showInfoText: false
    property bool interactive: true
    // Keep state caller-owned so model bindings survive hover and click feedback.
    readonly property bool effectivelyDisabled: root.disabled || root.state === "DISABLED"
    readonly property string visualState: root.effectivelyDisabled ? "DISABLED" : (root._interactionState.length > 0 ? root._interactionState : root.state)
    property color labelStateColor: {
        if (root.visualState === "DISABLED") return root.disabledLabelStateColor
        if (root.visualState === "ACTIVE") return root.activeStateColor
        if (root.visualState === "HOVER") return root.hoverStateColor
        return root.filledLabelStateColor
    }
    // Right-side value/action color. Left row text uses labelStateColor.
    property color stateColor: {
        if (root.visualState === "DISABLED") return root.disabledStateColor
        if (root.visualState === "ACTIVE") return root.activeStateColor
        if (root.visualState === "HOVER") return root.hoverStateColor
        return root.filledStateColor
    }
    property color stateDescriptionColor: root.visualState === "DISABLED" ? root.disabledDescriptionStateColor : root.descriptionColor
    property color filledLabelStateColor: Theme.color.neutral7
    property color filledStateColor: Theme.color.neutral9
    property color hoverStateColor: Theme.color.orangeLight1
    property color activeStateColor: Theme.color.orange
    property color disabledLabelStateColor: Theme.color.neutral4
    property color disabledDescriptionStateColor: Theme.dark ? Theme.color.neutral4 : Theme.color.neutral6
    property color disabledStateColor: Theme.color.neutral4
    property bool disabled: false
    property string _interactionState: ""
    hoverEnabled: root.interactive && AppMode.isDesktop
    enabled: !root.effectivelyDisabled
    state: "FILLED"

    states: [
        State { name: "FILLED" },
        State { name: "HOVER" },
        State { name: "ACTIVE" },
        State { name: "DISABLED" }
    ]

    onStateChanged: {
        if (root.state === "DISABLED") root._interactionState = ""
    }
    onEffectivelyDisabledChanged: {
        if (root.effectivelyDisabled) root._interactionState = ""
    }

    background: FocusBorder {
        visible: root.visualFocus
        topMargin: -4
        bottomMargin: -4
        leftMargin: -6
        rightMargin: -6
    }

    MouseArea {
        id: mouseArea
        anchors.fill: root
        enabled: root.enabled
        hoverEnabled: root.interactive && AppMode.isDesktop
        cursorShape: AppMode.isDesktop && root.interactive && root.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        onEntered: {
            if (root.interactive && !root.effectivelyDisabled) root._interactionState = "HOVER"
        }
        onExited: {
            root._interactionState = ""
        }
        onPressed: {
            if (root.interactive && !root.effectivelyDisabled) root._interactionState = "ACTIVE"
        }
        onReleased: {
            if (!root.interactive || root.effectivelyDisabled) return
            if (mouseArea.containsMouse) {
                root._interactionState = "HOVER"
                root.clicked()
            } else {
                root._interactionState = ""
            }
        }
    }

    contentItem: RowLayout {
        Header {
            Layout.topMargin: 14
            Layout.bottomMargin: 14
            Layout.fillWidth: true
            center: false
            header: root.header
            headerSize: 18
            headerColor: root.labelStateColor
            description: root.description
            descriptionSize: root.descriptionSize
            descriptionColor: root.stateDescriptionColor
            descriptionTextFormat: root.descriptionTextFormat
            descriptionMargin: 0
            subtext: root.showErrorText ? root.errorText : (root.showInfoText ? root.infoText : "")
            subtextColor: (root.showErrorText || root.showInfoText) ? Theme.color.blue : Theme.color.neutral7
        }
        Loader {
            id: action_loader
            active: true
            visible: active
            sourceComponent: root.actionItem
        }
    }
}
