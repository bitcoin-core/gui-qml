// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/addresslistmodel.h>

#include <QByteArray>
#include <QApplication>
#include <QString>

#include <cstddef>
#include <cstdint>

static int argc = 1;
static char arg0[] = "fuzz_address_list_model";
static char* argv[] = {arg0, nullptr};
static QApplication app(argc, argv);

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    AddressListModel model(/*parent=*/nullptr);

    if (size == 0) return 0;

    // Toggle category and show-used flags from the first byte.
    model.setCategory((data[0] & 1) ? AddressListModel::Change : AddressListModel::SingleUse);
    model.setShowUsed(data[0] & 2);
    model.setDisplayUnit(static_cast<int>(data[0] % 4));

    const QByteArray raw(reinterpret_cast<const char*>(data + 1), static_cast<int>(size - 1));
    const QString text = QString::fromUtf8(raw);

    model.refresh();
    (void)model.addressAt(static_cast<int>(size));
    (void)model.rowCount();
    (void)model.count();
    (void)model.categoryOptions();
    model.setAddressLabel(text, text);

    return 0;
}
