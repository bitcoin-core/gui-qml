// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtTest 1.2
import "../../qml/controls/utils.js" as Utils

TestCase {
    name: "RequestPayment"
    when: windowShown
    width: 400
    height: 200

    function test_formatRelativeTime_empty() {
        compare(Utils.formatRelativeTime(""), "")
        compare(Utils.formatRelativeTime(null), "")
        compare(Utils.formatRelativeTime(undefined), "")
    }

    function test_formatRelativeTime_just_now() {
        var now = new Date()
        compare(Utils.formatRelativeTime(now.toISOString()), "just now")
    }

    function test_formatRelativeTime_minutes() {
        var d = new Date()
        d.setMinutes(d.getMinutes() - 5)
        compare(Utils.formatRelativeTime(d.toISOString()), "5 minutes ago")

        var d1 = new Date()
        d1.setMinutes(d1.getMinutes() - 1)
        compare(Utils.formatRelativeTime(d1.toISOString()), "1 minute ago")
    }

    function test_formatRelativeTime_hours() {
        var d = new Date()
        d.setHours(d.getHours() - 3)
        compare(Utils.formatRelativeTime(d.toISOString()), "3 hours ago")

        var d1 = new Date()
        d1.setHours(d1.getHours() - 1)
        compare(Utils.formatRelativeTime(d1.toISOString()), "1 hour ago")
    }

    function test_formatRelativeTime_days() {
        var d = new Date()
        d.setDate(d.getDate() - 2)
        compare(Utils.formatRelativeTime(d.toISOString()), "2 days ago")

        var d1 = new Date()
        d1.setDate(d1.getDate() - 1)
        compare(Utils.formatRelativeTime(d1.toISOString()), "1 day ago")
    }

    Component {
        id: btcValidatorComponent
        TextField {
            validator: RegularExpressionValidator {
                regularExpression: /^(0|[1-9]\d{0,7})(\.\d{0,8})?$/
            }
            maximumLength: 17
        }
    }

    Component {
        id: satValidatorComponent
        TextField {
            validator: RegularExpressionValidator {
                regularExpression: /^(0|[1-9]\d{0,15})$/
            }
            maximumLength: 16
        }
    }

    function test_btcValidator_accepts_valid_amounts() {
        var field = createTemporaryObject(btcValidatorComponent, this)
        verify(field !== null)

        field.text = "0"
        compare(field.acceptableInput, true)

        field.text = "1.00000000"
        compare(field.acceptableInput, true)

        field.text = "21000000.00000000"
        compare(field.acceptableInput, true)

        field.text = "0.00000001"
        compare(field.acceptableInput, true)
    }

    function test_btcValidator_rejects_invalid_amounts() {
        var field = createTemporaryObject(btcValidatorComponent, this)
        verify(field !== null)

        field.text = "00"
        compare(field.acceptableInput, false)

        field.text = "1.000000001"
        compare(field.acceptableInput, false)

        field.text = "-1"
        compare(field.acceptableInput, false)

        field.text = "abc"
        compare(field.acceptableInput, false)
    }

    function test_satValidator_accepts_valid_amounts() {
        var field = createTemporaryObject(satValidatorComponent, this)
        verify(field !== null)

        field.text = "0"
        compare(field.acceptableInput, true)

        field.text = "100000000"
        compare(field.acceptableInput, true)

        field.text = "2100000000000000"
        compare(field.acceptableInput, true)
    }

    function test_satValidator_rejects_invalid_amounts() {
        var field = createTemporaryObject(satValidatorComponent, this)
        verify(field !== null)

        field.text = "00"
        compare(field.acceptableInput, false)

        field.text = "1.5"
        compare(field.acceptableInput, false)

        field.text = "-100"
        compare(field.acceptableInput, false)
    }
}
