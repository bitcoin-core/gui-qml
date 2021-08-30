// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_UTIL_H
#define BITCOIN_QML_UTIL_H

#include <QString>
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

} // namespace QmlUtil

#endif // BITCOIN_QML_UTIL_H
