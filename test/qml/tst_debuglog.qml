// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtTest 1.2
import "../../qml/pages/settings"

TestCase {
    name: "DebugLog"
    when: windowShown
    width: 520
    height: 720

    Component {
        id: debugLogComponent

        SettingsDebugLog {
            width: 480
            height: 680
        }
    }

    function init() {
        debugLogModel.reset()
    }

    function createPage() {
        const page = createTemporaryObject(debugLogComponent, this)
        verify(page !== null)
        return page
    }

    // A failed open surfaces the model's error message on the page. The label
    // is hidden (text bound to the empty openError) until the open fails.
    function test_open_failure_shows_error() {
        const page = createPage()
        const errorText = findChild(page, "debugLogOpenErrorText")
        verify(errorText !== null)
        compare(errorText.text, "")

        debugLogModel.setOpenLogFileResult(false, "Could not open debug log file.")
        debugLogModel.openLogFile()

        compare(errorText.text, "Could not open debug log file.")
    }

    // A later successful open clears the previously shown error.
    function test_successful_open_clears_error() {
        const page = createPage()
        const errorText = findChild(page, "debugLogOpenErrorText")
        verify(errorText !== null)

        debugLogModel.setOpenLogFileResult(false, "Could not open debug log file.")
        debugLogModel.openLogFile()
        compare(errorText.text, "Could not open debug log file.")

        debugLogModel.setOpenLogFileResult(true, "")
        debugLogModel.openLogFile()

        compare(errorText.text, "")
    }

    // Searching and handing the log to another application both need readable
    // log content, so they are disabled while there is none.
    function test_unreadable_log_disables_search_and_export() {
        const page = createPage()
        const search = findChild(page, "debugLogSearchField")
        const exportButton = findChild(page, "debugLogExportButton")
        const refresh = findChild(page, "debugLogRefreshButton")
        verify(search !== null)
        verify(exportButton !== null)
        verify(refresh !== null)
        verify(search.enabled)
        verify(exportButton.enabled)

        debugLogModel.setLogAvailable(false)

        compare(search.enabled, false)
        compare(exportButton.enabled, false)
        // Refresh stays available: it is how the user recovers once the log
        // exists again.
        compare(refresh.enabled, true)

        debugLogModel.setLogAvailable(true)

        compare(search.enabled, true)
        compare(exportButton.enabled, true)
    }

    // The open error is about the last open attempt on this visit, so it must
    // not survive navigation: a freshly created page clears any stale error
    // left in the long-lived model.
    function test_reentering_page_clears_stale_error() {
        const firstPage = createPage()
        debugLogModel.setOpenLogFileResult(false, "Could not open debug log file.")
        debugLogModel.openLogFile()
        compare(findChild(firstPage, "debugLogOpenErrorText").text,
                "Could not open debug log file.")
        firstPage.destroy()
        wait(0)

        const secondPage = createPage()
        const errorText = findChild(secondPage, "debugLogOpenErrorText")
        verify(errorText !== null)
        compare(errorText.text, "")
    }
}
