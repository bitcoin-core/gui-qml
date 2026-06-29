// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0

// Date-range calendar for the Activity custom date range filter: From/To
// display, month/year navigation, a weekday header, and a day grid with range
// selection, hover preview, and keyboard support. It owns the selection state;
// the caller reads startDate/endDate (or startIso/endIso) and valid/hasSelection
// and drives it with seed()/reset()/refreshToday(). It does not touch the model:
// applying or clearing the filter is left to the caller.
ColumnLayout {
    id: root
    spacing: 8

    // Public interface ------------------------------------------------------

    // The picked range as local Date values (null until picked).
    readonly property var startDate: root.customFrom
    readonly property var endDate: root.customTo
    // The picked range as yyyy-MM-dd strings ("" until picked). Pass these to
    // the model rather than the Dates: a JS Date converted straight to QDate can
    // shift a day across time zones.
    readonly property string startIso: root.customFrom ? root.isoDate(root.customFrom) : ""
    readonly property string endIso: root.customTo ? root.isoDate(root.customTo) : ""
    // Both ends picked (a complete range), and any end picked.
    readonly property bool valid: root.customFrom !== null && root.customTo !== null
    readonly property bool hasSelection: root.customFrom !== null || root.customTo !== null

    // Clear the picked range so a new one can be chosen.
    function reset() {
        root.customFrom = null
        root.customTo = null
        root.pickingEnd = false
        root.hoverDate = null
    }

    // Refresh "today" (call when the calendar is shown) so a page left up past
    // midnight does not keep marking yesterday as today, and drop the focus ring.
    function refreshToday() {
        root.todayDate = new Date()
        root.calendarFocusDate = null
    }

    // Seed the calendar with an existing range (or nulls to start clean) and
    // show the month of its start.
    function seed(from, to) {
        root.customFrom = from
        root.customTo = to
        root.pickingEnd = false
        root.hoverDate = null
        root.showCalendarMonth(from)
    }

    // Internal state --------------------------------------------------------

    // Dates are shown in the same long shape as the rest of the app (see
    // Transaction::dateTimeString), but rendered with toLocaleDateString so
    // month names localize (a Qt.formatDate format string renders C-locale
    // English in Qt 6), matching the localized month header and weekday row.
    readonly property string activityDateFormat: "MMMM d, yyyy"

    // customFrom and customTo are local Date values (or null). While pickingEnd
    // is true the start is chosen and the next click sets the end; hoverDate
    // previews the range on desktop. The calendar shows calMonth/calYear.
    property var customFrom: null
    property var customTo: null
    property bool pickingEnd: false
    property var hoverDate: null
    property int calMonth: (new Date()).getMonth()
    property int calYear: (new Date()).getFullYear()
    // First day of the week for the locale, as Qt::DayOfWeek
    // (1 = Monday .. 7 = Sunday). The mod-7 arithmetic in calendarCells() and
    // the weekday header maps it onto JS getDay()'s 0 = Sunday .. 6 = Saturday
    // numbering (7 % 7 == 0).
    readonly property int weekStart: Qt.locale().firstDayOfWeek
    property var todayDate: new Date()
    // Day the keyboard focus ring is on while the day grid has active focus;
    // null until the grid is first focused after opening.
    property var calendarFocusDate: null

    // The chips are too narrow for the long Activity date format (a month
    // like September wraps), so they use the locale's short date form.
    function formatRangeDate(date) {
        if (!date || isNaN(date.getTime())) {
            return ""
        }
        return date.toLocaleDateString(Qt.locale(), Locale.ShortFormat)
    }

    function isoDate(date) {
        return Qt.formatDate(date, "yyyy-MM-dd")
    }

    function sameDay(a, b) {
        return a && b && root.isoDate(a) === root.isoDate(b)
    }

    // The two ends of the range currently in play: the committed range, or the
    // tentative one while hovering after the first click.
    function rangeBounds() {
        if (root.customFrom && root.customTo) {
            return [root.customFrom, root.customTo]
        }
        if (root.pickingEnd && root.customFrom && root.hoverDate) {
            return root.hoverDate < root.customFrom
                ? [root.hoverDate, root.customFrom]
                : [root.customFrom, root.hoverDate]
        }
        return null
    }

    function isRangeEndpoint(date) {
        // Check the picked ends directly so the start lights up on the click,
        // not only once the mouse moves and a hover range exists.
        return root.sameDay(date, root.customFrom)
            || root.sameDay(date, root.customTo)
            || (root.pickingEnd && root.customFrom && root.hoverDate
                && root.sameDay(date, root.hoverDate))
    }

    function isInRange(date) {
        const b = root.rangeBounds()
        return b !== null && date >= b[0] && date <= b[1]
    }

    // Cells for the displayed month: leading blanks (null) so the first day
    // lands under its weekday for the locale, then one Date per day.
    function calendarCells() {
        var cells = []
        var firstDow = new Date(root.calYear, root.calMonth, 1).getDay()
        var lead = (firstDow - root.weekStart + 7) % 7
        for (var i = 0; i < lead; ++i) {
            cells.push(null)
        }
        var days = new Date(root.calYear, root.calMonth + 1, 0).getDate()
        for (var d = 1; d <= days; ++d) {
            cells.push(new Date(root.calYear, root.calMonth, d))
        }
        return cells
    }

    function showCalendarMonth(date) {
        var base = (date && !isNaN(date.getTime())) ? date : new Date()
        root.calMonth = base.getMonth()
        root.calYear = base.getFullYear()
    }

    function stepCalendarMonth(delta) {
        var m = root.calMonth + delta
        var y = root.calYear
        while (m < 0) { m += 12; y -= 1 }
        while (m > 11) { m -= 12; y += 1 }
        root.calMonth = m
        root.calYear = y
    }

    // Seed the keyboard focus ring when the day grid gains focus (or after the
    // displayed month changed under it): prefer the picked start, then today,
    // when they are in the displayed month.
    function ensureCalendarFocusDate() {
        var f = root.calendarFocusDate
        if (f && f.getMonth() === root.calMonth && f.getFullYear() === root.calYear) {
            return
        }
        var seed = root.customFrom || root.todayDate
        if (seed.getMonth() === root.calMonth && seed.getFullYear() === root.calYear) {
            root.calendarFocusDate = new Date(seed.getFullYear(), seed.getMonth(), seed.getDate())
        } else {
            root.calendarFocusDate = new Date(root.calYear, root.calMonth, 1)
        }
    }

    function moveCalendarFocus(deltaDays) {
        root.ensureCalendarFocusDate()
        var f = root.calendarFocusDate
        var d = new Date(f.getFullYear(), f.getMonth(), f.getDate() + deltaDays)
        root.calendarFocusDate = d
        root.showCalendarMonth(d)
    }

    // Month (or, with delta +-12, year) step that carries the focus ring along,
    // clamping the day to the target month's length.
    function stepCalendarFocusMonth(delta) {
        root.ensureCalendarFocusDate()
        var f = root.calendarFocusDate
        var m = f.getMonth() + delta
        var y = f.getFullYear()
        while (m < 0) { m += 12; y -= 1 }
        while (m > 11) { m -= 12; y += 1 }
        var days = new Date(y, m + 1, 0).getDate()
        root.calendarFocusDate = new Date(y, m, Math.min(f.getDate(), days))
        root.showCalendarMonth(root.calendarFocusDate)
    }

    // Range selection: the first click sets the start, the next sets the end
    // (reordered if earlier). Once both ends are set, the next click starts a
    // fresh range, so a reopened calendar seeded with the applied range stays
    // usable without hitting Reset first.
    function selectCalendarDate(date) {
        if (root.customFrom && root.customTo) {
            root.customFrom = null
            root.customTo = null
            root.pickingEnd = false
        }
        if (!root.pickingEnd) {
            root.customFrom = date
            root.customTo = null
            root.pickingEnd = true
        } else {
            if (date < root.customFrom) {
                root.customTo = root.customFrom
                root.customFrom = date
            } else {
                root.customTo = date
            }
            root.pickingEnd = false
        }
        root.hoverDate = null
    }

    // Accessible label for a calendar day: the full localized date plus its
    // selection state, so the state is announced to a screen reader and not
    // conveyed by color alone.
    function dayCellAccessibleName(cell) {
        var base = cell.modelData.toLocaleDateString(Qt.locale(), root.activityDateFormat)
        if (cell.endpoint) {
            //: Screen reader label for a chosen start or end day in the Activity custom date range calendar. %1 is the date.
            return qsTr("%1, selected range endpoint").arg(base)
        }
        if (cell.ranged) {
            //: Screen reader label for a day inside the chosen Activity date range. %1 is the date.
            return qsTr("%1, in selected range").arg(base)
        }
        if (cell.isToday) {
            //: Screen reader label for today in the Activity date range calendar. %1 is the date.
            return qsTr("%1, today").arg(base)
        }
        return base
    }

    // UI --------------------------------------------------------------------

    // From / To display. The highlighted chip is the endpoint the next calendar
    // click will set.
    RowLayout {
        Layout.fillWidth: true
        spacing: 8

        DateRangeChip {
            objectName: "activityDateFromChip"
            Layout.fillWidth: true
            //: Label for the start date of the Activity custom date range.
            label: qsTr("From")
            value: root.formatRangeDate(root.customFrom)
            // Lit whenever the next click sets the start: before a range is
            // picked, and again once both ends are set (the next click then
            // starts a fresh range).
            highlighted: !root.pickingEnd
        }

        DateRangeChip {
            objectName: "activityDateToChip"
            Layout.fillWidth: true
            //: Label for the end date of the Activity custom date range.
            label: qsTr("To")
            value: root.formatRangeDate(root.customTo)
            highlighted: root.pickingEnd
        }
    }

    // Month / year navigation: single chevrons step a month, double chevrons
    // jump a year (to reach distant dates fast).
    RowLayout {
        Layout.fillWidth: true
        spacing: 2

        CalNavButton {
            objectName: "calendarPrevYear"
            iconSource: "image://images/caret-left"
            doubled: true
            //: Accessibility label for the Activity calendar control that jumps back one year.
            accessibleName: qsTr("Previous year")
            onClicked: root.stepCalendarMonth(-12)
        }
        CalNavButton {
            objectName: "calendarPrev"
            iconSource: "image://images/caret-left"
            //: Accessibility label for the Activity calendar control that steps back one month.
            accessibleName: qsTr("Previous month")
            onClicked: root.stepCalendarMonth(-1)
        }

        CoreText {
            objectName: "calendarMonthLabel"
            Layout.fillWidth: true
            // toLocaleDateString localizes the month name (a Qt.formatDate
            // format string would render C-locale English), matching the
            // localized weekday header.
            text: (new Date(root.calYear, root.calMonth, 1)).toLocaleDateString(Qt.locale(), "MMMM yyyy")
            horizontalAlignment: Text.AlignHCenter
            color: Theme.color.neutral9
            font.pixelSize: 15
            bold: true
        }

        CalNavButton {
            objectName: "calendarNext"
            iconSource: "image://images/caret-right"
            //: Accessibility label for the Activity calendar control that steps forward one month.
            accessibleName: qsTr("Next month")
            onClicked: root.stepCalendarMonth(1)
        }
        CalNavButton {
            objectName: "calendarNextYear"
            iconSource: "image://images/caret-right"
            doubled: true
            //: Accessibility label for the Activity calendar control that jumps forward one year.
            accessibleName: qsTr("Next year")
            onClicked: root.stepCalendarMonth(12)
        }
    }

    // Weekday header, ordered by the locale's first day of week to match the day
    // grid. Use a Grid positioner, never a Quick Layout: a Repeater inside a
    // RowLayout/ColumnLayout/GridLayout hits a Qt 6.4 use-after-free that
    // SIGSEGVs only headed.
    Grid {
        Layout.alignment: Qt.AlignHCenter
        columns: 7
        spacing: 0

        Repeater {
            model: 7
            delegate: CoreText {
                required property int index
                readonly property int jsDay: (root.weekStart + index) % 7
                width: 36
                height: 22
                text: Qt.locale().dayName(jsDay === 0 ? 7 : jsDay, Locale.ShortFormat)
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                color: Theme.color.neutral6
                font.pixelSize: 12
            }
        }
    }

    // Day grid: one ItemDelegate per cell so each day is independently clickable
    // (and reachable by the test bridge).
    Grid {
        id: dayGrid
        objectName: "activityCalendarGrid"
        Layout.alignment: Qt.AlignHCenter
        columns: 7
        columnSpacing: 0
        rowSpacing: 2

        // The grid is a single tab stop; arrows move the focus ring between
        // days, PageUp/PageDown step a month (with Ctrl, a year), Enter or Space
        // picks the day.
        activeFocusOnTab: true
        onActiveFocusChanged: {
            if (activeFocus) {
                root.ensureCalendarFocusDate()
            }
        }
        Keys.onPressed: function(event) {
            switch (event.key) {
            case Qt.Key_Left: root.moveCalendarFocus(-1); break
            case Qt.Key_Right: root.moveCalendarFocus(1); break
            case Qt.Key_Up: root.moveCalendarFocus(-7); break
            case Qt.Key_Down: root.moveCalendarFocus(7); break
            case Qt.Key_PageUp:
                root.stepCalendarFocusMonth(event.modifiers & Qt.ControlModifier ? -12 : -1)
                break
            case Qt.Key_PageDown:
                root.stepCalendarFocusMonth(event.modifiers & Qt.ControlModifier ? 12 : 1)
                break
            case Qt.Key_Return:
            case Qt.Key_Enter:
            case Qt.Key_Space:
                root.ensureCalendarFocusDate()
                root.selectCalendarDate(root.calendarFocusDate)
                break
            default:
                return
            }
            event.accepted = true
        }

        // Clear the hover preview when the pointer leaves the grid.
        HoverHandler {
            enabled: AppMode.isDesktop
            onHoveredChanged: if (!hovered) root.hoverDate = null
        }

        Repeater {
            model: root.calendarCells()
            delegate: ItemDelegate {
                id: dayCell
                required property var modelData
                width: 36
                height: 32
                padding: 0
                enabled: modelData !== null
                objectName: modelData ? "calendarDay_" + root.isoDate(modelData) : ""
                Accessible.role: Accessible.Button
                Accessible.name: modelData ? root.dayCellAccessibleName(dayCell) : ""

                readonly property bool endpoint: modelData !== null && root.isRangeEndpoint(modelData)
                readonly property bool ranged: modelData !== null && root.isInRange(modelData)
                readonly property bool isToday: modelData !== null && root.sameDay(modelData, root.todayDate)
                readonly property bool keyFocused: modelData !== null && dayGrid.activeFocus
                    && root.calendarFocusDate !== null && root.sameDay(modelData, root.calendarFocusDate)

                onClicked: {
                    if (modelData) {
                        root.selectCalendarDate(modelData)
                    }
                }

                HoverHandler {
                    enabled: AppMode.isDesktop && dayCell.modelData !== null
                    cursorShape: Qt.PointingHandCursor
                    onHoveredChanged: if (hovered) root.hoverDate = dayCell.modelData
                }

                background: Rectangle {
                    visible: dayCell.modelData !== null
                    radius: 4
                    // Hover and press tints mark the day as clickable; the
                    // range tint wins so the preview stays readable.
                    color: dayCell.endpoint
                        ? Theme.color.orange
                        : dayCell.ranged
                            ? Theme.color.neutral3
                            : dayCell.down
                                ? Theme.color.neutral4
                                : dayCell.hovered
                                    ? Theme.color.neutral2
                                    : "transparent"
                    // The 2px neutral keyboard focus ring wins over the 1px
                    // orange today ring, so the focused day is always
                    // identifiable.
                    border.width: dayCell.keyFocused ? 2 : 1
                    border.color: dayCell.keyFocused
                        ? Theme.color.neutral9
                        : dayCell.isToday && !dayCell.endpoint
                            ? Theme.color.orange
                            : "transparent"
                }

                contentItem: CoreText {
                    text: dayCell.modelData ? dayCell.modelData.getDate() : ""
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    color: dayCell.endpoint ? Theme.color.white : Theme.color.neutral9
                    font.pixelSize: 13
                    // Mark today by weight too, so it does not rely on the ring color alone.
                    bold: dayCell.isToday
                }
            }
        }
    }

    // Private sub-components -------------------------------------------------

    component DateRangeChip: Rectangle {
        id: chip
        property string label: ""
        property string value: ""
        property bool highlighted: false
        //: Read for an Activity date range endpoint that has no date picked yet.
        readonly property string accessibleValue: chip.value !== "" ? chip.value : qsTr("no date selected")
        implicitHeight: 46
        radius: 5
        color: Theme.color.neutral2
        border.color: chip.highlighted ? Theme.color.orange : "transparent"
        border.width: 1
        Accessible.role: Accessible.StaticText
        Accessible.name: chip.highlighted
            //: Accessibility name of the active Activity date range endpoint. %1 is "From" or "To", %2 the picked date or "no date selected".
            ? qsTr("%1, %2, the next calendar selection sets this date").arg(chip.label).arg(chip.accessibleValue)
            //: Accessibility name of an inactive Activity date range endpoint. %1 is "From" or "To", %2 the picked date or "no date selected".
            : qsTr("%1, %2").arg(chip.label).arg(chip.accessibleValue)
        ColumnLayout {
            anchors.fill: parent
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            anchors.topMargin: 6
            anchors.bottomMargin: 6
            spacing: 1
            CoreText {
                Layout.fillWidth: true
                text: chip.label
                color: Theme.color.neutral7
                font.pixelSize: 11
                // Weight marks the active endpoint too, so the state does not
                // rely on the border color alone.
                bold: chip.highlighted
                horizontalAlignment: Text.AlignLeft
            }
            CoreText {
                Layout.fillWidth: true
                //: Placeholder in the Activity date range From/To field before a date is picked.
                text: chip.value !== "" ? chip.value : qsTr("Select date")
                color: chip.value !== "" ? Theme.color.neutral9 : Theme.color.neutral6
                font.pixelSize: 14
                horizontalAlignment: Text.AlignLeft
                // CoreText wraps by default, which would defeat the elide and
                // overflow the fixed-height chip on long dates.
                wrap: false
                elide: Text.ElideRight
            }
        }
    }

    // A calendar month/year step button. One caret steps a month; the year jump
    // shows two carets, composed from the caret already in the Bitcoin Icons set
    // (the set has no double-caret glyph).
    component CalNavButton: ItemDelegate {
        id: navButton
        property url iconSource
        property bool doubled: false
        property string accessibleName: ""
        implicitWidth: 28
        implicitHeight: 28
        padding: 0
        Accessible.role: Accessible.Button
        Accessible.name: navButton.accessibleName
        contentItem: Item {
            Row {
                anchors.centerIn: parent
                spacing: -5
                Repeater {
                    model: navButton.doubled ? 2 : 1
                    delegate: Icon {
                        source: navButton.iconSource
                        color: Theme.color.orange
                        size: 14
                    }
                }
            }
        }
    }
}
