// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/rpcconsolemodel.h>

#include <test/mocks/mocknode.h>

#include <QByteArray>
#include <QApplication>
#include <QEventLoop>
#include <QString>
#include <QTimer>

#include <cstddef>
#include <cstdint>

static int argc = 1;
static char arg0[] = "fuzz_rpc_console_model";
static char* argv[] = {arg0, nullptr};
static QApplication app(argc, argv);

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    static MockNode node;
    static RpcConsoleModel model(node);

    model.clear();
    model.resetHistoryNavigation();

    const QByteArray raw(reinterpret_cast<const char*>(data), static_cast<int>(size));
    const QString text = QString::fromUtf8(raw);

    // Use the first byte to choose between sync and async operations.
    if (size == 0) return 0;
    const uint8_t op = data[0];

    switch (op % 4) {
    case 0:
        model.browseHistory(-1, text);
        break;
    case 1:
        model.browseHistory(1, text);
        break;
    case 2:
        model.resetHistoryNavigation();
        break;
    case 3: {
        // Submit a command and briefly pump events so the worker thread reply
        // can be delivered. Use a short timeout to keep fuzz iterations fast.
        model.submitCommand(text);
        QEventLoop loop;
        QTimer::singleShot(20, &loop, &QEventLoop::quit);
        loop.exec();
        break;
    }
    }

    return 0;
}
