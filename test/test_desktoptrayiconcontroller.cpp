// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/desktoptrayiconcontroller.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <QSignalSpy>

#ifndef BITCOINQML_NO_TEST_MAIN
int main(int argc, char* argv[])
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
#else
int RunDesktopTrayIconControllerTests(int argc, char* argv[])
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
#endif

TEST(DesktopTrayIconControllerTest, initiallyNotVisible)
{
    DesktopTrayIconController controller;
    EXPECT_FALSE(controller.visible());
}

TEST(DesktopTrayIconControllerTest, setVisibleFalseWhenAlreadyHidden_noSignal)
{
    DesktopTrayIconController controller;
    QSignalSpy spy(&controller, &DesktopTrayIconController::visibleChanged);
    controller.setVisible(false);
    EXPECT_EQ(spy.count(), 0);
}

TEST(DesktopTrayIconControllerTest, isDarkDefaultTrue)
{
    DesktopTrayIconController controller;
    EXPECT_TRUE(controller.isDark());
}

TEST(DesktopTrayIconControllerTest, setIsDarkSameValue_noSignal)
{
    DesktopTrayIconController controller;
    QSignalSpy spy(&controller, &DesktopTrayIconController::isDarkChanged);
    controller.setIsDark(true); // same as default
    EXPECT_EQ(spy.count(), 0);
}

TEST(DesktopTrayIconControllerTest, setIsDarkChanged_emitsSignal)
{
    DesktopTrayIconController controller;
    QSignalSpy spy(&controller, &DesktopTrayIconController::isDarkChanged);
    controller.setIsDark(false);
    ASSERT_EQ(spy.count(), 1);
    EXPECT_EQ(spy.at(0).at(0).toBool(), false);
}
