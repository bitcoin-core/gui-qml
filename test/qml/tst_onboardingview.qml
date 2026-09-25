// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtTest 1.2
import "../../qml/controls"

TestCase {
    id: testCase
    name: "OnboardingView"
    when: windowShown
    visible: true
    width: 900
    height: 600

    Component {
        id: stackComponent
        StackView {
            width: 900
            height: 600
        }
    }

    Component {
        id: firstPage
        OnboardingView { title: "First" }
    }

    Component {
        id: secondPage
        OnboardingView { title: "Second" }
    }

    Component {
        id: formPage
        OnboardingView {
            width: 480
            height: 360
            title: "New wallet"
            heading: "Your keys. Your wallet."
            subheading: "Fully managed on this computer."
            showCloseButton: true
            imageView: Item {
                objectName: "customOnboardingImage"
                implicitWidth: 64
                implicitHeight: 64
            }
            primaryButtonText: "Create wallet"
            secondaryButtonText: "Cancel"
            property int primaryCount: 0
            property int secondaryCount: 0
            property int closeCount: 0
            onPrimaryClicked: primaryCount++
            onSecondaryClicked: secondaryCount++
            onCloseClicked: closeCount++
            childView: Column {
                Repeater {
                    model: 12
                    Rectangle { width: 400; height: 60 }
                }
            }
        }
    }

    function test_backOnlyAppearsAfterPushAndPopsStack() {
        const stack = createTemporaryObject(stackComponent, testCase)
        verify(stack !== null)
        stack.push(firstPage)
        compare(stack.currentItem.showBackButton, false)

        stack.push(secondPage)
        tryCompare(stack, "depth", 2)
        const second = stack.currentItem
        tryCompare(second, "showBackButton", true)
        tryVerify(function() { return second.StackView.status === StackView.Active })
        mouseClick(findChild(second, "onboardingBackButton"))
        tryCompare(stack, "depth", 1)
        compare(stack.currentItem.title, "First")
    }

    function test_scrollableChildAndFooterActions() {
        const page = createTemporaryObject(formPage, testCase)
        verify(page !== null)
        verify(findChild(page, "onboardingChildView") !== null)
        verify(findChild(page, "customOnboardingImage") !== null)
        verify(page.scrollView.contentHeight > page.scrollView.height)
        compare(page.primaryButton.text, "Create wallet")
        compare(page.secondaryButton.text, "Cancel")

        mouseClick(page.primaryButton)
        mouseClick(page.secondaryButton)
        mouseClick(findChild(page, "onboardingCloseButton"))
        compare(page.primaryCount, 1)
        compare(page.secondaryCount, 1)
        compare(page.closeCount, 1)
    }

    function test_narrowFooterStacksActions() {
        const page = createTemporaryObject(formPage, testCase)
        verify(page !== null)
        page.width = 360
        wait(0)

        compare(page.primaryButton.width, page.secondaryButton.width)
        verify(page.secondaryButton.y > page.primaryButton.y)
    }
}
