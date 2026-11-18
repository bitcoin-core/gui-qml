// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_WALLETSENDDRAFT_H
#define BITCOIN_QML_MODELS_WALLETSENDDRAFT_H

#include <qml/models/preparationoperation.h>
#include <qml/models/senddraftinterface.h>

#include <QPointer>

class WalletQmlModel;

/** A preparation of a WalletQmlModel's send draft. The wallet prepares one
 * draft at a time; this operation follows the one it started. */
class WalletPreparationOperation final : public PreparationOperation
{
    Q_OBJECT
public:
    explicit WalletPreparationOperation(WalletQmlModel& wallet, QObject* parent = nullptr);
    //! False when the wallet can't prepare now.
    bool start(std::optional<QString> passphrase);
    void unlock(const QString& passphrase) override;
    void cancel() override;

private:
    void follow();
    QPointer<WalletQmlModel> m_wallet;
    quint64 m_preparation{0};
};

/** WalletQmlModel's SendDraftInterface. */
class WalletSendDraftInterface final : public SendDraftInterface
{
    Q_OBJECT
public:
    explicit WalletSendDraftInterface(WalletQmlModel& wallet);
    bool available() const override;
    QString draftRevision() const override;
    bool selectionPending() const override;
    bool draftReady() const override;
    bool sweepsWallet() const override;
    void resetDraft() override;
    PreparationOperation* startPreparation(std::optional<QString> passphrase) override;
    PsbtOperation* startImport(const QString& path) override;

private:
    QPointer<WalletQmlModel> m_wallet;
};

#endif // BITCOIN_QML_MODELS_WALLETSENDDRAFT_H
