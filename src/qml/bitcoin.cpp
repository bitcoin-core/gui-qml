// Copyright (c) 2021-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/bitcoin.h>

#include <clientversion.h>
#include <common/args.h>
#include <common/init.h>
#include <common/license_info.h>
#include <common/system.h>
#include <init.h>
#include <interfaces/init.h>
#include <node/interface_ui.h>
#include <noui.h>
#include <qml/bitcoinqmlapplication.h>
#include <util/strencodings.h>
#include <util/threadnames.h>
#include <util/translation.h>

#include <QQuickStyle>
#include <QString>
#include <QStringLiteral>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

int QmlGuiMain(int argc, char* argv[])
{
    Q_INIT_RESOURCE(bitcoin_qml);

    QQuickStyle::setStyle(QStringLiteral("Basic"));

    BitcoinQmlApplication app{argc, argv};

    SetupEnvironment();
    util::ThreadSetInternalName("main");
    noui_connect();

    std::unique_ptr<interfaces::Init> init{interfaces::MakeGuiInit(argc, argv)};

    SetupServerArgs(gArgs, init->canListenIpc());
    gArgs.AddArg("-test-automation=<path>",
        "Enable the GUI test automation bridge at the given local socket path",
        ArgsManager::ALLOW_ANY | ArgsManager::DISALLOW_NEGATION |
            ArgsManager::DISALLOW_ELISION | ArgsManager::DEBUG_ONLY,
        OptionsCategory::GUI);
    std::string error;
    if (!gArgs.ParseParameters(argc, argv, error)) {
        InitError(Untranslated(strprintf("Error parsing command line arguments: %s", error)));
        return EXIT_FAILURE;
    }

    // Capture before InitConfig so configuration and settings files cannot enable the bridge.
    const auto test_automation_socket{gArgs.GetArg("-test-automation")};
    if (test_automation_socket && test_automation_socket->empty()) {
        InitError(Untranslated("The -test-automation option requires a socket path."));
        return EXIT_FAILURE;
    }

    if (HelpRequested(gArgs) || gArgs.GetBoolArg("-version", false)) {
        std::cout << init->exeName() << " " << FormatFullVersion() << "\n";
        if (gArgs.GetBoolArg("-version", false)) {
            std::cout << FormatParagraph(LicenseInfo());
        } else {
            std::cout << "\n" << gArgs.GetHelpMessage();
        }
        return EXIT_SUCCESS;
    }

    for (int i = 1; i < argc; ++i) {
        if (!IsSwitchChar(argv[i][0])) {
            InitError(Untranslated(strprintf("Command line contains unexpected token '%s', see %s -h for a list of options.", argv[i], init->exeName())));
            return EXIT_FAILURE;
        }
    }

    if (auto config_error{common::InitConfig(gArgs)}) {
        InitError(config_error->message, config_error->details);
        return EXIT_FAILURE;
    }
    if (test_automation_socket && gArgs.GetChainTypeString() != "regtest") {
        InitError(Untranslated("The -test-automation option is only available on regtest."));
        return EXIT_FAILURE;
    }

    app.parameterSetup();
    app.createNode(*init);
    if (!app.baseInitialize()) {
        return EXIT_FAILURE;
    }

    if (!app.createWindow()) {
        return EXIT_FAILURE;
    }

    if (test_automation_socket && !app.createTestBridge(QString::fromStdString(*test_automation_socket))) {
        return EXIT_FAILURE;
    }

    app.requestInitialize();
    return app.exec();
}
