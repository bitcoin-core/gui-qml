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
            { component: developer, field: "developerScriptThreadsField", property: "scriptThreads", value: -1 }
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
        const input = findChild(page, spec.field)
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
        const input = findChild(page, spec.field)
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
        verify(page[spec.error].length > 0)
    }

    function test_typed_out_of_range_data() {
        const values = [
            { component: storage, field: "pruneTargetInput", property: "pruneSizeGB", error: "pruneTargetError", text: "0" },
            { component: mempool, field: "mempoolSizeLimitInput", property: "maxMempoolSizeMB", error: "mempoolSizeError", text: String(optionsModel.minMaxMempoolSizeMB - 1) },
            { component: developer, field: "developerDbcacheField", property: "dbcacheSizeMiB", error: "dbcacheError", text: String(optionsModel.minDbcacheSizeMiB - 1) },
            { component: developer, field: "developerDbcacheField", property: "dbcacheSizeMiB", error: "dbcacheError", text: "2147483648" },
            { component: developer, field: "developerScriptThreadsField", property: "scriptThreads", error: "scriptThreadsError", text: String(optionsModel.maxScriptThreads + 1) }
        ]
        const rows = []
        for (const value of values) {
            for (const finish of ["enter", "blur"]) {
                rows.push({ tag: value.field + "-" + value.text + "-" + finish, value: value, finish: finish })
            }
        }
        return rows
    }

    function test_typed_out_of_range(data) {
        const spec = data.value
        const initial = optionsModel[spec.property]
        const page = createTemporaryObject(spec.component, testCase)
        verify(page !== null)
        const input = findChild(page, spec.field)
        verify(input !== null)
        input.validator.locale = "en_US"
        input.forceActiveFocus()
        input.selectAll()
        keyClick(Qt.Key_Backspace)
        for (let i = 0; i < spec.text.length; ++i) keyClick(spec.text.charAt(i))
        compare(input.text, spec.text)
        // Out-of-range input stays intermediate, so Return and focus loss
        // reach the commit and its range error only through fixup().
        verify(!input.acceptableInput)
        if (data.finish === "enter") keyClick(Qt.Key_Return)
        else testCase.forceActiveFocus()
        compare(optionsModel[spec.property], initial)
        compare(input.text, spec.text)
        verify(page[spec.error].length > 0)
    }
}
