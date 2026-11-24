// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_WALLETPSBTOPERATION_H
#define BITCOIN_QML_MODELS_WALLETPSBTOPERATION_H

#include <qml/models/psbtoperation.h>

#include <QPointer>

class WalletModel;

/** A PSBT action of a WalletModel. The wallet runs one PSBT action at a
 * time; this operation follows the one it started and stops when the wallet
 * starts another. */
class WalletPsbtOperation final : public PsbtOperation
{
    Q_OBJECT
public:
    WalletPsbtOperation(WalletModel& wallet, Kind kind, QObject* parent = nullptr);
    //! False when the wallet can't start the action now.
    bool start(const QString& path, const QString& signer_result);
    void cancel() override;

private:
    void follow();
    QPointer<WalletModel> m_wallet;
    quint64 m_action{0};
};

#endif // BITCOIN_QML_MODELS_WALLETPSBTOPERATION_H
