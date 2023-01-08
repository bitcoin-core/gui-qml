import "../controls"
import "../controls"
import "../pages/onboarding"
import QtQuick 2.15
import QtTest 1.15

Rectangle {
    id: root
    width: 640
    height: 665
    color: Theme.color.background

    OnboardingCover {}

    TestCase {
        id: onboardingFlow
        when: windowShown
        
        function test_maths() {
            var image = grabImage(root)
            image.save("./output.png")
        }
    }
}
