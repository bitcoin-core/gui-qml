// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

.pragma library

// Several widget instances may share one data source. Keep it active until
// the last active consumer leaves; picker previews never register a consumer.
var consumers = []

function publish(model, property) {
    if (!model) return
    model[property] = consumers.some(function(entry) {
        return entry.model === model && entry.property === property && entry.active
    })
}

function update(widget, model, property, active) {
    var previous = null
    consumers = consumers.filter(function(entry) {
        if (entry.widget !== widget) return true
        previous = entry
        return false
    })
    if (model) consumers.push({widget: widget, model: model, property: property, active: active})
    if (previous && (previous.model !== model || previous.property !== property)) publish(previous.model, previous.property)
    publish(model, property)
}

function remove(widget) { update(widget, null, "", false) }
