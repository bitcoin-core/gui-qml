// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtTest 1.2
import "../../qml/components"
import "../../qml/pages/settings"

TestCase {
    id: testCase
    name: "IntegerSettings"
    when: windowShown
    visible: true
    width: 800
    height: 665

    Component { id: storage; StorageSettingsPage { width: 600; height: 600 } }
    Component { id: mempool; MempoolSettingsPage { width: 600; height: 600 } }
    Component { id: developer; DeveloperOptions { width: 600 } }
    Component { id: legacyStorage; StorageSettings { width: 600 } }
    Component { id: legacyMempool; MempoolInformationRows { width: 600 } }

    function init() {
        optionsModel.clearCoreSettingStatusesForTest()
        optionsModel.setStorageStatusForTest(false, 2000, "", "")
        optionsModel.prune = true
        optionsModel.pruneSizeGB = 2
        optionsModel.maxMempoolSizeMB = 300
        optionsModel.dbcacheSizeMiB = 450
        optionsModel.scriptThreads = 0
    }

    function test_grouped_input_data() {
        const fields = [
            { component: storage, field: "pruneTargetInput", property: "pruneSizeGB", value: 1000, error: "pruneTargetError" },
            { component: mempool, field: "mempoolSizeLimitInput", property: "maxMempoolSizeMB", value: 10000, error: "mempoolSizeError" },
            { component: developer, field: "developerDbcacheField", property: "dbcacheSizeMiB", value: 1000, error: "dbcacheError" },
            { component: developer, field: "developerScriptThreadsField", property: "scriptThreads", value: -1 },
            { component: legacyStorage, field: "pruneTargetInput", property: "pruneSizeGB", value: 1000 },
            { component: legacyMempool, field: "mempoolSizeLimitRow", property: "maxMempoolSizeMB", value: 10000, legacy: true }
        ]
        const rows = []
        for (let i = 0; i < fields.length; ++i) {
            for (const locale of ["en_US", "de_DE", "ar_EG"]) {
                for (const finish of ["enter", "blur"]) {
                    rows.push({ tag: i + "-" + locale + "-" + finish, field: fields[i], locale: locale, finish: finish })
                }
            }
        }
        return rows
    }

    function test_grouped_input(data) {
        const spec = data.field
        const page = createTemporaryObject(spec.component, testCase)
        verify(page !== null)
        const item = findChild(page, spec.field)
        const input = spec.legacy ? item.loadedItem : item
        verify(input !== null)
        input.validator.locale = data.locale
        input.forceActiveFocus()
        input.selectAll()
        keyClick(Qt.Key_Backspace)
        const text = (spec.value < 0 ? "-" : "")
            + Number(Math.abs(spec.value)).toLocaleString(Qt.locale(data.locale), "f", 0)
        if (data.locale === "ar_EG") {
            // Exercise insertion of localized digits, as with a paste or input method.
            input.insert(0, text)
        } else {
            for (let i = 0; i < text.length; ++i) keyClick(text.charAt(i))
        }
        compare(input.text, text)
        verify(input.acceptableInput)
        if (data.finish === "enter") keyClick(Qt.Key_Return)
        else testCase.forceActiveFocus()
        compare(optionsModel[spec.property], spec.value)
        compare(input.text, String(spec.value))
    }

    function test_malformed_grouping_data() {
        return test_grouped_input_data().filter(row => row.field.property !== "scriptThreads" && row.locale !== "ar_EG")
    }

    function test_malformed_grouping(data) {
        const spec = data.field
        const initial = optionsModel[spec.property]
        const page = createTemporaryObject(spec.component, testCase)
        const item = findChild(page, spec.field)
        const input = spec.legacy ? item.loadedItem : item
        input.validator.locale = data.locale
        input.forceActiveFocus()
        input.selectAll()
        keyClick(Qt.Key_Backspace)
        const text = data.locale === "en_US" ? "1,00" : "1.00"
        for (let i = 0; i < text.length; ++i) keyClick(text.charAt(i))
        compare(input.text, text)
        if (data.finish === "enter") keyClick(Qt.Key_Return)
        else testCase.forceActiveFocus()
        compare(optionsModel[spec.property], initial)
        compare(input.text, text)
        if (spec.error) verify(page[spec.error].length > 0)
        else if (spec.legacy) compare(item.showErrorText, true)
        else compare(findChild(page, "pruneTargetSetting").showErrorText, true)
    }

}
