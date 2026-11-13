// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/psbtqmlmodel.h>

#include <primitives/transaction.h>

#include <QByteArray>
#include <QApplication>
#include <QString>

#include <cstddef>
#include <cstdint>

static int argc = 1;
static char arg0[] = "fuzz_psbtqmlmodel";
static char* argv[] = {arg0, nullptr};
static QApplication app(argc, argv);

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    const QByteArray raw(reinterpret_cast<const char*>(data), static_cast<int>(size));
    const QString path = QString::fromUtf8(raw);

    (void)PsbtQmlModel::LocalFilePath(path);

    CMutableTransaction empty_tx;
    PartiallySignedTransaction psbt{empty_tx};
    (void)PsbtQmlModel::LoadPsbtFromFile(path, psbt);
    return 0;
}
