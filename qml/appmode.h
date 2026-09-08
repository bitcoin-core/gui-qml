// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_APPMODE_H
#define BITCOIN_QML_APPMODE_H

#include <QObject>

class AppMode : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool isDesktop READ isDesktop NOTIFY modeChanged)
    Q_PROPERTY(bool isMobile READ isMobile NOTIFY modeChanged)
    Q_PROPERTY(bool walletEnabled READ walletEnabled NOTIFY walletEnabledChanged)
    Q_PROPERTY(bool adaptiveSidebarLayoutAvailable READ adaptiveSidebarLayoutAvailable CONSTANT)
    Q_PROPERTY(bool adaptiveSidebarLayout READ adaptiveSidebarLayout WRITE setAdaptiveSidebarLayout NOTIFY adaptiveSidebarLayoutChanged)
    Q_PROPERTY(QString state READ state NOTIFY modeChanged)

public:
    enum Mode {
        DESKTOP,
        MOBILE
    };

    explicit AppMode(Mode mode, bool wallet_enabled);

    bool isMobile() { return m_mode == MOBILE; }
    bool isDesktop() { return m_mode == DESKTOP; }
    bool walletEnabled() { return m_wallet_enabled; }
    bool adaptiveSidebarLayoutAvailable() const { return m_adaptive_sidebar_layout_available; }
    bool adaptiveSidebarLayout() const { return m_adaptive_sidebar_layout; }
    void setAdaptiveSidebarLayout(bool enabled);
    void setWalletEnabled(bool wallet_enabled)
    {
        if (m_wallet_enabled == wallet_enabled) return;
        m_wallet_enabled = wallet_enabled;
        Q_EMIT walletEnabledChanged();
    }
    QString state()
    {
        return m_mode == MOBILE ? "MOBILE" : "DESKTOP";
    }
    Mode mode() const { return m_mode; }

Q_SIGNALS:
    void modeChanged();
    void walletEnabledChanged();
    void adaptiveSidebarLayoutChanged();

private:
    const Mode m_mode;
    bool m_wallet_enabled;
    const bool m_adaptive_sidebar_layout_available;
    bool m_adaptive_sidebar_layout{false};
};

#endif // BITCOIN_QML_APPMODE_H
