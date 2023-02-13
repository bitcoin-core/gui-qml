// Copyright (c) 2023 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../../controls"
import "../../components"

Page {
    background: null
    property alias navLeftDetail: navbar.leftDetail

    header: NavigationBar {
        id: navbar
    }

    Text {
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        id: description
        height: 75
        width: Math.min(parent.width - 40, 450)
        wrapMode: Text.WordWrap
        text: qsTr("Peers are nodes you are connected to. You want to ensure that you are connected to x, y and z, but not a, b, and c. Learn more.")
        font.family: "Inter"
        font.styleName: "Regular"
        font.pixelSize: 13
        color: Theme.color.neutral7
        horizontalAlignment: Text.AlignHCenter
    }

    ListView {
        id: listView
        clip: true
        width: Math.min(parent.width - 40, 450)
        anchors.top: description.bottom
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        model: peerListModelProxy
        spacing: 15

        delegate: Item {
            required property int nodeId;
            required property string address;
            required property string subversion;
            required property string direction;
            implicitHeight: 65
            implicitWidth: listView.width

            ColumnLayout {
                anchors.left: parent.left
                Label {
                    Layout.alignment: Qt.AlignLeft
                    id: primary
                    text: "#" + nodeId
                    font.family: "Inter"
                    font.styleName: "Regular"
                    font.pixelSize: 18
                    color: Theme.color.neutral9
                }
                Label {
                    Layout.alignment: Qt.AlignLeft
                    id: tertiary
                    text: address
                    font.family: "Inter"
                    font.styleName: "Regular"
                    font.pixelSize: 15
                    color: Theme.color.neutral7
                }
            }
            ColumnLayout {
                anchors.right: parent.right
                Label {
                    Layout.alignment: Qt.AlignRight
                    id:secondary
                    text: direction
                    font.family: "Inter"
                    font.styleName: "Regular"
                    font.pixelSize: 18
                    color: Theme.color.neutral9
                }
                Label {
                    Layout.alignment: Qt.AlignRight
                    id: quaternary
                    text: subversion
                    font.family: "Inter"
                    font.styleName: "Regular"
                    font.pixelSize: 15
                    color: Theme.color.neutral7
                }
            }
            Separator {
                anchors.bottom: parent.bottom
                width: parent.width
            }
        }
    }
}
