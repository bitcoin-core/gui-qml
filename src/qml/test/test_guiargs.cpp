// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <common/args.h>
#include <qml/guiargs.h>

#include <string>
#include <vector>

class GuiArgsTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void qmlGuiArgsParseGuiOnlyFlags();
};

void GuiArgsTests::qmlGuiArgsParseGuiOnlyFlags()
{
    ArgsManager args;
    SetupQmlGuiArgs(args);

    std::string parse_error;
    const std::vector<std::string> argv{
        std::string{"bitcoinqml"},
        std::string{"-choosedatadir"},
        std::string{"-resetguisettings"},
        std::string{"-lang=de"},
        std::string{"-min"},
#ifdef ENABLE_TEST_AUTOMATION
        std::string{"-test-automation=/tmp/test-bridge.sock"},
        std::string{"-test-settings-dir=/tmp/qml-settings"},
#endif
    };

    std::vector<const char*> raw_argv;
    raw_argv.reserve(argv.size());
    for (const std::string& arg : argv) raw_argv.push_back(arg.c_str());

    QVERIFY2(args.ParseParameters(static_cast<int>(raw_argv.size()), raw_argv.data(), parse_error), parse_error.c_str());
    QVERIFY(args.GetBoolArg("-choosedatadir", false));
    QVERIFY(args.GetBoolArg("-resetguisettings", false));
    QVERIFY(args.GetBoolArg("-min", false));
    QCOMPARE(args.GetArg("-lang", ""), std::string{"de"});
#ifdef ENABLE_TEST_AUTOMATION
    QCOMPARE(args.GetArg("-test-automation", ""), std::string{"/tmp/test-bridge.sock"});
    QCOMPARE(args.GetArg("-test-settings-dir", ""), std::string{"/tmp/qml-settings"});
#endif
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <qml/test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(GuiArgsTests)
#else
QTEST_MAIN(GuiArgsTests)
#endif
#include "test_guiargs.moc"
