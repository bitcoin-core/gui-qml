pragma ComponentBehavior: Bound

// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Layouts 1.15

ListRow {
    id: root

    property string value: ""
    property url link: ""
    property url linkIconSource: "image://images/export"
    property int linkIconSize: 18
    property int valueMaximumWidth: 300
    property color valueColor: enabled ? Theme.color.neutral9 : Theme.color.neutral4
    property color linkIconColor: valueColor
    property var valueTextStyle: Theme.text.description

    signal activated(url link)

    Accessible.name: value.length > 0 ? title + ", " + value : title
    accessibleRole: Accessible.Link

    trailingItem: RowLayout {
        spacing: 6

        CoreText {
            objectName: root.objectName.length > 0 ? root.objectName + "Value" : ""
            Layout.maximumWidth: root.valueMaximumWidth
            text: root.value
            color: root.valueColor
            font: root.valueTextStyle.font
            lineHeight: root.valueTextStyle.lineHeight
            lineHeightMode: Text.FixedHeight
            horizontalAlignment: Text.AlignRight
            wrap: false
            elide: Text.ElideMiddle
        }

        Icon {
            visible: root.linkIconSource.toString().length > 0
            Layout.preferredWidth: visible ? root.linkIconSize : 0
            Layout.preferredHeight: visible ? root.linkIconSize : 0
            source: root.linkIconSource
            color: root.linkIconColor
            size: root.linkIconSize
        }
    }

    onClicked: root.activated(root.link)
}
