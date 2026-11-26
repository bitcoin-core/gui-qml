// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/qmlcachepolicy.h>

#include <QByteArray>
#include <QtGlobal>

void ConfigureQmlCachePolicy()
{
    // QML_FORCE_DISK_CACHE overrides the safe settings below, so do not allow
    // it to re-enable host-generated cache reads or writes.
    qunsetenv("QML_FORCE_DISK_CACHE");

#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    qunsetenv("QML_DISABLE_DISK_CACHE");
    qputenv("QML_DISK_CACHE", "aot");
#else
    // Older Qt versions cannot control embedded and host cache units separately.
    // Prefer the embedded source rather than allowing stale host cache files.
    qunsetenv("QML_DISK_CACHE");
    qputenv("QML_DISABLE_DISK_CACHE", "1");
#endif
}
