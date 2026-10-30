// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/walletqmlmodel.h>

#include <QByteArray>
#include <QApplication>
#include <QString>

#include <cstddef>
#include <cstdint>

static int argc = 1;
static char arg0[] = "fuzz_walletqmlmodel";
static char* argv[] = {arg0, nullptr};
static QApplication app(argc, argv);

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    const QByteArray raw(reinterpret_cast<const char*>(data), static_cast<int>(size));
    const QString text = QString::fromUtf8(raw);

    WalletQmlModel model; // node == nullptr, wallet == nullptr
    (void)model.receiveAddressTypeLabel(text);
    (void)model.importPsbtFromFile(text);
    (void)model.setCurrentPaymentRequestAddress(text);
    (void)model.loadPaymentRequest(text);
    (void)model.loadPaymentRequestDetail(text);
    (void)model.removeReceiveRequest(text);
    (void)model.usePaymentRequestAsTemplate(text);
    return 0;
}
