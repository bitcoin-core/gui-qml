// Copyright (c) 2023-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Dialogs 1.3

import "../controls"

ColumnLayout {
    id: columnLayout
    signal back
    property bool snapshotLoading: nodeModel.snapshotLoading
    property bool snapshotLoaded: nodeModel.isSnapshotLoaded
    property bool snapshotImportCompleted: chainModel.isSnapshotActive
    property bool onboarding: false
    property bool snapshotVerified: onboarding ? false : chainModel.isSnapshotActive
    property string snapshotFileName: ""
    property var snapshotInfo: (snapshotVerified || snapshotLoaded) ? chainModel.getSnapshotInfo() : ({})
    property string selectedFile: ""

    width: Math.min(parent.width, 450)
    anchors.horizontalCenter: parent.horizontalCenter

    StackLayout {
        id: settingsStack
        currentIndex: onboarding ? 0 : snapshotLoaded ? 2 : snapshotVerified ? 2 : snapshotLoading ? 1 : 0

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
                onClicked: fileDialog.open()
            }

            FileDialog {
                id: fileDialog
                folder: shortcuts.home
                selectMultiple: false
                selectExisting: true
                nameFilters: ["Snapshot files (*.dat)", "All files (*)"]
                onAccepted: {
                    selectedFile = fileUrl.toString()
                    snapshotFileName = selectedFile
                    nodeModel.snapshotLoadThread(snapshotFileName)
                }
            }
            // TODO: Handle file error signal
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
                description: qsTr("This might take a while...")
            }

            ProgressIndicator {
                id: progressIndicator
                Layout.topMargin: 20
                width: 200
                height: 20
                progress: nodeModel.snapshotProgress
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
                description: snapshotInfo && snapshotInfo["date"] ?
                    qsTr("It contains transactions up to %1. Newer transactions still need to be downloaded." +
                    " The data will be verified in the background.").arg(snapshotInfo["date"]) :
                    qsTr("It contains transactions up to DEBUG. Newer transactions still need to be downloaded." +
                    " The data will be verified in the background.")
            }

            ContinueButton {
                Layout.preferredWidth: Math.min(300, columnLayout.width - 2 * Layout.leftMargin)
                Layout.topMargin: 40
                Layout.alignment: Qt.AlignCenter
                text: qsTr("Done")
                onClicked: {
                    chainModel.isSnapshotActiveChanged()
                    back()
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
                        text: snapshotInfo && snapshotInfo["height"] ?
                            snapshotInfo["height"] : qsTr("DEBUG")
                        Layout.alignment: Qt.AlignRight
                        font.pixelSize: 14
                    }
                }
                Separator { Layout.fillWidth: true }
                CoreText {
                    text: snapshotInfo && snapshotInfo["hashSerialized"] ?
                        qsTr("Hash: %1").arg(snapshotInfo["hashSerialized"].substring(0, 13) + "...") :
                        qsTr("Hash: DEBUG")
                    font.pixelSize: 14
                }
            }
        }
    }
}
