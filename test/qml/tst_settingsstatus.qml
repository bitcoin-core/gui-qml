// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtTest 1.2
import "../../qml/components"

TestCase {
    name: "SettingsStatus"
    when: windowShown
    width: 520
    height: 720

    Component {
        id: connectionSettingsComponent

        ConnectionSettings {
            width: 460
            height: 680
        }
    }

    function init() {
        testSetup.failOnWarnings()
        optionsModel.clearCoreSettingStatusesForTest()
    }

    function createConnectionSettingsPage() {
        const page = createTemporaryObject(connectionSettingsComponent, this)
        verify(page !== null)
        return page
    }

    function test_command_line_status_disables_setting_row() {
        const text = "Set by command line (-listen). Remove the command-line option to change this here."
        optionsModel.setCoreSettingStatusForTest("listen", false, "command_line", text, false)

        const page = createConnectionSettingsPage()
        const row = findChild(page, "listenSetting")
        verify(row !== null)
        compare(row.state, "DISABLED")
        verify(!row.enabled)
        compare(row.infoText, text)
        compare(row.showInfoText, true)
    }

    function test_config_status_shows_gui_override_copy() {
        const text = "Loaded from bitcoin.conf. Changing this saves a GUI override in settings.json."
        optionsModel.setCoreSettingStatusForTest("listen", true, "bitcoin_conf", text, true)

        const page = createConnectionSettingsPage()
        const row = findChild(page, "listenSetting")
        verify(row !== null)
        compare(row.state, "FILLED")
        verify(row.enabled)
        compare(row.infoText, text)
        compare(row.showInfoText, true)
    }
}
