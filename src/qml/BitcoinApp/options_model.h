// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_OPTIONS_MODEL_H
#define BITCOIN_QML_OPTIONS_MODEL_H

#include <common/settings.h>

#include <QObject>

namespace interfaces {
class Node;
}

/** Model for Bitcoin client options. */
class OptionsQmlModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool listen READ listen WRITE setListen NOTIFY listenChanged)
    Q_PROPERTY(bool prune READ prune WRITE setPrune NOTIFY pruneChanged)
    Q_PROPERTY(int pruneSizeGB READ pruneSizeGB WRITE setPruneSizeGB NOTIFY pruneSizeGBChanged)

public:
    explicit OptionsQmlModel(interfaces::Node& node);

    bool listen() const { return m_listen; }
    void setListen(bool new_listen);
    bool prune() const { return m_prune; }
    void setPrune(bool new_prune);
    int pruneSizeGB() const { return m_prune_size_gb; }
    void setPruneSizeGB(int new_prune_size);

Q_SIGNALS:
    void listenChanged(bool new_listen);
    void pruneChanged(bool new_prune);
    void pruneSizeGBChanged(int new_prune_size_gb);

private:
    interfaces::Node& m_node;

    // Properties that are exposed to QML.
    bool m_listen;
    bool m_prune;
    int m_prune_size_gb;

    common::SettingsValue pruneSetting() const;
};

#endif // BITCOIN_QML_OPTIONS_MODEL_H
