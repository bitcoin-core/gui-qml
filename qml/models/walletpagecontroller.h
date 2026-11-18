// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_WALLETPAGECONTROLLER_H
#define BITCOIN_QML_MODELS_WALLETPAGECONTROLLER_H

#include <qml/models/walletinterface.h>

#include <QMetaObject>
#include <QObject>
#include <QPointer>

/** Base of a wallet page's controller (doc/architecture.md, "Scopes").
 *
 * The page binds `active` to its visibility and gives the controller its
 * wallet's interface once. The controller is `available` while the page is
 * shown and the wallet can be used. When either ends, or the controller is
 * destroyed, it stops: prompts and work the backend has not admitted end,
 * admitted work finishes on its wallet. Subclasses check available() before
 * starting work and never follow the wallet's availability themselves. */
class WalletPageController : public QObject
{
    Q_OBJECT
    //! The page is shown. Bound by the page, e.g. `active: root.visible`.
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    //! The page is shown and its wallet can be used.
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
public:
    ~WalletPageController() override;

    bool active() const { return m_active; }
    void setActive(bool active);
    bool available() const;

Q_SIGNALS:
    void activeChanged();
    void availableChanged();

protected:
    explicit WalletPageController(QObject* parent);
    //! Binds the wallet once and returns true. Null is ignored. A second,
    //! different wallet is a page bug: the controller warns, rejects it,
    //! stops and stays disabled until its page is recreated, so a delayed
    //! callback for the first wallet cannot start work on either.
    bool bindWallet(WalletInterface* wallet);
    WalletInterface* boundWallet() const { return m_disabled ? nullptr : m_wallet.data(); }
    //! Ends prompts and work that has not been admitted; keeps admitted
    //! outcomes. Called when `available` turns false. A subclass calls it
    //! from its own destructor too.
    virtual void stop() = 0;

private:
    void update();

    QPointer<WalletInterface> m_wallet;
    QMetaObject::Connection m_wallet_available;
    bool m_active{false};
    bool m_available{false};
    bool m_disabled{false};
};

#endif // BITCOIN_QML_MODELS_WALLETPAGECONTROLLER_H
