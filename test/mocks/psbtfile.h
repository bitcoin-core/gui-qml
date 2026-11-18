// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_TEST_MOCKS_PSBTFILE_H
#define BITCOIN_QML_TEST_MOCKS_PSBTFILE_H

#include <psbt.h>
#include <qml/psbtserialization.h>

#include <QFile>
#include <QString>

//! Writes \p document to \p path as raw PSBT bytes, for a test to import.
//! Returns why it failed, or an empty string.
inline QString WritePsbtFile(const PartiallySignedTransaction& document, const QString& path)
{
    QFile file{path};
    if (!file.open(QIODevice::WriteOnly)) return file.errorString();
    const auto bytes = psbtwork::SerializeRaw(document);
    if (file.write(bytes) != bytes.size()) return file.errorString();
    return {};
}

#endif // BITCOIN_QML_TEST_MOCKS_PSBTFILE_H
