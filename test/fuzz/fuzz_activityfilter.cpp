// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/activityfilterproxymodel.h>

#include <QByteArray>
#include <QApplication>
#include <QString>

#include <cstddef>
#include <cstdint>

static int argc = 1;
static char arg0[] = "fuzz_activityfilter";
static char* argv[] = {arg0, nullptr};
static QApplication app(argc, argv);

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (size < 2) return 0;
    const size_t half = size / 2;
    const QByteArray raw1(reinterpret_cast<const char*>(data), static_cast<int>(half));
    const QByteArray raw2(reinterpret_cast<const char*>(data + half), static_cast<int>(size - half));
    const QString s1 = QString::fromUtf8(raw1);
    const QString s2 = QString::fromUtf8(raw2);

    ActivityFilterProxyModel model;
    (void)model.applyCustomRange(s1, s2);
    (void)model.setSearchText(s1);
    return 0;
}
