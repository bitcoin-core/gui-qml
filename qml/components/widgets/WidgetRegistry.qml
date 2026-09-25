// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQml 2.15

QtObject {
    id: root
    property list<WidgetDefinition> definitions: []

    // Adapt the typed definitions to the layout model's data-only catalog.
    readonly property var catalog: {
        const result = []
        const seen = []
        for (let i = 0; i < definitions.length; ++i) {
            const definition = definitions[i]
            const error = definition.validationError
            if (error !== "") {
                console.warn("WidgetRegistry: " + definition.widgetId + ": " + error)
                continue
            }
            if (seen.indexOf(definition.widgetId) !== -1) {
                console.warn("WidgetRegistry: duplicate widgetId " + definition.widgetId)
                continue
            }
            seen.push(definition.widgetId)
            const sizes = []
            for (let j = 0; j < definition.sizes.length; ++j) {
                const size = definition.sizes[j]
                sizes.push({columns: size.columns, rows: size.rows, label: size.label})
            }
            result.push({id: definition.widgetId, title: definition.title, source: definition.source,
                         sizes: sizes, defaultSize: definition.defaultSize})
        }
        return result
    }
}
