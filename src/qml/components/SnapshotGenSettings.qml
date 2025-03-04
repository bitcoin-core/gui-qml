// Copyright (c) 2023-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Dialogs 1.3

import "../controls"
import "../controls/utils.js" as Utils


ColumnLayout {
    id: columnLayout
    signal back
    property bool onboarding: false
    property bool generateSnapshot: false
    property string selectedFile: ""
    property bool snapshotGenerating: nodeModel.snapshotGenerating
    property bool isPruned: optionsModel.prune
    property bool isIBDCompleted: nodeModel.isIBDCompleted
    property bool isSnapshotGenerated: nodeModel.isSnapshotGenerated
    property var snapshotInfo: ( isSnapshotGenerated || isIBDCompleted ) ? chainModel.getSnapshotInfo() : ({})
    property bool isRewinding: nodeModel.isRewinding


    width: Math.min(parent.width, 450)
    anchors.horizontalCenter: parent.horizontalCenter

    StackLayout {
        id: genSettingsStack
        currentIndex: snapshotGenerating ? 1 : isSnapshotGenerated ? 2 : generateSnapshot ? 0 : onboarding ? 0 : 0

        ColumnLayout {
            // index: 0
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
                header: qsTr("Generate snapshot")
                descriptionBold: false
                descriptionColor: Theme.color.neutral6
                descriptionSize: 17
                descriptionLineHeight: 1.1
                description: isPruned
                    ? qsTr("A snapshot captures the state of the bitcoin network up to a certain date in the recent past. " +
                           "It can be imported into other bitcoin nodes to speed up the initial setup.\n\n")
                    : isIBDCompleted
                        ? qsTr("A snapshot captures the state of the bitcoin network up to a certain date in the recent past. " +
                               "It can be imported into other bitcoin nodes to speed up the initial setup.\n\n" +
                               "You can generate a snapshot at block height %1 and date %2.").arg(snapshotInfo["height"]).arg(snapshotInfo["date"])
                        : qsTr("A snapshot captures the state of the bitcoin network up to a certain date in the recent past. " +
                               "It can be imported into other bitcoin nodes to speed up the initial setup.\n\n" +
                               "Snapshot generation is available once the initial block download is complete.")
            }

            CoreText {
                Layout.fillWidth: true
                // Layout.topMargin: 20
                color: Theme.color.neutral6
                font.pixelSize: 17
                visible: isPruned
                text: isPruned
                    ? qsTr("Snapshot generation is not available due to current settings. To use this feature, disable \"Store recent blocks only\" in Storage Settings and download the transaction history again.")
                    : qsTr("")
                wrap: true
                wrapMode: Text.WordWrap
            }

            ContinueButton {
                Layout.preferredWidth: Math.min(300, columnLayout.width - 2 * Layout.leftMargin)
                Layout.topMargin: 40
                Layout.alignment: Qt.AlignCenter
                text: qsTr("Generate")
                enabled: !isPruned && isIBDCompleted
                onClicked: {
                    nodeModel.generateSnapshotThread()
                }
            }
        }

        ColumnLayout {
            // index: 1
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
                header: qsTr("Generating Snapshot")
                description: isRewinding ? qsTr("Rewinding...\nThis might take a while...") : qsTr("Restoring...\nThis might take a while...")
            }

            ProgressIndicator {
                id: generatingProgressIndicator
                Layout.topMargin: 20
                width: 200
                height: 20
                progress: nodeModel.snapshotGenerating ? nodeModel.rewindProgress : 0
                Layout.alignment: Qt.AlignCenter
                progressColor: Theme.color.blue
            }
        }

        ColumnLayout {
            // index: 2
            id: snapshotGeneratedColumn
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
                header: qsTr("Snapshot Generated")
                descriptionBold: false
                descriptionColor: Theme.color.neutral6
                descriptionSize: 17
                descriptionLineHeight: 1.1
                description: snapshotInfo && snapshotInfo["date"] ?
                    qsTr("It contains transactions up to %1." +
                    " You can use this snapshot to quickstart other nodes.").arg(snapshotInfo["date"])
                    : qsTr("It contains transactions up to DEBUG. You can use this snapshot to quickstart other nodes.")
            }

            TextButton {
                Layout.alignment: Qt.AlignCenter
                text: qsTr("Generate new snapshot")
                onClicked: {
                    nodeModel.generateSnapshotThread()
                }
            }

            ContinueButton {
                Layout.preferredWidth: Math.min(300, columnLayout.width - 2 * Layout.leftMargin)
                Layout.topMargin: 20
                Layout.alignment: Qt.AlignCenter
                text: qsTr("View file")
                borderColor: Theme.color.neutral6
                backgroundColor: "transparent"
                onClicked: viewSnapshotFileDialog.open()
            }

            FileDialog {
                id: viewSnapshotFileDialog
                folder: nodeModel.getSnapshotDirectory()
                selectMultiple: false
                selectExisting: true
                nameFilters: ["Snapshot files (*.dat)", "All files (*)"]
            }

            Setting {
                id: snapshotGeneratedViewDetails
                Layout.alignment: Qt.AlignCenter
                header: qsTr("View details")
                actionItem: CaretRightIcon {
                    id: snapshotGeneratedCaretIcon
                    color: snapshotGeneratedViewDetails.stateColor
                    rotation: snapshotGeneratedViewDetails.expanded ? 90 : 0
                    Behavior on rotation { NumberAnimation { duration: 200 } }
                }

                property bool expanded: false

                onClicked: {
                    expanded = !expanded
                }
            }

            ColumnLayout {
                id: snapshotGeneratedDetailsContent
                visible: snapshotGeneratedViewDetails.expanded
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
                        text: snapshotInfo && snapshotInfo["height"] ?
                            snapshotInfo["height"] : qsTr("DEBUG")
                        Layout.alignment: Qt.AlignRight
                        font.pixelSize: 14
                    }
                }
                Separator { Layout.fillWidth: true }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 5
                    CoreText {
                        text: qsTr("Hash:")
                        font.pixelSize: 14
                    }
                    CoreText {
                        text: snapshotInfo && snapshotInfo["hashSerializedFirstHalf"] ?
                            snapshotInfo["hashSerializedFirstHalf"] + "\n" + snapshotInfo["hashSerializedSecondHalf"] :
                            qsTr("DEBUG")
                        Layout.fillWidth: true
                        font.pixelSize: 14
                        textFormat: Text.PlainText
                    }
                }
            }
        }
    }
}
