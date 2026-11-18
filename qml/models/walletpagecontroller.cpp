// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/walletpagecontroller.h>

#include <QDebug>

WalletPageController::WalletPageController(QObject* parent) : QObject(parent) {}

WalletPageController::~WalletPageController()
{
    disconnect(m_wallet_available);
}

bool WalletPageController::available() const
{
    return m_active && !m_disabled && m_wallet && m_wallet->available();
}

void WalletPageController::setActive(bool active)
{
    if (m_active == active) return;
    m_active = active;
    const QPointer<WalletPageController> guard{this};
    Q_EMIT activeChanged();
    if (guard) update();
}

bool WalletPageController::bindWallet(WalletInterface* wallet)
{
    if (!wallet || m_disabled) return false;
    if (m_wallet == wallet) return true;
    if (m_wallet) {
        qWarning() << metaObject()->className() << "was given a second wallet; it stays disabled until its page is recreated";
        m_disabled = true;
        disconnect(m_wallet_available);
        update();
        return false;
    }
    m_wallet = wallet;
    m_wallet_available = connect(wallet, &WalletInterface::availableChanged, this, &WalletPageController::update);
    // The wallet's own destruction ends its availability too. Never ask a
    // destroyed wallet; its subclass part is gone.
    connect(wallet, &QObject::destroyed, this, [this] { m_wallet.clear(); update(); });
    update();
    return true;
}

void WalletPageController::update()
{
    const bool now = available();
    if (now == m_available) return;
    m_available = now;
    const QPointer<WalletPageController> guard{this};
    if (!now) stop();
    if (guard) Q_EMIT availableChanged();
}
