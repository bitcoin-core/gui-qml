// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Layouts 1.15

import "../../../controls"

DesignSystemSection {
    id: root

    readonly property var durationTokens: ["fast", "normal", "moderate", "exit", "slow", "navigation", "tooltipDelay"]
    readonly property var easingRoles: ["enter", "exit", "move", "loop"]
    readonly property var helperRows: [
        ["mode", Theme.motion.mode],
        ["animated", Theme.motion.animated],
        ["reduced", Theme.motion.reduced],
        ["decorativeLoops", Theme.motion.decorativeLoops],
        ["travel(300)", Theme.motion.travel(300)],
        ["fade(300)", Theme.motion.fade(300)],
        ["delay(650)", Theme.motion.delay(650)],
        ["dialogOffset", Theme.motion.dialogOffset],
        ["popoverScale", Theme.motion.popoverScale],
        ["pressScale", Theme.motion.pressScale]
    ]

    signal replayRequested()

    title: "Motion"
    description: "Theme.motion tokens in the current mode. Switch the mode under Page controls; Replay runs every sample again."

    NeutralButton {
        objectName: "designSystemMotionReplayButton"
        text: "Replay"
        onClicked: root.replayRequested()
    }

    DesignSystemSpecimen {
        objectName: "designSystemMotionDurations"
        Layout.fillWidth: true
        name: "Durations"
        note: "A bar fills over each duration with enter easing. Under off every duration is 0."

        Repeater {
            model: root.durationTokens

            delegate: DesignSystemState {
                id: durationSample
                required property string modelData
                objectName: "designSystemMotion_" + modelData
                width: 160
                label: modelData + "\n" + Theme.motion[modelData] + " ms"

                Rectangle {
                    Layout.preferredWidth: 140
                    Layout.preferredHeight: 8
                    radius: 4
                    color: Theme.color.surface2

                    Rectangle {
                        id: fill
                        height: parent.height
                        radius: parent.radius
                        color: Theme.color.accent
                        width: parent.width
                    }

                    NumberAnimation {
                        id: fillAnimation
                        target: fill
                        property: "width"
                        from: 0
                        to: 140
                        duration: Theme.motion[durationSample.modelData]
                        easing.type: Theme.motion.easing.enter
                    }

                    Connections {
                        target: root
                        function onReplayRequested() { fillAnimation.restart() }
                    }
                }
            }
        }
    }

    DesignSystemSpecimen {
        objectName: "designSystemMotionEasing"
        Layout.fillWidth: true
        name: "Easing roles"
        note: "A dot crosses the track over slow with each role: enter for things appearing, exit for things leaving, move for on-screen moves, loop for repeating animations."

        Repeater {
            model: root.easingRoles

            delegate: DesignSystemState {
                id: easingSample
                required property string modelData
                objectName: "designSystemMotionEasing_" + modelData
                width: 160
                label: "easing." + modelData

                Rectangle {
                    id: track
                    Layout.preferredWidth: 140
                    Layout.preferredHeight: 16
                    radius: 8
                    color: Theme.color.surface2

                    Rectangle {
                        id: dot
                        x: track.width - width
                        width: 16
                        height: 16
                        radius: 8
                        color: Theme.color.accent
                    }

                    NumberAnimation {
                        id: dotAnimation
                        target: dot
                        property: "x"
                        from: 0
                        to: track.width - dot.width
                        duration: Theme.motion.slow
                        easing.type: Theme.motion.easing[easingSample.modelData]
                    }

                    Connections {
                        target: root
                        function onReplayRequested() { dotAnimation.restart() }
                    }
                }
            }
        }
    }

    DesignSystemSpecimen {
        objectName: "designSystemMotionTransitions"
        Layout.fillWidth: true
        name: "Standard transitions"
        note: "Dialog: fade over fade(slow) and a dialogOffset drop over travel(slow). Popover: adds popoverScale. Press: pressScale while held. Reduced motion keeps only the fades."

        DesignSystemState {
            objectName: "designSystemMotionDialog"
            label: "dialog"

            Item {
                Layout.preferredWidth: 140
                Layout.preferredHeight: 72

                Rectangle {
                    id: dialogSample
                    property real offset: 0
                    width: 120
                    height: 56
                    x: 10
                    y: 12 + offset
                    radius: 10
                    color: Theme.color.surface1
                    border.color: Theme.color.surface2
                    border.width: 1
                }

                ParallelAnimation {
                    id: dialogAnimation
                    NumberAnimation {
                        target: dialogSample; property: "opacity"; from: 0; to: 1
                        duration: Theme.motion.fade(Theme.motion.slow); easing.type: Theme.motion.easing.enter
                    }
                    NumberAnimation {
                        target: dialogSample; property: "offset"; from: -Theme.motion.dialogOffset; to: 0
                        duration: Theme.motion.travel(Theme.motion.slow); easing.type: Theme.motion.easing.enter
                    }
                }

                Connections {
                    target: root
                    function onReplayRequested() { dialogAnimation.restart() }
                }
            }
        }

        DesignSystemState {
            objectName: "designSystemMotionPopover"
            label: "popover"

            Item {
                Layout.preferredWidth: 140
                Layout.preferredHeight: 72

                Rectangle {
                    id: popoverSample
                    width: 120
                    height: 56
                    x: 10
                    y: 8
                    radius: 10
                    transformOrigin: Item.TopLeft
                    color: Theme.color.surface1
                    border.color: Theme.color.surface2
                    border.width: 1
                }

                ParallelAnimation {
                    id: popoverAnimation
                    NumberAnimation {
                        target: popoverSample; property: "opacity"; from: 0; to: 1
                        duration: Theme.motion.fade(Theme.motion.slow); easing.type: Theme.motion.easing.enter
                    }
                    NumberAnimation {
                        target: popoverSample; property: "scale"; from: Theme.motion.popoverScale; to: 1
                        duration: Theme.motion.travel(Theme.motion.slow); easing.type: Theme.motion.easing.enter
                    }
                }

                Connections {
                    target: root
                    function onReplayRequested() { popoverAnimation.restart() }
                }
            }
        }

        DesignSystemState {
            objectName: "designSystemMotionPress"
            label: "press (hold the button)"

            NeutralButton { text: "Hold me" }
        }
    }

    DesignSystemSpecimen {
        objectName: "designSystemMotionHelpers"
        Layout.fillWidth: true
        name: "Mode and helpers"
        note: "travel() and delay() drop to 0 under reduced and off; fade() is capped at normal under reduced and 0 under off."

        Repeater {
            model: root.helperRows

            delegate: DesignSystemState {
                required property var modelData
                objectName: "designSystemMotionHelper_" + modelData[0]
                width: 160
                label: modelData[0] + "\n" + modelData[1]
            }
        }
    }
}
