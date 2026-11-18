// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/walletopenoperation.h>

#include <utility>

WalletOpenOperation::WalletOpenOperation(quint64 id, Live live, QObject* parent)
    : QObject(parent), m_id(id), m_live(std::move(live)), m_following(true)
{
}

WalletOpenOperation::WalletOpenOperation(QString target, QString error, QObject* parent)
    : QObject(parent), m_id(0)
{
    m_frozen.state = State::WalletOperationFailed;
    m_frozen.target = std::move(target);
    m_frozen.error = std::move(error);
    m_frozen.adoption = Adoption::AdoptionUnavailable;
}

void WalletOpenOperation::freeze()
{
    if (!m_following) return;
    if (m_live.current()) {
        m_frozen.state = m_live.state(); m_frozen.target = m_live.target();
        m_frozen.error = m_live.error(); m_frozen.warnings = m_live.warnings();
        m_frozen.result = m_live.result(); m_frozen.can_open = m_live.can_open();
        m_frozen.adoption = m_live.adoption(); m_frozen.adoption_error = m_live.adoption_error();
    }
    m_following = false;
    m_live = {};
}
