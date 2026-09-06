// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/util.h>

#include <logging.h>
#include <support/cleanse.h>

#include <cassert>
#include <cstddef>
#include <string>

#include <QByteArray>
#include <QGuiApplication>
#include <QJsonObject>
#include <QPluginLoader>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QScreen>
#include <QSGRendererInterface>
#include <QString>
#include <QSysInfo>

namespace QmlUtil {

QString GraphicsApi(QQuickWindow* window)
{
    switch (window->rendererInterface()->graphicsApi()) {
    case QSGRendererInterface::Unknown: return "Unknown";
    case QSGRendererInterface::Software: return "The Qt Quick 2D Renderer";
    case QSGRendererInterface::OpenVG: return "OpenVG via EGL";
#if (QT_VERSION >= QT_VERSION_CHECK(5, 14, 0))
    case QSGRendererInterface::OpenGL: return "OpenGL ES 2.0 or higher via a graphics abstraction layer";
    case QSGRendererInterface::Direct3D11: return "Direct3D 11 via a graphics abstraction layer";
    case QSGRendererInterface::Vulkan: return "Vulkan 1.0 via a graphics abstraction layer";
    case QSGRendererInterface::Metal: return "Metal via a graphics abstraction layer";
    case QSGRendererInterface::Null: return "Null (no output) via a graphics abstraction layer";
#endif
#if (QT_VERSION >= QT_VERSION_CHECK(6, 6, 0))
    case QSGRendererInterface::Direct3D12: return "Direct3D 12 via a graphics abstraction layer";
#endif
    } // no default case, so the compiler can warn about missing cases
    assert(false);
}

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
            QJsonObject meta_data = p.metaData();
            const std::string plugin_class = meta_data.take(QString("className")).toString().toStdString();
            const int plugin_version = meta_data.take(QString("version")).toInt();
            LogInfo(" %s, version %d\n", plugin_class, plugin_version);
        }
    }

    LogInfo("Qt Quick Controls style: %s\n", QQuickStyle::name().toStdString());
    LogInfo("System: %s, %s\n", QSysInfo::prettyProductName().toStdString(), QSysInfo::buildAbi().toStdString());
    for (const QScreen* s : QGuiApplication::screens()) {
        LogInfo("Screen: %s %dx%d, pixel ratio=%.1f\n", s->name().toStdString(), s->size().width(), s->size().height(), s->devicePixelRatio());
    }
}

SecureString SecureStringFromQString(const QString& value)
{
    QByteArray bytes{value.toUtf8()};
    SecureString secure;
    secure.assign(bytes.constData(), bytes.constData() + bytes.size());
    if (!bytes.isEmpty()) {
        memory_cleanse(bytes.data(), static_cast<std::size_t>(bytes.size()));
    }
    return secure;
}

void ClearSecureString(SecureString& value)
{
    if (value.empty()) {
        return;
    }
    memory_cleanse(value.data(), value.size());
    value.clear();
}

} // namespace QmlUtil
