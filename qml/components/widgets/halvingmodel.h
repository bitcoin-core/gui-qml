// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_COMPONENTS_WIDGETS_HALVINGMODEL_H
#define BITCOIN_QML_COMPONENTS_WIDGETS_HALVINGMODEL_H

#include <QObject>

/** Network-specific subsidy milestones, derived from the active chain height.
 * Arrival is a projection at the network target spacing, not a wall-clock promise.
 */
class HalvingModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(bool complete READ complete NOTIFY changed)
    Q_PROPERTY(double progress READ progress NOTIFY changed)
    Q_PROPERTY(int blocksLeft READ blocksLeft NOTIFY changed)
    Q_PROPERTY(int periodStart READ periodStart NOTIFY changed)
    Q_PROPERTY(int nextHeight READ nextHeight NOTIFY changed)
    Q_PROPERTY(double secondsRemaining READ secondsRemaining NOTIFY changed)
    Q_PROPERTY(double subsidy READ subsidy NOTIFY changed)
    Q_PROPERTY(double nextSubsidy READ nextSubsidy NOTIFY changed)
public:
    explicit HalvingModel(int interval, int spacing, QObject* parent = nullptr);
    bool available() const { return m_height >= 0 && m_interval > 0 && m_spacing > 0; }
    bool complete() const { return available() && subsidySats() == 0; }
    double progress() const;
    int blocksLeft() const;
    int periodStart() const;
    int nextHeight() const;
    double secondsRemaining() const { return available() ? double(blocksLeft()) * m_spacing : -1; }
    double subsidy() const { return available() ? subsidySats() / 100000000.0 : -1; }
    double nextSubsidy() const { return available() ? (subsidySats() >> 1) / 100000000.0 : -1; }
    void setHeight(int height);
Q_SIGNALS:
    void changed();
private:
    qint64 subsidySats() const;
    const int m_interval;
    const int m_spacing;
    int m_height{-1};
};

#endif // BITCOIN_QML_COMPONENTS_WIDGETS_HALVINGMODEL_H
