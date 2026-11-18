// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_SENDIMPORTPORTS_H
#define BITCOIN_QML_MODELS_SENDIMPORTPORTS_H

#include <QObject>
#include <QString>
#include <QUrl>

/** One payment request or PSBT import, as SendImportModel drives it.
 * PaymentImportOperation implements it in the app; QML tests use a fixture. */
class ImportOperation : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    virtual bool pending() const = 0;
    virtual bool applied() const = 0;
    virtual bool reviewOpened() const = 0;
    //! A payment request that can apply and differs from the draft.
    virtual bool offersPayment() const = 0;
    //! Nonzero while the import waits for consent to replace draft fields.
    virtual quint64 promptToken() const = 0;
    virtual QString resultToken() const = 0;
    virtual QString error() const = 0;
    //! The payment request's public message.
    virtual QString message() const = 0;
    virtual void use() = 0;
    virtual void replace(quint64 prompt) = 0;
    virtual bool requestFile(const QUrl& file) = 0;
    virtual void cancel() = 0;
Q_SIGNALS:
    void changed();
};

/** The send draft that imports apply to: one wallet and its current
 * recipient. Implemented for WalletQmlModel in the app and for the wallet
 * fixture in QML tests. */
class ImportTarget : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    virtual bool available() const = 0;
    //! Changes on user edits of the draft, not on validation echoes.
    virtual QString draftRevision() const = 0;
    //! True while an import is writing into the draft.
    virtual bool importPending() const = 0;
    virtual bool recipientAddressEmpty() const = 0;
    //! The result token of the imported PSBT the wallet is reviewing.
    virtual QString importedReviewToken() const = 0;

    //! Each returns null when imports are unavailable. \p recipient is the
    //! recipient whose field received a paste, or null for the current one.
    virtual ImportOperation* importText(const QString& text, QObject* owner) = 0;
    virtual ImportOperation* importPaste(const QString& text, int field, QObject* recipient, QObject* owner) = 0;
    virtual ImportOperation* importFile(const QString& path, QObject* owner) = 0;
    virtual ImportOperation* importDrop(int url_count, const QUrl& first_url, const QString& text, QObject* owner) = 0;
    //! An import whose file is chosen later with requestFile().
    virtual ImportOperation* captureFile(QObject* owner) = 0;
    //! Points an import made elsewhere (a desktop intent) at this draft.
    virtual bool adopt(ImportOperation* operation) = 0;
Q_SIGNALS:
    void availableChanged();
    void draftRevisionChanged();
    void importPendingChanged();
    void currentRecipientChanged();
    //! A field of the current recipient changed.
    void recipientEdited();
    void importedReviewChanged();
};

#endif // BITCOIN_QML_MODELS_SENDIMPORTPORTS_H
