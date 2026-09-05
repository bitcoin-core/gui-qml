// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick.Layouts 1.15
import "../controls"

InfoBanner {
    id: root
    Layout.fillWidth: true
    Layout.maximumWidth: 450
    Layout.alignment: Qt.AlignHCenter
    radius: 15
    iconSource: "image://images/info-filled"
    message: qsTr("Restart the application for these changes to take effect.")
    bannerLayout: InfoBanner.Layout.Horizontal
    horizontalContentMargin: 15
    verticalContentMargin: 10
    contentSpacing: 10
    iconSize: 24
    textRightMargin: iconSize
    messageFontPixelSize: 15
    messageLineHeight: 21
    backgroundColor: Qt.rgba(Theme.color.blue.r, Theme.color.blue.g, Theme.color.blue.b, 0.25)
    iconColor: Theme.color.blue
    messageColor: Theme.color.blue
}
