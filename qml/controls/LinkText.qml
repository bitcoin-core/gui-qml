// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15

// A sentence with one external link inside it. LinkButton is the
// standalone action; this is for a link that is part of the text around
// it. Like the other external links (LinkRow, the "Show in" menu items) the
// link is in the text color with the export icon after it, and it is
// underlined so it stands out from the sentence without relying on color.
// The sentence is one Tab stop named after its link; Enter or Space opens
// it. Like LinkRow, it only reports the activation: the page confirms
// external links with ExternalPopup.
CoreText {
    id: root

    // The sentence, with %1 where the link goes.
    property string sentence: ""
    // The words of the link, as plain text.
    property string linkText: ""
    property url link: ""
    // Marks the link as leaving the app, like LinkRow's icon. Must be one of
    // the app's monochrome icons, which are drawn in the link color.
    property url linkIconSource: "image://images/export"
    readonly property color linkTextColor: enabled ? Theme.color.neutral9 : Theme.color.disabledContent

    // Same as LinkRow, the shared row for external links.
    signal activated(url link)

    function escaped(value) {
        return value.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/"/g, "&quot;")
    }

    textFormat: Text.StyledText
    // The icon follows the underlined words directly (its image has its own
    // margin), outside the link markup so the underline stops at the words.
    // It is drawn in the link color by the image provider, since an image in
    // text can't be recolored like Icon, and centered on the line, like
    // LinkRow's icon beside its text. It still belongs to the link: see
    // linkArea.
    readonly property int iconSize: Math.round(font.pixelSize * 1.15)
    readonly property string iconMarkup: linkIconSource.toString().length === 0 ? ""
        : "<img src=\"" + linkIconSource + "?color=" + linkTextColor.toString().substring(1, 7)
          + "\" width=\"" + iconSize + "\" height=\"" + iconSize + "\" align=\"middle\">"
    text: sentence.arg("<a href=\"" + escaped(link.toString()) + "\">" + escaped(linkText) + "</a>" + iconMarkup)
    color: enabled ? Theme.color.neutral7 : Theme.color.disabledContent
    linkColor: linkTextColor

    // Where the link's words and its icon are drawn, one area for each line
    // the link takes. Text only reports links under a point, so these are
    // measured with linkAt(), and the last line's area is widened by the
    // icon that follows the last word. Clicks, the hand cursor and the focus
    // ring use these areas, so the icon acts as part of the link, Tab
    // outlines the link rather than the whole sentence, and the sentence
    // around a link that wraps stays plain text.
    property var linkAreas: []
    // Each row is checked every 8 px to find the link, then every 2 px only
    // around its two ends. For a link at least 8 px wide on each line, that
    // finds the same stretch as checking every 2 px, with a quarter of the
    // linkAt() calls, which add up when a resize measures again.
    function linkStretchAt(y) {
        let left = -1
        let right = -1
        for (let x = 0; x < width; x += 8) {
            if (linkAt(x, y).length === 0) continue
            if (left < 0) left = x
            right = x
        }
        if (left < 0) return null
        for (let x = Math.max(0, left - 6); x < left; x += 2) {
            if (linkAt(x, y).length > 0) {
                left = x
                break
            }
        }
        for (let x = right + 6; x > right; x -= 2) {
            if (x < width && linkAt(x, y).length > 0) {
                right = x
                break
            }
        }
        return { left: left, right: right }
    }
    function measureLinkArea() {
        const lines = []
        let line = null
        for (let y = 0; y < height; y += 2) {
            const stretch = linkStretchAt(y)
            const left = stretch === null ? -1 : stretch.left
            const right = stretch === null ? -1 : stretch.right
            if (left < 0) {
                line = null
                continue
            }
            // Every row of a line covers the same stretch of the link; a
            // different stretch is the link's next line.
            if (line !== null && line.left === left && line.right === right) {
                line.bottom = y
            } else {
                line = { left: left, right: right, top: y, bottom: y }
                lines.push(line)
            }
        }
        if (lines.length > 0 && iconMarkup.length > 0) lines[lines.length - 1].right += iconSize
        linkAreas = lines.map(function(l) {
            const right = Math.min(width, l.right + 2)
            return Qt.rect(l.left, l.top, right - l.left, l.bottom - l.top + 2)
        })
    }
    onWidthChanged: Qt.callLater(measureLinkArea)
    onHeightChanged: Qt.callLater(measureLinkArea)
    onTextChanged: Qt.callLater(measureLinkArea)
    Component.onCompleted: Qt.callLater(measureLinkArea)
    textStyle: Theme.text.description
    wrap: true
    horizontalAlignment: Text.AlignLeft
    activeFocusOnTab: enabled
    Accessible.role: Accessible.Link
    // A link that ends its sentence can carry the period, so the icon comes
    // last; the link's name leaves it out.
    Accessible.name: linkText.replace(/[.!?:;,]+$/, "")
    Accessible.description: sentence.arg(linkText)
    Accessible.onPressAction: if (enabled) root.activated(link)

    // The Text reports the href as a string; report the link itself, as LinkRow does.
    onLinkActivated: if (enabled) root.activated(root.link)
    Keys.onReturnPressed: root.activated(link)
    Keys.onEnterPressed: root.activated(link)
    Keys.onSpacePressed: root.activated(link)

    Repeater {
        model: root.linkAreas
        MouseArea {
            objectName: root.objectName.length > 0
                ? root.objectName + "LinkArea" + (index > 0 ? index : "") : ""
            x: modelData.x
            y: modelData.y
            width: modelData.width
            height: modelData.height
            enabled: root.enabled
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: root.activated(root.link)

            FocusBorder {
                visible: root.activeFocus
            }
        }
    }
}
