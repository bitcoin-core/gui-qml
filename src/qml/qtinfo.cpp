// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/qtinfo.h>
#include <logging.h>

#include <QGuiApplication>
#include <QJsonObject>
#include <QPluginLoader>
#include <QQuickStyle>
#include <QScreen>
#include <QSysInfo>

#include <string>

namespace QmlDiagnostics {
void LogQtInfo()
{
#ifdef QT_STATIC
    const std::string qt_link{"static"};
#else
    const std::string qt_link{"dynamic"};
#endif
    LogInfo("Qt %s (%s), plugin=%s\n", qVersion(), qt_link, QGuiApplication::platformName().toStdString());

    const auto static_plugins = QPluginLoader::staticPlugins();
    if (static_plugins.empty()) {
        LogInfo("No static plugins.\n");
    } else {
        LogInfo("Static plugins:\n");
        for (const QStaticPlugin& p : static_plugins) {
            const QJsonObject meta_data = p.metaData();
            const std::string plugin_class = meta_data.value(QStringLiteral("className")).toString().toStdString();
            const int plugin_version = meta_data.value(QStringLiteral("version")).toInt();
            LogInfo(" %s, version %d\n", plugin_class, plugin_version);
        }
    }

    LogInfo("Qt Quick Controls style: %s\n", QQuickStyle::name().toStdString());
    LogInfo("System: %s, %s\n", QSysInfo::prettyProductName().toStdString(), QSysInfo::buildAbi().toStdString());
    for (const QScreen* screen : QGuiApplication::screens()) {
        LogInfo("Screen: %s %dx%d, pixel ratio=%.1f\n", screen->name().toStdString(), screen->size().width(), screen->size().height(), screen->devicePixelRatio());
    }
}
} // namespace QmlDiagnostics
