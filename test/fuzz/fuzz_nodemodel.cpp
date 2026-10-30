// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/mocks/stubnode.h>

#include <qml/models/nodemodel.h>

#include <QByteArray>
#include <QApplication>
#include <QString>

#include <cstddef>
#include <cstdint>

static int argc = 1;
static char arg0[] = "fuzz_nodemodel";
static char* argv[] = {arg0, nullptr};
static QApplication app(argc, argv);

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    const QByteArray raw(reinterpret_cast<const char*>(data), static_cast<int>(size));
    const QString text = QString::fromUtf8(raw);

    StubNode node;
    NodeModel model(node, /*backend_ready=*/true);

    (void)model.validateProxyAddress(text);
    (void)model.defaultProxyAddress();
    (void)model.banPeer(text, size > 0 ? static_cast<int64_t>(data[0]) : 0);
    return 0;
}
