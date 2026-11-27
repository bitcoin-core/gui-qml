// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/walletmodel.h>
#include <test/qt_test_registry.h>

#include <QMetaMethod>
#include <QMetaProperty>
#include <QSet>
#include <QtTest/QtTest>

class WalletQmlModelSurfaceTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    //! WalletModel exposes data and wallet-wide state; wallet actions
    //! belong to a page controller's interface. This list only shrinks.
    void walletQmlModelSurfaceOnlyShrinks()
    {
        static const QSet<QByteArray> allowed{
            "availableReceiveAddressTypes", "changeWalletPassphrase",
            "clearSelectedCoins", "clearSettingsError", "defaultReceiveAddressType", "discardCurrentTransaction",
            "encryptWallet", "estimatedFeeForTarget", "feeTargetIndex",
            "receiveAddressTypeLabel", "reloadReceiveRequests", "requestClose", "requestImportPsbtFromFile",
            "requestPrepareTransaction", "requestPrepareTransactionWithPassphrase", "scheduleFeeEstimates",
            "setCustomFeeTarget", "setDefaultReceiveAddressType", "useMaximum", "useMaximumFor",
        };
        QSet<QByteArray> found;
        const auto* meta = &WalletModel::staticMetaObject;
        for (int i = meta->methodOffset(); i < meta->methodCount(); ++i) {
            const auto method = meta->method(i);
            if (method.methodType() == QMetaMethod::Method && method.access() == QMetaMethod::Public) found.insert(method.name());
        }
        for (int i = meta->propertyOffset(); i < meta->propertyCount(); ++i) {
            const QByteArray type = meta->property(i).typeName();
            if (type.contains("Operation") || type.contains("BumpTransactionModel")) found.insert(meta->property(i).name());
        }
        QStringList added, removed;
        for (const auto& name : found) if (!allowed.contains(name)) added << QString::fromLatin1(name);
        for (const auto& name : allowed) if (!found.contains(name)) removed << QString::fromLatin1(name);
        QVERIFY2(added.isEmpty(), qPrintable("New wallet action; add it to a page controller's interface instead: " + added.join(", ")));
        QVERIFY2(removed.isEmpty(), qPrintable("Gone from WalletModel; remove it from this list: " + removed.join(", ")));
    }
};

BITCOINQML_REGISTER_QT_TEST(WalletQmlModelSurfaceTests)
#include <test_walletqmlmodelsurface.moc>
