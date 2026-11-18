// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
import QtQuick
import QtTest
import org.bitcoincore.qt 1.0
import "../../qml/components"
import "../../qml/controls"
TestCase {
    id: testCase
    name: "EffectiveSettings"
    when: windowShown
    visible: true
    width: 1000
    height: 900
    Component { id: viewComponent; SettingsView { width: 1000; height: 900; selectedSectionId: "connection" } }
    Component { id: doneSpy; SignalSpy { signalName: "doneClicked" } }
    function init() { optionsModel.resetSettingsEditorsForTest(); optionsModel.settingsReady = true; optionsModel.settingsPending = false; optionsModel.settingsError = "" }
    function view() { const result = createTemporaryObject(viewComponent, testCase); verify(result !== null); verify(result.navigation.editor !== null); tryCompare(result.navigation.editor, "fresh", true); return result }
    function edit(result, key, text) { const input = findChild(result, key + "SettingInput"); verify(input !== null); input.text = text; input.textEdited(); tryCompare(result.navigation.editor.entry(key), "text", text); tryCompare(result.navigation.editor, "inputPending", false) }
    function save(result) { const button = findChild(result, "settingsSaveBatchButton"); verify(button !== null); tryCompare(button, "enabled", true); button.clicked(); tryVerify(() => result.navigation.editor.lastSavePersisted); tryCompare(result.navigation.editor, "busy", false) }
    function question(result, target) { result.selectSection(target); tryCompare(result.navigation.editor, "closeQuestion", true); tryCompare(findChild(result, "settingsCloseQuestion"), "visible", true) }
    function test_navigation_cannot_skip_an_input_waiting_for_domain() { const page = view(); const input = findChild(page, "proxySettingInput"); input.text = "127.0.0.1:9250"; input.textEdited(); page.navigation.request("storage", false); tryCompare(page.navigation.editor, "closeQuestion", true); compare(page.selectedSectionId, "connection"); compare(page.navigation.pendingSection, "storage"); compare(optionsModel.settingsWritesForTest(), 0); findChild(findChild(page, "settingsCloseQuestion").contentItem, "settingsCloseKeep").clicked(); tryCompare(page.navigation.editor, "closeQuestion", false); compare(page.navigation.editor.entry("proxy").text, "127.0.0.1:9250"); verify(page.navigation.editor.dirty) }
    // Relative luminance contrast (WCAG 2) between two colors.
    function contrast(a, b) { const lum = c => [c.r, c.g, c.b].map(v => v <= 0.04045 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4)).reduce((sum, v, i) => sum + v * [0.2126, 0.7152, 0.0722][i], 0); const la = lum(a), lb = lum(b); return (Math.max(la, lb) + 0.05) / (Math.min(la, lb) + 0.05) }
    function test_text_settings_use_the_theme_field_surface_data() { const rows = []; for (let i = 0; i < Theme.palettes.length; ++i) rows.push({tag: Theme.palettes[i].paletteId, paletteId: Theme.palettes[i].paletteId}); return rows }
    // Valera: a platform TextField kept its light native background under
    // dark-theme text on Windows. With either palette, and a platform palette
    // of the opposite brightness, the field draws the Theme surface and its
    // text stays readable.
    function test_text_settings_use_the_theme_field_surface(data) {
        const originalPaletteId = Theme.paletteId
        try {
            Theme.paletteId = data.paletteId
            const page = view(); const input = findChild(page, "proxySettingInput"); verify(input !== null); verify(input.background !== null)
            input.palette.base = Theme.dark ? "#ffffff" : "#000000"; input.palette.text = Theme.dark ? "#000000" : "#ffffff"
            verify(Qt.colorEqual(input.background.color, Theme.color.surface1), String(input.background.color))
            verify(Qt.colorEqual(input.color, Theme.color.neutral9), String(input.color))
            verify(contrast(input.color, input.background.color) >= 4.5)
        } finally {
            Theme.paletteId = originalPaletteId
        }
    }
    function test_typed_values_and_loading_have_no_write_effect() { const page = view(); compare(page.navigation.editor.entry("listen").booleanValue, true); compare(page.navigation.editor.entry("proxy").text, ""); compare(page.navigation.editor.canSave, false); compare(optionsModel.settingsWritesForTest(), 0) }
    function test_raw_invalid_integer_is_retained_outside_persistence() { const page = view(); page.selectSection("mempool"); edit(page, "maxmempool", "300x"); compare(page.navigation.editor.canSave, false); verify(page.navigation.editor.entry("maxmempool").error.length > 0); compare(optionsModel.settingsWritesForTest(), 0) }
    function test_same_numeric_default_is_an_explicit_override_and_has_a_receipt() { const page = view(); page.selectSection("mempool"); edit(page, "maxmempool", "0300"); tryCompare(page.navigation.editor, "dirty", true); save(page); const result = page.navigation.editor.saveResult; compare(result.outcome, SettingsSaveResult.Saved); verify(result.operationId.indexOf(page.navigation.editor.editorId + ":") === 0); verify(result.actualValues.indexOf("maxmempool = 300") >= 0); compare(optionsModel.settingsWritesForTest(), 1); edit(page, "maxmempool", "0300"); compare(page.navigation.editor.dirty, false); compare(page.navigation.editor.canSave, false); compare(optionsModel.settingsWritesForTest(), 1) }
    function test_native_validation_failure_keeps_the_literal_and_writes_nothing() { const page = view(); optionsModel.rejectSettingsForTest("Captured native policy rejection"); edit(page, "proxy", "127.0.0.1:9050"); findChild(page, "settingsSaveBatchButton").clicked(); tryCompare(page.navigation.editor, "error", "Captured native policy rejection"); compare(page.navigation.editor.entry("proxy").text, "127.0.0.1:9050"); compare(page.navigation.editor.dirty, true); compare(optionsModel.settingsWritesForTest(), 0) }
    function test_keep_editing_preserves_the_original_navigation_and_draft() { const page = view(); edit(page, "proxy", "127.0.0.1:9050"); question(page, "storage"); findChild(findChild(page, "settingsCloseQuestion").contentItem, "settingsCloseKeep").clicked(); tryCompare(page.navigation.editor, "closeQuestion", false); compare(page.selectedSectionId, "connection"); compare(page.navigation.editor.entry("proxy").text, "127.0.0.1:9050"); compare(page.navigation.editor.dirty, true); compare(optionsModel.settingsWritesForTest(), 0) }
    // Valera: Escape answers the leave question with Keep editing.
    function test_escape_keeps_editing() { const page = view(); edit(page, "proxy", "127.0.0.1:9050"); question(page, "storage"); const alert = findChild(page, "settingsCloseQuestion"); tryCompare(alert, "opened", true); keyClick(Qt.Key_Escape); tryCompare(page.navigation.editor, "closeQuestion", false); compare(page.selectedSectionId, "connection"); compare(page.navigation.editor.entry("proxy").text, "127.0.0.1:9050"); compare(optionsModel.settingsWritesForTest(), 0) }
    function test_discard_navigation_opens_a_fresh_editor_without_a_write() { const page = view(); const original = page.navigation.editor.editorId; edit(page, "proxy", "127.0.0.1:9050"); question(page, "storage"); findChild(findChild(page, "settingsCloseQuestion").contentItem, "settingsCloseDiscard").clicked(); tryCompare(page, "selectedSectionId", "storage"); tryCompare(page.navigation.editor, "fresh", true); verify(page.navigation.editor.editorId !== original); compare(page.navigation.editor.entry("proxy").text, ""); compare(optionsModel.settingsWritesForTest(), 0) }
    function test_done_waits_for_the_original_save_outcome() { const page = view(); const spy = createTemporaryObject(doneSpy, testCase, {target: page}); edit(page, "proxy", "127.0.0.1:9050"); page.navigation.request("", true); tryCompare(page.navigation.editor, "closeQuestion", true); compare(spy.count, 0); compare(optionsModel.settingsWritesForTest(), 0); findChild(findChild(page, "settingsCloseQuestion").contentItem, "settingsCloseSave").clicked(); tryCompare(spy, "count", 1); compare(optionsModel.settingsWritesForTest(), 1) }
    function test_an_open_consent_cannot_retarget_to_another_section() { const page = view(); edit(page, "proxy", "127.0.0.1:9050"); question(page, "storage"); page.navigation.request("about", false); compare(page.navigation.pendingSection, "storage"); findChild(findChild(page, "settingsCloseQuestion").contentItem, "settingsCloseDiscard").clicked(); tryCompare(page, "selectedSectionId", "storage"); compare(optionsModel.settingsWritesForTest(), 0) }
    function test_stale_close_consent_cannot_discard_a_successor_edit() { const page = view(); edit(page, "proxy", "127.0.0.1:9050"); question(page, "storage"); const editor = page.navigation.editor; const old = editor.closePrompt; verify(old !== null); editor.entry("proxy").editText("127.0.0.1:9150"); tryCompare(editor.entry("proxy"), "text", "127.0.0.1:9150"); tryVerify(() => editor.closePrompt !== null && editor.closePrompt !== old); /* The successor edit retired the old question; only the new prompt can answer. */ wait(0); compare(page.selectedSectionId, "connection"); compare(editor.closeQuestion, true); compare(editor.dirty, true); editor.closePrompt.answer(TextEditClosePrompt.KeepEditing); tryCompare(editor, "closeQuestion", false) }
    function test_prune_mode_changes_preserve_550_mib_exactly() { const page = view(); page.selectSection("storage"); compare(page.navigation.editor.entry("prune").text, "550"); page.navigation.editor.setPruneMode(SettingsEditorModel.Disabled); tryCompare(page.navigation.editor.entry("prune"), "text", "0"); page.navigation.editor.setPruneMode(SettingsEditorModel.Automatic); tryCompare(page.navigation.editor.entry("prune"), "text", "550"); save(page); verify(page.navigation.editor.saveResult.actualValues.indexOf("prune = 550") >= 0) }
    function test_restart_notice_uses_saved_policy_and_remains_global() { const page = view(); page.navigation.editor.entry("listen").editBoolean(false); tryCompare(page.navigation.editor, "canSave", true); compare(page.navigation.editor.restartRequired, false); save(page); compare(page.navigation.editor.restartRequired, true); page.selectSection("about"); compare(page.navigation.editor.restartNoticeVisible, true); verify(findChild(page, "settingsRestartNotice").visible); page.navigation.editor.acknowledgeRestart(); tryCompare(page.navigation.editor, "restartNoticeVisible", false); compare(page.navigation.editor.restartRequired, true) }
    function test_live_only_changes_do_not_request_a_restart() { const page = view(); page.navigation.editor.entry("natpmp").editBoolean(true); tryCompare(page.navigation.editor, "canSave", true); save(page); compare(page.navigation.editor.restartRequired, false); compare(page.navigation.editor.entry("natpmp").effectiveText, "true") }
    function test_multiple_fields_share_one_native_batch() { const page = view(); edit(page, "proxy", "127.0.0.1:9050"); edit(page, "onion", "127.0.0.1:9150"); save(page); compare(optionsModel.settingsWritesForTest(), 1); verify(page.navigation.editor.saveResult.actualValues.indexOf("proxy = 127.0.0.1:9050") >= 0); verify(page.navigation.editor.saveResult.actualValues.indexOf("onion = 127.0.0.1:9150") >= 0) }
}
