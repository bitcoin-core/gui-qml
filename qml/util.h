// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_UTIL_H
#define BITCOIN_QML_UTIL_H

#include <support/allocators/secure.h>

#include <QString>
#include <QList>
#include <QtGlobal>

QT_BEGIN_NAMESPACE
class QQuickWindow;
QT_END_NAMESPACE

/**
 * Utility functions used by the QML-based Bitcoin Core GUI.
 */
namespace QmlUtil {
/**
* Returns a human-readable description of the graphics API
* that is in use by the Qt Quick scene graph renderer.
*/
QString GraphicsApi(QQuickWindow* window);

/**
 * Writes to debug.log short info about the used Qt and the host system.
 */
struct QtScreenInfo { QString name; int width{0}, height{0}; qreal pixel_ratio{1}; };
struct QtInfo { QString platform, controls_style; QList<QtScreenInfo> screens; };
// Capture only the necessary GUI-affine platform objects, then log/read host
// facts on Native. The diagnostic writer never dereferences QScreen objects.
QtInfo CaptureQtInfo();
void LogQtInfo(const QtInfo& info);

void ClearSecureString(SecureString& value);

} // namespace QmlUtil

#endif // BITCOIN_QML_UTIL_H
