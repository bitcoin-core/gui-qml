// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15

import "../../../controls"
import "../../../components"

SettingsPage {
    id: root

    // Lays the content out as on a page this wide (0: the real width).
    property real previewWidth: 0
    readonly property real layoutWidth: previewWidth > 0 ? Math.min(previewWidth, width) : width

    contentHorizontalPadding: Theme.space.pagePadding(layoutWidth) + (width - layoutWidth) / 2

    // The motion mode is switched for the design system pages only. Settings
    // keeps the stack alive when you move to another section, so reset it when
    // the page on top is hidden. A page covered by the next one, or popped
    // back to the previous one, keeps it.
    onVisibleChanged: {
        if (!visible && StackView.view && StackView.view.currentItem === root) Theme.motion.resetMode()
    }
}
