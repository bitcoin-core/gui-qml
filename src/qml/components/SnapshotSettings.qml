// Copyright (c) 2023-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import "../controls"

ColumnLayout {
    signal snapshotImportCompleted()
    property int snapshotVerificationCycles: 0
    property real snapshotVerificationProgress: 0
    property bool snapshotVerified: false

    id: columnLayout
    width: Math.min(parent.width, 450)
    anchors.horizontalCenter: parent.horizontalCenter


    Timer {
        id: snapshotSimulationTimer
        interval: 50 // Update every 50ms
        running: false
        repeat: true
        onTriggered: {
            if (snapshotVerificationProgress < 1) {
                snapshotVerificationProgress += 0.01
            } else {
                snapshotVerificationCycles++
                if (snapshotVerificationCycles < 1) {
                    snapshotVerificationProgress = 0
                } else {
                    running = false
                    snapshotVerified = true
                    settingsStack.currentIndex = 2
                }
            }
        }
    }

    StackLayout {
        id: settingsStack
        currentIndex: 0

        ColumnLayout {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: Math.min(parent.width, 450)

            Image {
                Layout.alignment: Qt.AlignCenter
                source: "image://images/circle-file"

                sourceSize.width: 200
                sourceSize.height: 200
            }

            Header {
                Layout.fillWidth: true
                Layout.topMargin: 20
                headerBold: true
                header: qsTr("Load snapshot")
                descriptionBold: false
                descriptionColor: Theme.color.neutral6
                descriptionSize: 17
                descriptionLineHeight: 1.1
                description: qsTr("You can start using the application more quickly by loading a recent transaction snapshot." +
                " It will be automatically verified in the background.")
            }

            ContinueButton {
                Layout.preferredWidth: Math.min(300, columnLayout.width - 2 * Layout.leftMargin)
                Layout.topMargin: 40
                Layout.leftMargin: 20
                Layout.rightMargin: Layout.leftMargin
                Layout.bottomMargin: 20
                Layout.alignment: Qt.AlignCenter
                text: qsTr("Choose snapshot file")
                onClicked: {
                    settingsStack.currentIndex = 1
                    snapshotSimulationTimer.start()
                }
            }
        }

        ColumnLayout {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: Math.min(parent.width, 450)

            Image {
                Layout.alignment: Qt.AlignCenter
                source: "image://images/circle-file"

                sourceSize.width: 200
                sourceSize.height: 200
            }

            Header {
                Layout.fillWidth: true
                Layout.topMargin: 20
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                header: qsTr("Loading Snapshot")
            }

            ProgressIndicator {
                id: progressIndicator
                Layout.topMargin: 20
                width: 200
                height: 20
                progress: snapshotVerificationProgress
                Layout.alignment: Qt.AlignCenter
                progressColor: Theme.color.blue
            }
        }

        ColumnLayout {
            id: loadedSnapshotColumn
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: Math.min(parent.width, 450)

            Image {
                Layout.alignment: Qt.AlignCenter
                source: "image://images/circle-green-check"

                sourceSize.width: 60
                sourceSize.height: 60
            }

            Header {
                Layout.fillWidth: true
                Layout.topMargin: 20
                headerBold: true
                header: qsTr("Snapshot Loaded")
                descriptionBold: false
                descriptionColor: Theme.color.neutral6
                descriptionSize: 17
                descriptionLineHeight: 1.1
                description: qsTr("It contains transactions up to January 12, 2024. Newer transactions still need to be downloaded." +
                " The data will be verified in the background.")
            }

            ContinueButton {
                Layout.preferredWidth: Math.min(300, columnLayout.width - 2 * Layout.leftMargin)
                Layout.topMargin: 40
                Layout.alignment: Qt.AlignCenter
                text: qsTr("Done")
                onClicked: {
                    snapshotImportCompleted()
                    connectionSwipe.decrementCurrentIndex()
                    connectionSwipe.decrementCurrentIndex()
                }
            }

            Setting {
                id: viewDetails
                Layout.alignment: Qt.AlignCenter
                header: qsTr("View details")
                actionItem: CaretRightIcon {
                    id: caretIcon
                    color: viewDetails.stateColor
                    rotation: viewDetails.expanded ? 90 : 0
                    Behavior on rotation { NumberAnimation { duration: 200 } }
                }

                property bool expanded: false

                onClicked: {
                    expanded = !expanded
                }
            }

            ColumnLayout {
                id: detailsContent
                visible: viewDetails.expanded
                Layout.preferredWidth: Math.min(300, parent.width - 2 * Layout.leftMargin)
                Layout.alignment: Qt.AlignCenter
                Layout.leftMargin: 80
                Layout.rightMargin: 80
                Layout.topMargin: 10
                spacing: 10
                // TODO: make sure the block height number aligns right
                RowLayout {
                    CoreText {
                        text: qsTr("Block Height:")
                        Layout.alignment: Qt.AlignLeft
                        font.pixelSize: 14
                    }
                    CoreText {
                        text: qsTr("200,000")
                        Layout.alignment: Qt.AlignRight
                        font.pixelSize: 14
                    }
                }
                Separator { Layout.fillWidth: true }
                CoreText {
                    text: qsTr("Hash: 0x1234567890abcdef...")
                    font.pixelSize: 14
                }
            }
        }
    }
}
