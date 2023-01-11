import QtQuick 2.15
import QtTest 1.15
import "../controls"
import "../pages/onboarding"

Rectangle {
    id: root
    width: 640
    height: 665
    color: Theme.color.background

    OnboardingWizard {
        id: onboardingWizard
        anchors.fill: parent
    }

    TestCase {
        id: onboardingFlow
        when: windowShown

        function test_all_continue_buttons() {
            verify(onboardingWizard.currentIndex == 0)

            var cover = findChild(onboardingWizard, "onboardingCover")
            verify(cover, onboardingWizard.currentItem)
            mouseClick(findChild(cover, "continueButton"))
            verify(onboardingWizard.currentIndex == 1)
            wait(200)

            var strengthen = findChild(onboardingWizard, "onboardingStrengthen")
            verify(strengthen, onboardingWizard.currentItem)
            mouseClick(findChild(strengthen, "continueButton"))
            verify(onboardingWizard.currentIndex == 2)
            wait(200)

            var blockClock = findChild(onboardingWizard, "onboardingBlockClock")
            verify(blockClock, onboardingWizard.currentItem)
            mouseClick(findChild(blockClock, "continueButton"))
            verify(onboardingWizard.currentIndex == 3)
            wait(200)

            var storageLocation = findChild(onboardingWizard, "onboardingStorageLocation")
            verify(storageLocation, onboardingWizard.currentItem)
            mouseClick(findChild(storageLocation, "continueButton"))
            verify(onboardingWizard.currentIndex == 4)
            wait(200)

            var storageAmount = findChild(onboardingWizard, "onboardingStorageAmount")
            verify(storageAmount, onboardingWizard.currentItem)
            mouseClick(findChild(storageAmount, "continueButton"))
            verify(onboardingWizard.currentIndex == 5)
            wait(200)

            var connection = findChild(onboardingWizard, "onboardingConnection")
            verify(connection, onboardingWizard.currentItem)
            mouseClick(findChild(connection, "continueButton"))
            verify(onboardingWizard.finished)
        }
    }
}
