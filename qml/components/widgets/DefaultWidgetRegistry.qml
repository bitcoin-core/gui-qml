// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQml 2.15

WidgetRegistry {
    definitions: [
        WidgetDefinition {
            widgetId: "blockclock"
            //: Name of the block clock in the dashboard widget picker.
            title: qsTr("Block clock")
            source: Qt.resolvedUrl("BlockClockWidget.qml")
            sizes: [
                WidgetSize { columns: 2; rows: 2 },
                WidgetSize { columns: 3; rows: 3 }
            ]
            defaultSize: 1
        }
    ]
}
