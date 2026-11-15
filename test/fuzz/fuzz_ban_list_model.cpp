// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/banlistmodel.h>

#include <test/mocks/mocknode.h>

#include <net_types.h>
#include <netaddress.h>
#include <netbase.h>

#include <QByteArray>
#include <QApplication>
#include <QString>

#include <cstddef>
#include <cstdint>

static int argc = 1;
static char arg0[] = "fuzz_ban_list_model";
static char* argv[] = {arg0, nullptr};
static QApplication app(argc, argv);

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    MockNode node;

    // Build a banmap from newline-separated subnet strings in the fuzz input.
    banmap_t banned;
    const QByteArray raw(reinterpret_cast<const char*>(data), static_cast<int>(size));
    const QString text = QString::fromUtf8(raw);
    for (const QString& line : text.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const CSubNet subnet = LookupSubNet(line.toStdString());
        if (subnet.IsValid()) {
            banned.emplace(subnet, CBanEntry{/*nCreateTimeIn=*/0});
        }
    }

    node.get_banned_fn = [&banned](banmap_t& banmap) {
        banmap = banned;
        return true;
    };
    node.unban_fn = [&banned](const CSubNet& subnet) {
        banned.erase(subnet);
        return true;
    };

    BanListModel model(node, /*parent=*/nullptr, /*backend_ready=*/true);
    model.refresh();
    (void)model.rowCount();
    (void)model.count();
    if (!banned.empty()) {
        model.unbanAt(0);
    }
    model.beginShutdown();
    return 0;
}
