// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/BitcoinApp/util.h>

#include <cassert>
#include <string>

#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QString>

namespace QmlUtil {

QString GraphicsApi(QQuickWindow* window)
{
    switch (window->rendererInterface()->graphicsApi()) {
    case QSGRendererInterface::Unknown: return "Unknown";
    case QSGRendererInterface::Software: return "The Qt Quick 2D Renderer";
    case QSGRendererInterface::OpenVG: return "OpenVG via EGL";
    case QSGRendererInterface::OpenGL: return "OpenGL ES 2.0 or higher via a graphics abstraction layer";
    case QSGRendererInterface::Direct3D11: return "Direct3D 11 via a graphics abstraction layer";
#if (QT_VERSION >= QT_VERSION_CHECK(6, 6, 0))
    case QSGRendererInterface::Direct3D12: return "Direct3D 12 via a graphics abstraction layer";
#endif
    case QSGRendererInterface::Vulkan: return "Vulkan 1.0 via a graphics abstraction layer";
    case QSGRendererInterface::Metal: return "Metal via a graphics abstraction layer";
    case QSGRendererInterface::Null: return "Null (no output) via a graphics abstraction layer";
    } // no default case, so the compiler can warn about missing cases
    assert(false);
}

} // namespace QmlUtil
