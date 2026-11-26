// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtTest 1.2
import org.bitcoincore.qt 1.0
import "../../qml/controls"

TestCase {
    name: "ActivityCalendar"
    when: windowShown
    width: 480
    height: 640

    Component {
        id: calendarComponent
        ActivityCalendar {
            width: 360
        }
    }

    function createCalendar(properties) {
        const calendar = createTemporaryObject(calendarComponent, this, properties || {})
        verify(calendar !== null)
        return calendar
    }

    function test_dates_use_the_gregorian_locale_by_default() {
        const calendar = createCalendar()
        compare(calendar.dateLocale.name, DateLocale.gregorian.name)
    }

    function test_month_names_keep_the_locale_script() {
        const calendar = createCalendar({ dateLocale: Qt.locale("sr_Latn_RS"), calYear: 2026, calMonth: 10 })
        compare(findChild(calendar, "calendarMonthLabel").text, "novembar 2026")
        const day = new Date(2026, 10, 10)
        compare(calendar.formatRangeDate(day), day.toLocaleDateString(Qt.locale("sr_Latn_RS"), Locale.ShortFormat))
        const label = calendar.dayCellAccessibleName({ modelData: day })
        verify(label.indexOf("novembar") >= 0, label)
    }

    function test_weekday_names_use_the_date_locale() {
        const locale = Qt.locale("sr_Latn_RS")
        const calendar = createCalendar({ dateLocale: locale })
        const children = findChild(calendar, "calendarWeekdays").children
        const names = []
        for (let i = 0; i < children.length; ++i) {
            if (children[i].text !== undefined)
                names.push(children[i].text)
        }
        compare(names.length, 7)
        for (let day = 1; day <= 7; ++day)
            verify(names.indexOf(locale.dayName(day, Locale.ShortFormat)) >= 0, names.join(" "))
    }
}
