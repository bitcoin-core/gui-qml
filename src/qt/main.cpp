// Copyright (c) 2018-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bitcoin-build-config.h> // IWYU pragma: keep

#if USE_QML
#include <qml/bitcoin.h>
#else
#include <qt/bitcoin.h>
#endif // USE_QML

#include <compat/compat.h>
#include <interfaces/node.h>
#include <logging.h>
#include <noui.h>
#include <util/translation.h>

#include <QCoreApplication>

#include <functional>
#include <string>

#if defined(QT_STATICPLUGIN)
#include <QtPlugin>
#if defined(QT_QPA_PLATFORM_XCB)
Q_IMPORT_PLUGIN(QXcbIntegrationPlugin);
#elif defined(QT_QPA_PLATFORM_WINDOWS)
Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin);
#elif defined(QT_QPA_PLATFORM_COCOA)
Q_IMPORT_PLUGIN(QCocoaIntegrationPlugin);
#elif defined(QT_QPA_PLATFORM_ANDROID)
Q_IMPORT_PLUGIN(QAndroidPlatformIntegrationPlugin)
#endif
#endif

/** Translate string to current locale using Qt. */
extern const TranslateFn G_TRANSLATION_FUN = [](const char* psz) {
    return QCoreApplication::translate("bitcoin-core", psz).toStdString();
};

const std::function<std::string()> G_TEST_GET_FULL_NAME{};

MAIN_FUNCTION
{
    qRegisterMetaType<interfaces::BlockAndHeaderTipInfo>("interfaces::BlockAndHeaderTipInfo");

    // Subscribe to global signals from core.
    noui_connect();

#if USE_QML
    return QmlGuiMain(argc, argv);
#else
    return GuiMain(argc, argv);
#endif // USE_QML
}
