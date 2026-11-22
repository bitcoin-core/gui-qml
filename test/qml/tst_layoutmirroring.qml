// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15 as Controls
import QtTest 1.2
import org.bitcoincore.qt 1.0
import "../../qml/controls"
import "../../qml/components"
import "../../qml/pages/wallet"

TestCase {
    name: "LayoutMirroring"
    when: windowShown
    width: 960
    height: 720

    Item {
        id: rtlHost
        anchors.fill: parent
        LayoutMirroring.enabled: true
        LayoutMirroring.childrenInherit: true
    }

    Item {
        id: ltrHost
        anchors.fill: parent
    }

    Component {
        id: iconComponent
        Icon { color: "white" }
    }

    Component {
        id: switchComponent
        OptionSwitch {}
    }

    Component {
        id: sliderComponent
        Slider { width: 200; from: 0; to: 10; value: 3 }
    }

    Component {
        id: checkBoxComponent
        CheckBox { width: 300; text: "Remember" }
    }

    Component {
        id: textFieldComponent
        PasswordTextField { width: 300 }
    }

    Component {
        id: navButtonComponent
        NavButton { text: "Back"; iconSource: "image://images/caret-left" }
    }

    Component {
        id: backButtonComponent
        OnboardingBackButton {}
    }

    Component {
        id: searchBarComponent
        SearchBar { width: 300; fieldObjectName: "mirroredSearchField" }
    }

    Component {
        id: popupPickerComponent
        PopupPicker {
            objectName: "mirroredPicker"
            x: 500
            width: 200
            minimumMenuWidth: 320
            model: [{ text: "One", value: 1 }]
            currentValue: 1
        }
    }

    Component {
        id: pageStackComponent
        PageStack { width: 400; height: 300 }
    }

    Component {
        id: amountComponent
        BitcoinAmountDisplayLabel { width: 300; text: "0.001 BTC"; horizontalAlignment: Text.AlignLeft }
    }

    Component {
        id: calendarComponent
        ActivityCalendar { calMonth: 5; calYear: 2026 }
    }

    Component {
        id: splitComponent
        NavigationSplitView {
            width: 900
            height: 600
            transitionDuration: 0
            primaryComponent: Rectangle {}
            detailComponent: Rectangle {}
        }
    }

    Component {
        id: amountInputComponent
        BitcoinAmountInputField { width: 400; inputObjectName: "mirroredAmountInput" }
    }

    Component {
        id: feeSelectionComponent
        SendFeeSelection { walletModel: testWalletModel; width: 600 }
    }

    Component {
        id: addressRowComponent
        AddressRow {
            index: 0
            width: 520
            address: "bcrt1qexampleaddress"
            label: ""
            category: "single-use"
            displayAmount: "0 BTC"
            hasAmount: false
            scriptType: "P2WPKH"
            isUsed: false
            canEditLabel: true
        }
    }

    Component {
        id: activityRowComponent
        ActivityRow {
            width: 700
            activityId: "tx"
            txid: "tx"
            requestId: ""
            label: "Savings"
            address: "bcrt1qexampleaddress012345678901234567890"
            amount: "0.5 BTC"
            netAmountSat: -50000000
            dateTimeLabel: "Nov 22"
            activityType: 0
            status: 0
            depth: 1
            blocksToMaturity: 0
            statusKnown: true
            isInactive: false
            isPendingRequest: false
            replacedByTxid: ""
            hasPaymentRequest: false
            actions: []
        }
    }

    Component {
        id: flowComponent
        TransactionFlow {
            width: 900
            flow: ({
                inputs: [{ id: "input:0", kind: "output", amountSat: 100000, amountKnown: true, ownership: "wallet",
                    address: "bcrt1qexampleaddress012345678901234567890", label: "", paymentRequests: [] }],
                outputs: [{ id: "output:0", kind: "output", amountSat: 99000, amountKnown: true, ownership: "external",
                    address: "bcrt1qexampleaddress012345678901234567890", label: "", paymentRequests: [] }],
                complete: true, feeKnown: true, feeSat: 1000, inputCount: 1, outputCount: 1
            })
        }
    }

    function create(component, host, properties) {
        const item = createTemporaryObject(component, host, properties || {})
        verify(item !== null)
        wait(0)
        return item
    }

    function test_directionalIconsFlipOnlyWhenMirrored() {
        const back = create(iconComponent, rtlHost, { source: "image://images/caret-left" })
        compare(back.transform[0].xScale, -1)

        const check = create(iconComponent, rtlHost, { source: "image://images/check" })
        compare(check.transform[0].xScale, 1)

        const ltrBack = create(iconComponent, ltrHost, { source: "image://images/caret-left" })
        compare(ltrBack.transform[0].xScale, 1)
    }

    function test_switchKnobStartsAtTheTrailingEdge() {
        const toggle = create(switchComponent, rtlHost)
        const knobCenter = function() { return toggle.indicator.x + toggle.indicator.width / 2 }
        tryVerify(function() { return knobCenter() > toggle.width / 2 })
        toggle.checked = true
        tryVerify(function() { return knobCenter() < toggle.width / 2 })
    }

    function test_sliderFillGrowsFromTheRight() {
        const slider = create(sliderComponent, rtlHost)
        const track = slider.background
        const fill = track.children[0]
        fuzzyCompare(fill.width, track.width * 0.3, 0.5)
        fuzzyCompare(fill.x + fill.width, track.width, 0.5)
    }

    function test_checkBoxIndicatorSitsOnTheRight() {
        const box = create(checkBoxComponent, rtlHost)
        verify(box.indicator.x > box.width / 2)
        const label = box.contentItem.children[0]
        compare(label.x, 0)
        verify(label.mapToItem(box, label.width, 0).x <= box.indicator.x)
    }

    function test_textFieldReservesRoomForTheVisibilityIconOnTheLeft() {
        const field = create(textFieldComponent, rtlHost)
        const ltrField = create(textFieldComponent, ltrHost)
        compare(field.field.leftPadding, ltrField.field.rightPadding)
        compare(field.field.rightPadding, ltrField.field.leftPadding)
        verify(field.field.leftPadding > field.field.rightPadding)
        verify(field.loadedTrailingItem.mapToItem(field, 0, 0).x < field.width / 2)
    }

    function test_navButtonPaddingFollowsTheIcon() {
        const button = create(navButtonComponent, rtlHost)
        compare(button.leftPadding, 22)
        compare(button.rightPadding, Theme.space.xxs)
    }

    function test_onboardingBackButtonPutsTheChevronOnTheRight() {
        const button = create(backButtonComponent, rtlHost)
        const row = button.contentItem
        const chevron = row.children[0]
        const label = row.children[1]
        verify(chevron.x > label.x)
        compare(chevron.transform[0].xScale, -1)
        compare(button.rightPadding, Theme.space.xxs)
    }

    function test_searchBarSwapsIconAndClearPadding() {
        const bar = create(searchBarComponent, rtlHost)
        const field = findChild(bar, "mirroredSearchField")
        verify(field !== null)
        compare(field.rightPadding, 32)
        compare(field.leftPadding, 10)
    }

    function test_popupPickerMenuOpensFromTheLeftEdge() {
        const picker = create(popupPickerComponent, rtlHost)
        const menu = findChild(picker, "mirroredPickerMenu")
        verify(menu !== null)
        picker.open()
        tryVerify(function() { return menu.opened })
        compare(menu.background.mapToItem(rtlHost, 0, 0).x, picker.x)
        menu.close()
    }

    function test_pageStackPushesFromTheLeft() {
        const motionMode = Theme.motion.mode
        Theme.motion.mode = "full"
        const stack = create(pageStackComponent, rtlHost)
        const ltrStack = create(pageStackComponent, ltrHost)
        const travel = stack.slideDistance
        const ltrTravel = ltrStack.slideDistance
        Theme.motion.mode = motionMode
        compare(travel, -rtlHost.width)
        compare(ltrTravel, ltrHost.width)
    }

    function test_amountAnimationUsesEffectiveAlignment() {
        const amount = create(amountComponent, rtlHost)
        compare(amount.alignmentFactor, 1)
        const ltrAmount = create(amountComponent, ltrHost)
        compare(ltrAmount.alignmentFactor, 0)
    }

    function test_calendarArrowKeysFollowTheGrid() {
        const calendar = create(calendarComponent, rtlHost)
        const grid = findChild(calendar, "activityCalendarGrid")
        verify(grid !== null)
        compare(grid.effectiveLayoutDirection, Qt.RightToLeft)
        calendar.calendarFocusDate = new Date(2026, 5, 10)
        grid.forceActiveFocus()
        keyClick(Qt.Key_Left)
        compare(calendar.calendarFocusDate.getDate(), 11)
        keyClick(Qt.Key_Right)
        keyClick(Qt.Key_Right)
        compare(calendar.calendarFocusDate.getDate(), 9)
    }

    function test_splitViewPutsThePrimaryColumnOnTheRight() {
        const split = create(splitComponent, rtlHost)
        const primary = findChild(split, "navigationSplitPrimary")
        const separator = findChild(split, "navigationSplitSeparator")
        const detail = findChild(split, "navigationSplitDetail")
        compare(primary.width, 360)
        compare(primary.x, 540)
        compare(separator.x, 539)
        compare(detail.x, 0)
        compare(detail.width, 539)

        split.width = 390
        wait(0)
        compare(split.isCompact, true)
        compare(primary.x, 0)
        compare(detail.x, -390)
        split.showDetail()
        wait(0)
        compare(primary.x, 390)
        compare(detail.x, 0)
    }

    function test_textFieldsAlignToTheLeadingEdge() {
        const field = create(textFieldComponent, rtlHost)
        compare(field.field.effectiveHorizontalAlignment, TextInput.AlignRight)
        const ltrField = create(textFieldComponent, ltrHost)
        compare(ltrField.field.effectiveHorizontalAlignment, TextInput.AlignLeft)

        const bar = create(searchBarComponent, rtlHost)
        compare(findChild(bar, "mirroredSearchField").effectiveHorizontalAlignment, TextInput.AlignRight)
    }

    function test_amountFieldKeepsItsGapFromTheUnitButton() {
        const field = create(amountInputComponent, rtlHost)
        const input = findChild(field, "mirroredAmountInput")
        compare(input.effectiveHorizontalAlignment, TextInput.AlignRight)
        compare(input.rightPadding, 0)
        verify(input.leftPadding > 0)
    }

    function test_feeTicksFollowTheMirroredSlider() {
        const control = create(feeSelectionComponent, rtlHost)
        const first = findChild(control, "feeTargetBlocksTick0")
        const last = findChild(control, "feeTargetBlocksTick6")
        verify(first.x > last.x)
        compare(last.x, 0)
    }

    function test_addressRowHugsTheLeadingEdge() {
        const row = create(addressRowComponent, rtlHost)
        const address = findChild(row, "addressRowAddressText")
        const note = findChild(row, "addressRowNoteField")
        const right = row.mapFromItem(address, address.width, 0).x
        fuzzyCompare(right, row.mapFromItem(note, note.width, 0).x, 0.5)
        verify(right > row.width / 2)
    }

    function test_activityRowLabelHugsTheIcon() {
        const row = create(activityRowComponent, rtlHost)
        const label = findChild(row, "activityRowLabel")
        const icon = findChild(row, "activityRowIcon")
        const labelRight = row.mapFromItem(label, label.width, 0).x
        const iconLeft = row.mapFromItem(icon, 0, 0).x
        verify(labelRight <= iconLeft)
        verify(iconLeft - labelRight < 40)
    }

    function test_transactionFlowStaysLeftToRight() {
        const flow = create(flowComponent, rtlHost)
        const input = findChild(flow, "transactionFlowInput_0")
        const output = findChild(flow, "transactionFlowOutput_0")
        verify(input !== null && output !== null)
        compare(input.LayoutMirroring.enabled, false)
        verify(flow.mapFromItem(input, 0, 0).x < flow.mapFromItem(output, 0, 0).x)
    }
}
