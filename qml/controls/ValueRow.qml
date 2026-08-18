pragma ComponentBehavior: Bound

// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Layouts 1.15

FormRow {
    id: root

    property string value: ""
    property url valueIconSource: ""
    property int valueIconSize: 18
    property int valueMaximumWidth: 260
    property color valueColor: enabled ? Theme.color.neutral9 : Theme.color.neutral4
    property color valueIconColor: valueColor
    property var valueTextStyle: Theme.text.description

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
            visible: root.valueIconSource.toString().length > 0
            Layout.preferredWidth: visible ? root.valueIconSize : 0
            Layout.preferredHeight: visible ? root.valueIconSize : 0
            source: root.valueIconSource
            color: root.valueIconColor
            size: root.valueIconSize
        }
    }
}
