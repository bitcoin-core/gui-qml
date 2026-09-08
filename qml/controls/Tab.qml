// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15

QtObject {
    readonly property string kind: "tab"
    property string title: ""
    property string iconSource: ""
    property string activeIconSource: ""
    property Component contentComponent: null
    property url contentUrl: ""
    property bool enabled: true
    property int displayModes: TabView.Sidebar | TabView.TabBar
    property bool pinToBottom: false
    property bool dimmed: false

    // Action-only tabs do not switch the active content when clicked;
    // instead they fire `triggered()` for the host to handle (e.g. open a popup).
    property bool actionOnly: false
    signal triggered()

    // Fired when this tab becomes the current tab (initial load + every switch).
    signal selected()
}
