// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/util.h>

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
    case QSGRendererInterface::OpenGL: return "OpenGL ES 2.0 or higher";
    case QSGRendererInterface::Direct3D12: return "Direct3D 12";
    case QSGRendererInterface::OpenVG: return "OpenVG via EGL";
#if (QT_VERSION >= QT_VERSION_CHECK(5, 14, 0))
    case QSGRendererInterface::OpenGLRhi: return "OpenGL ES 2.0 or higher via a graphics abstraction layer";
    case QSGRendererInterface::Direct3D11Rhi: return "Direct3D 11 via a graphics abstraction layer";
    case QSGRendererInterface::VulkanRhi: return "Vulkan 1.0 via a graphics abstraction layer";
    case QSGRendererInterface::MetalRhi: return "Metal via a graphics abstraction layer";
    case QSGRendererInterface::NullRhi: return "Null (no output) via a graphics abstraction layer";
#endif
    } // no default case, so the compiler can warn about missing cases
    assert(false);
}

} // namespace QmlUtil
