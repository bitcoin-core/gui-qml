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
        id: navigationBarComponent
        OnboardingNavigationBar {
            width: 720
            title: "Add a wallet"
            showCloseButton: true
        }
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

    function test_navigationBarCrossFadesTitleAndButtons() {
        const bar = createTemporaryObject(navigationBarComponent, testCase)
        verify(bar !== null)
        const back = findChild(bar, "onboardingBackButton")
        const outgoing = findChild(bar, "onboardingOutgoingTitle")
        const current = findChild(bar, "onboardingCurrentTitle")
        const close = findChild(bar, "onboardingCloseButton")
        compare(current.text, "Add a wallet")
        compare(current.opacity, 1)

        bar.title = ""
        bar.showBackButton = true
        bar.showCloseButton = false
        compare(outgoing.text, "Add a wallet")
        verify(outgoing.opacity > 0)
        compare(current.text, "")
        tryCompare(back, "opacity", 1)
        tryCompare(outgoing, "opacity", 0)
        tryCompare(close, "visible", false)

        bar.title = "View-only wallet"
        tryCompare(current, "opacity", 1)
        compare(current.text, "View-only wallet")
        bar.showBackButton = false
        tryCompare(back, "visible", false)
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
        tryVerify(function() {
            return page.primaryButton.width === page.secondaryButton.width
                && page.secondaryButton.y > page.primaryButton.y
        })
    }
}
