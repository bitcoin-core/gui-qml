// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/networkstatusmodel.h>

#include <QtNetwork/QNetworkInformation>

NetworkStatusModel::NetworkStatusModel(QObject* parent)
    : NetworkStatusModel{true, parent}
{
}

NetworkStatusModel::NetworkStatusModel(bool monitor, QObject* parent)
    : QObject{parent},
      m_monitor{monitor}
{
    if (m_monitor) {
        initializeMonitoring();
    }
}

QString NetworkStatusModel::reachability() const
{
    switch (m_reachability) {
    case QNetworkInformation::Reachability::Disconnected:
        return QStringLiteral("Disconnected");
    case QNetworkInformation::Reachability::Local:
        return QStringLiteral("Local");
    case QNetworkInformation::Reachability::Site:
        return QStringLiteral("Site");
    case QNetworkInformation::Reachability::Online:
        return QStringLiteral("Online");
    case QNetworkInformation::Reachability::Unknown:
        return QStringLiteral("Unknown");
    }
    return QStringLiteral("Unknown");
}

void NetworkStatusModel::setReachability(QNetworkInformation::Reachability reachability)
{
    const bool old_available{m_reachability_available};
    const bool old_offline{networkOffline()};
    const QString old_name{this->reachability()};

    m_reachability_available = true;
    m_reachability = reachability;

    if (old_available != m_reachability_available || old_offline != networkOffline() || old_name != this->reachability()) {
        Q_EMIT reachabilityChanged();
    }
}

void NetworkStatusModel::initializeMonitoring()
{
    if (!QNetworkInformation::instance()) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 4, 0)
        QNetworkInformation::loadBackendByFeatures(QNetworkInformation::Feature::Reachability);
#else
        QNetworkInformation::load(QNetworkInformation::Feature::Reachability);
#endif
    }

    QNetworkInformation* info{QNetworkInformation::instance()};
    if (!info) {
        return;
    }

    m_reachability_available = info->supports(QNetworkInformation::Feature::Reachability);
    if (!m_reachability_available) {
        return;
    }

    setReachability(info->reachability());
    connect(info, &QNetworkInformation::reachabilityChanged, this, &NetworkStatusModel::setReachability);
}
