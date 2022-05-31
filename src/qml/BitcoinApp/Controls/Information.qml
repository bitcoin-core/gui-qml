// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Control {
    id: root
    property bool last: parent && root === parent.children[parent.children.length - 1]
    required property string header
    property string subtext
    property int subtextMargin: 3
    property string description
    property int descriptionMargin: 10
    property int descriptionSize: 18
    property bool isReadonly: true
    property bool hasIcon: false
    property string iconSource
    property int iconWidth: 30
    property int iconHeight: 30
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
                Layout.alignment: Qt.AlignRight | Qt.AlignHCenter
                active: root.description.length > 0
                visible: active
                sourceComponent: TextEdit {
                    font.family: "Inter"
                    font.styleName: "Regular"
                    font.pixelSize: root.descriptionSize
                    color: Theme.color.neutral8
                    textFormat: Text.RichText
                    text: "<style>a:link { color: " + Theme.color.neutral8 + "; text-decoration: none;}</style>" + "<a href=\"" + link + "\">" + root.description + "</a>"
                    readOnly: isReadonly
                    onLinkActivated: Qt.openUrlExternally(link)
                    horizontalAlignment: Text.AlignRight
                    wrapMode: Text.WordWrap
                }
            }
            Loader {
                Layout.preferredWidth: root.iconWidth
                Layout.preferredHeight: root.iconHeight
                Layout.alignment: Qt.AlignRight | Qt.AlignHCenter
                active: root.hasIcon
                visible: active
                sourceComponent: Image {
                    horizontalAlignment: Image.AlignRight
                    source: root.iconSource
                    fillMode: Image.PreserveAspectFit
                    mipmap: true
                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            Qt.openUrlExternally(link)
                        }
                    }
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
