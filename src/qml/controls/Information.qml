// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Control {
    id: root
    property bool last: parent && root === parent.children[parent.children.length - 1]
    required property string header
    property string subtext
    property int subtextMargin: 3
    property string description
    property int descriptionMargin: 10
    property int descriptionSize: 18
    property string link
    contentItem: ColumnLayout {
        spacing: 20
        width: parent.width
        RowLayout {
            Header {
                Layout.fillWidth: true
                center: false
                header: root.header
                headerSize: 18
                description: root.subtext
                descriptionSize: 15
                descriptionMargin: root.subtextMargin
                wrap: false
            }
            Loader {
                Layout.fillWidth: true
                Layout.preferredWidth: 0
                Layout.rightMargin: 12
                active: root.description.length > 0
                visible: active
                sourceComponent: Label {
                    topPadding: root.descriptionMargin
                    font.family: "Inter"
                    font.styleName: "Regular"
                    font.pointSize: root.descriptionSize
                    color: Theme.color.neutral8
                    text: root.description
                    onLinkActivated: Qt.openUrlExternally(link)
                    MouseArea {
                        id: mouseArea
                        anchors.fill: parent
                        cursorShape: link.length > 0 ? Qt.PointingHandCursor : Qt.ArrowCursor
                    }
                    horizontalAlignment: Text.AlignRight
                    wrapMode: Text.WordWrap
                }
            }
        }
        Loader {
            Layout.fillWidth:true
            Layout.columnSpan: 2
            active: !last
            visible: active
            sourceComponent: Rectangle {
                height: 1
                color: Theme.color.neutral5
            }
        }
    }
}
