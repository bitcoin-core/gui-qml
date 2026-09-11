// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_NETWORKSTATUSMODEL_H
#define BITCOIN_QML_MODELS_NETWORKSTATUSMODEL_H

#include <QObject>
#include <QString>

#include <QtNetwork/QNetworkInformation>

class NetworkStatusModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool reachabilityAvailable READ reachabilityAvailable NOTIFY reachabilityChanged)
    Q_PROPERTY(bool networkOffline READ networkOffline NOTIFY reachabilityChanged)
    Q_PROPERTY(QString reachability READ reachability NOTIFY reachabilityChanged)

public:
    explicit NetworkStatusModel(QObject* parent = nullptr);
    explicit NetworkStatusModel(bool monitor, QObject* parent = nullptr);

    bool reachabilityAvailable() const { return m_reachability_available; }
    bool networkOffline() const
    {
        return m_reachability_available &&
               m_reachability == QNetworkInformation::Reachability::Disconnected;
    }
    QString reachability() const;

    void setReachability(QNetworkInformation::Reachability reachability);

Q_SIGNALS:
    void reachabilityChanged();

private:
    void initializeMonitoring();

    bool m_monitor{true};
    bool m_reachability_available{false};
    QNetworkInformation::Reachability m_reachability{QNetworkInformation::Reachability::Unknown};
};

#endif // BITCOIN_QML_MODELS_NETWORKSTATUSMODEL_H
