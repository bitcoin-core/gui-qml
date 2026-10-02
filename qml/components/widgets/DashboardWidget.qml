// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import "../../controls"
import "WidgetMetrics.js" as WidgetMetrics
import "WidgetActivity.js" as WidgetActivity

// Content contract. WidgetFrame supplies these values; placement and saving
// belong to the dashboard. Bind expensive visual work to active when needed.
Item {
    id: root
    property bool preview: false
    property var activityTarget: null
    property string activityProperty: "active"
    property bool activityRequested: active
    property bool activityInitialized: false
    function updateActivity() {
        if (activityInitialized) WidgetActivity.update(root, preview ? null : activityTarget, activityProperty, activityRequested)
    }
    onActivityTargetChanged: updateActivity()
    onActivityPropertyChanged: updateActivity()
    onActivityRequestedChanged: updateActivity()
    onPreviewChanged: updateActivity()
    Component.onCompleted: { activityInitialized = true; updateActivity() }
    Component.onDestruction: WidgetActivity.remove(root)

    property int columnSpan: 1
    property int rowSpan: 1
    property bool active: true
    // Picker previews render without activating live model polling. The picker
    // overrides this binding to pause previews outside its scroll viewport.
    property bool renderingActive: active || preview
    // The host supplies the actual cell size. The fallback also supports previews.
    property real cellSize: Math.min((width + 2 * WidgetMetrics.contentPadding - (columnSpan - 1) * 12) / columnSpan,
                                    (height + 2 * WidgetMetrics.contentPadding - (rowSpan - 1) * 12) / rowSpan)
    readonly property bool largeTypography: columnSpan >= 3 && rowSpan >= 2
    readonly property var primaryValueStyle: largeTypography
        ? Theme.text.widgetPrimaryValueLarge : Theme.text.widgetPrimaryValue
    readonly property font primaryValueFont: scaledFont(primaryValueStyle)
    readonly property var primaryLabelStyle: largeTypography
        ? Theme.text.widgetPrimaryLabelLarge : Theme.text.widgetPrimaryLabel
    readonly property var secondaryValueStyle: largeTypography
        ? Theme.text.widgetSecondaryValueLarge : Theme.text.widgetSecondaryValue
    readonly property var footerLabelStyle: largeTypography
        ? Theme.text.widgetFooterLabelLarge : Theme.text.widgetFooterLabel
    readonly property var footerValueStyle: largeTypography
        ? Theme.text.widgetFooterValueLarge : Theme.text.widgetFooterValue
    readonly property font primaryLabelFont: primaryLabelStyle.font
    readonly property font secondaryValueFont: scaledFont(secondaryValueStyle)
    readonly property font footerLabelFont: footerLabelStyle.font
    readonly property font footerValueFont: footerValueStyle.font
    readonly property real fontScale: Math.max(0.85, cellSize / 160)
    function scaledFont(style) {
        return Theme.fontWithStyle(style.family, style.styleName, scaledPixelSize(style))
    }
    function scaledPixelSize(style) { return Math.round(style.pixelSize * fontScale) }

    readonly property bool compact: columnSpan === 1 && rowSpan === 1
    readonly property bool expanded: rowSpan > 1 && width >= 340 && height >= 200
}
