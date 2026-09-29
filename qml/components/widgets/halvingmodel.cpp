// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/components/widgets/halvingmodel.h>

HalvingModel::HalvingModel(int interval, int spacing, QObject* parent)
    : QObject(parent), m_interval(interval), m_spacing(spacing)
{
}

void HalvingModel::setHeight(int height)
{
    if (m_height == height) return;
    m_height = height;
    Q_EMIT changed();
}

qint64 HalvingModel::subsidySats() const
{
    if (!available()) return 0;
    const int halvings = m_height / m_interval;
    return halvings >= 64 ? 0 : (qint64{50} * 100000000) >> halvings;
}

double HalvingModel::progress() const
{
    return !available() ? 0 : complete() ? 1 : double(m_height % m_interval) / m_interval;
}

int HalvingModel::blocksLeft() const
{
    return !available() ? -1 : complete() ? 0 : m_interval - m_height % m_interval;
}

int HalvingModel::periodStart() const
{
    return available() ? m_height - m_height % m_interval : -1;
}

int HalvingModel::nextHeight() const
{
    return available() && !complete() ? periodStart() + m_interval : -1;
}
