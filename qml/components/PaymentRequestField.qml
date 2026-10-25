// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import "../controls"

// A payment request detail: shows the saved value until edited.
TextFieldRow {
    id: root
    property alias label: root.title
    property bool editable: true
    property alias unitControl: root.trailingAccessory

    tracksValue: true
    enabled: editable
    showDivider: false
    minimumRowHeight: 56
    titleColor: editable ? Theme.color.neutral7 : Theme.color.disabledContent
    fieldHeight: Theme.size.control
    fieldWidth: Math.max(80, Math.min(260, width * 0.53
        - (trailingAccessoryWidth > 0 ? trailingAccessoryWidth + Theme.space.xs : 0)))
    onEditableChanged: if (!editable) reset()
}
