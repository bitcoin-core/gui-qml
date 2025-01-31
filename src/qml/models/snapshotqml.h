// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_SNAPSHOTQML_H
#define BITCOIN_QML_MODELS_SNAPSHOTQML_H

#include <interfaces/handler.h>
#include <interfaces/node.h>

#include <QObject>

class SnapshotQml : public QObject
{
   Q_OBJECT
public:
    SnapshotQml(interfaces::Node& node, QString path);

    bool processPath();

private:
    interfaces::Node& m_node;
    QString m_path;
};

#endif // BITCOIN_QML_MODELS_SNAPSHOTQML_H
