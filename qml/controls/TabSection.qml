// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQml.Models 2.15

QtObject {
    id: section

    readonly property string kind: "section"
    property string title: ""
    property bool enabled: true
    property int displayModes: TabView.Sidebar | TabView.TabBar
    default property list<QtObject> tabs

    // Optional model + delegate for dynamically-generated tabs.
    // Static `tabs` (declared as children) and dynamic tabs are combined
    // into `effectiveTabs` which TabView iterates.
    property var model: null
    property Component delegate: null

    property var _dynamicTabs: []
    // Dynamic (model-driven) tabs come first; declarative static `tabs` follow.
    // This lets authors add trailing items like "+ Add Wallet" after the dynamic list.
    readonly property var effectiveTabs: _dynamicTabs.concat(tabs)

    property Instantiator _instantiator: Instantiator {
        active: section.model !== null && section.delegate !== null
        model: section.model
        delegate: section.delegate
        onObjectAdded: (idx, obj) => section._refreshDynamic()
        onObjectRemoved: (idx, obj) => section._refreshDynamic()
    }

    function _refreshDynamic() {
        var arr = []
        for (var i = 0; i < _instantiator.count; ++i) {
            var instance = _instantiator.objectAt(i)
            if (instance)
                arr.push(instance)

        }
        _dynamicTabs = arr
    }
}
