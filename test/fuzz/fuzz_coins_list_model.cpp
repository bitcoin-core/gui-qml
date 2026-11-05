// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/coinslistmodel.h>

#include <QByteArray>
#include <QApplication>
#include <QString>
#include <QStringList>

#include <cstddef>
#include <cstdint>

static int argc = 1;
static char arg0[] = "fuzz_coins_list_model";
static char* argv[] = {arg0, nullptr};
static QApplication app(argc, argv);

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    CoinsListModel model(/*parent=*/nullptr);

    if (size == 0) return 0;

    // Split input into null-terminated strings.
    int field = 0;
    size_t start = 0;
    for (size_t i = 0; i <= size; ++i) {
        if (i == size || data[i] == '\0') {
            const QByteArray raw(reinterpret_cast<const char*>(data + start), static_cast<int>(i - start));
            const QString text = QString::fromUtf8(raw);
            switch (field % 8) {
            case 0: model.setSearchText(text); break;
            case 1: model.setSortBy(text); break;
            case 2: model.setFilter(text); break;
            case 3: model.setGroupBy(text); break;
            case 4: model.setSortDescending(text.startsWith(QLatin1Char('1'))); break;
            case 5: model.setMinAmount(text.toLongLong()); break;
            case 6: model.setMaxAmount(text.toLongLong()); break;
            case 7: model.toggleCoinSelectionById(text); break;
            }
            ++field;
            start = i + 1;
        }
    }

    model.update();
    model.refreshSelection();
    model.beginSelection();
    model.applySelection();
    model.cancelSelection();
    (void)model.rowCount();
    (void)model.coinCount();
    (void)model.totalSatoshi();
    (void)model.lockedSatoshi();
    (void)model.availableMinAmount();
    (void)model.availableMaxAmount();
    return 0;
}
