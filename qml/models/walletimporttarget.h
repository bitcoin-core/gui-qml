// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_WALLETIMPORTTARGET_H
#define BITCOIN_QML_MODELS_WALLETIMPORTTARGET_H

#include <qml/models/sendimportports.h>

#include <QMetaObject>
#include <QPointer>

class BitcoinUriModel;
class SendRecipient;
class WalletModel;

/** A wallet's send draft as an ImportTarget: imports go through BitcoinUri
 * into the wallet's current recipient. */
class WalletImportTarget : public ImportTarget
{
    Q_OBJECT
    Q_PROPERTY(WalletModel* wallet READ wallet WRITE setWallet NOTIFY walletChanged)
    Q_PROPERTY(BitcoinUriModel* uri READ uri WRITE setUri NOTIFY uriChanged)

public:
    explicit WalletImportTarget(QObject* parent = nullptr);

    WalletModel* wallet() const { return m_wallet; }
    void setWallet(WalletModel* wallet);
    BitcoinUriModel* uri() const { return m_uri; }
    void setUri(BitcoinUriModel* uri);

    bool available() const override;
    QString draftRevision() const override;
    bool importPending() const override;
    bool recipientAddressEmpty() const override;
    QString importedReviewToken() const override;
    ImportOperation* importText(const QString& text, QObject* owner) override;
    ImportOperation* importPaste(const QString& text, int field, QObject* recipient, QObject* owner) override;
    ImportOperation* importFile(const QString& path, QObject* owner) override;
    ImportOperation* importDrop(int url_count, const QUrl& first_url, const QString& text, QObject* owner) override;
    ImportOperation* captureFile(QObject* owner) override;
    bool adopt(ImportOperation* operation) override;

Q_SIGNALS:
    void walletChanged();
    void uriChanged();

private:
    SendRecipient* currentRecipient() const;
    void watchRecipient();

    QPointer<WalletModel> m_wallet;
    QPointer<BitcoinUriModel> m_uri;
    QPointer<SendRecipient> m_recipient;
    QList<QMetaObject::Connection> m_recipient_connections;
};

#endif // BITCOIN_QML_MODELS_WALLETIMPORTTARGET_H
