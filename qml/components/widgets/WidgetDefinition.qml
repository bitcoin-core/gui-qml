// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQml 2.15

// Metadata only: registering a widget never instantiates its content.
QtObject {
    required property string widgetId
    required property string title
    required property url source
    required property list<WidgetSize> sizes
    property int defaultSize: 0

    readonly property string validationError: {
        if (widgetId.trim() === "") return "widgetId must not be empty"
        if (title.trim() === "") return "title must not be empty"
        if (source.toString() === "") return "source must not be empty"
        if (sizes.length === 0) return "at least one supported size is required"
        if (defaultSize < 0 || defaultSize >= sizes.length) return "defaultSize must index a supported size"
        const allowed = ["1x1", "2x1", "2x2", "3x2", "3x3"]
        const seen = []
        for (let i = 0; i < sizes.length; ++i) {
            const key = sizes[i].columns + "x" + sizes[i].rows
            if (allowed.indexOf(key) === -1) return "unsupported size " + key
            if (seen.indexOf(key) !== -1) return "duplicate size " + key
            seen.push(key)
        }
        return ""
    }
}
