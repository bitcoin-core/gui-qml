// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_FEEBUMPCONTROLLER_H
#define BITCOIN_QML_MODELS_FEEBUMPCONTROLLER_H

#include <qml/models/feebumpinterface.h>
#include <qml/models/walletpagecontroller.h>

#include <QPointer>
#include <QString>

/** The Speed up overlay's fee bump. Declared by SpeedUpOverlay. Opening shows
 * the wallet's unresolved attempt for the transaction if there is one, else
 * starts a review. When the overlay hides, a review that was not committed is
 * discarded; a committed attempt stays with the wallet and its result is
 * shown by the next open, once. */
class FeeBumpController : public WalletPageController
{
    Q_OBJECT
    //! The wallet whose transactions this page bumps, given once.
    Q_PROPERTY(FeeBumpInterface* feeBumps READ feeBumps WRITE setFeeBumps NOTIFY feeBumpsChanged)
    //! The attempt this page shows, or null.
    Q_PROPERTY(FeeBumpOperation* operation READ operation NOTIFY changed)
    Q_PROPERTY(bool readyToConfirm READ readyToConfirm NOTIFY changed)
    Q_PROPERTY(bool readyToPrepareAgain READ readyToPrepareAgain NOTIFY changed)
    Q_PROPERTY(bool uncertainOutcome READ uncertainOutcome NOTIFY changed)
    Q_PROPERTY(bool succeeded READ succeeded NOTIFY changed)
    Q_PROPERTY(bool needsUnlock READ needsUnlock NOTIFY changed)
public:
    explicit FeeBumpController(QObject* parent = nullptr);
    ~FeeBumpController() override;

    FeeBumpInterface* feeBumps() const { return m_fee_bumps; }
    void setFeeBumps(FeeBumpInterface* fee_bumps);
    FeeBumpOperation* operation() const { return m_operation; }
    bool readyToConfirm() const { return is(FeeBumpOperation::NeedsConfirmation); }
    bool readyToPrepareAgain() const { return is(FeeBumpOperation::NeedsRevalidation); }
    bool uncertainOutcome() const { return is(FeeBumpOperation::Uncertain); }
    bool succeeded() const { return is(FeeBumpOperation::Succeeded); }
    bool needsUnlock() const { return m_operation && m_operation->needsUnlock(); }

    //! Shows the wallet's unresolved attempt for `txid`, or starts a review.
    Q_INVOKABLE bool open(const QString& txid);
    //! Confirms, prepares again or rechecks, by the attempt's state. A new
    //! commit is never offered while the replacement is unresolved.
    Q_INVOKABLE bool proceed();
    //! Confirms with the wallet password.
    Q_INVOKABLE bool unlock(const QString& passphrase);
    //! The page showed the final result: the wallet forgets the attempt.
    Q_INVOKABLE void dismiss();

Q_SIGNALS:
    void feeBumpsChanged();
    void changed();

protected:
    void stop() override;

private:
    bool is(FeeBumpOperation::State state) const { return m_operation && m_operation->state() == state; }
    void let(FeeBumpOperation* operation);

    QPointer<FeeBumpInterface> m_fee_bumps;
    QPointer<FeeBumpOperation> m_operation;
    QString m_txid;
    //! The page showed the attempt's final result while it was available.
    bool m_shown_final{false};
};

#endif // BITCOIN_QML_MODELS_FEEBUMPCONTROLLER_H
