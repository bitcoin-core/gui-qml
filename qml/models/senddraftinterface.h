// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_SENDDRAFTINTERFACE_H
#define BITCOIN_QML_MODELS_SENDDRAFTINTERFACE_H

#include <qml/models/walletinterface.h>
#include <qml/models/preparationoperation.h>
#include <qml/models/psbtoperation.h>

#include <QObject>
#include <QString>

#include <optional>

/** What a SendDraftController needs from a wallet: its send draft and the
 * actions that turn it into a review. WalletQmlModel implements it; tests use
 * a fake. */
class SendDraftInterface : public WalletInterface
{
    Q_OBJECT
public:
    using WalletInterface::WalletInterface;

    //! Changes whenever the user changes the draft or its coin selection.
    virtual QString draftRevision() const = 0;
    //! A coin selection change is still being applied.
    virtual bool selectionPending() const = 0;
    //! Prepares the draft for review, with the wallet password if given.
    //! Returns null while another wallet action runs; the caller owns it.
    virtual PreparationOperation* startPreparation(std::optional<QString> passphrase) = 0;
    //! Imports a PSBT file as the review. Returns null while another wallet
    //! action runs.
    virtual PsbtOperation* startImport(const QString& path) = 0;

Q_SIGNALS:
    void draftChanged();
};

#endif // BITCOIN_QML_MODELS_SENDDRAFTINTERFACE_H
