// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Layouts 1.15

DesignSystemPage {
    id: root

    property Component section
    readonly property alias sectionItem: sectionLoader.item

    Loader {
        id: sectionLoader
        Layout.fillWidth: true
        sourceComponent: root.section
        onLoaded: {
            // The Layout section reads the previewed page width.
            if ("layoutWidth" in item) item["layoutWidth"] = Qt.binding(function() { return root.layoutWidth })
        }
    }
}
