// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

RowLayout {
    id: root
    property alias leftDetail: left_detail.sourceComponent
    property alias middleDetail: middle_detail.sourceComponent
    property alias rightDetail: right_detail.sourceComponent

    height: 46
    spacing: 0
    Layout.fillWidth: true
    RowLayout {
        Layout.preferredWidth: parent.width / 3
        Loader {
            Layout.alignment: Qt.AlignLeft
            id: left_detail
            active: true
            visible: active
            sourceComponent: root.leftDetail
        }
    }
    RowLayout {
        Layout.preferredWidth: parent.width / 3
        Loader {
            Layout.alignment: Qt.AlignHCenter
            id: middle_detail
            active: true
            visible: active
            sourceComponent: root.middleDetail
        }
    }
    RowLayout {
        Layout.preferredWidth: parent.width / 3
        Loader {
            id: right_detail
            Layout.rightMargin: 10
            Layout.alignment: Qt.AlignRight
            active: true
            visible: active
            sourceComponent: root.rightDetail
        }
    }
}
