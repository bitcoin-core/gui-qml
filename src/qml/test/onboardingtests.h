// Copyright (c) 2014-2023 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QObject>
#include <QQmlEngine>

class Setup : public QObject
{
    Q_OBJECT

public:
    Setup(); 
    ~Setup() = default;

public Q_SLOTS:
    void qmlEngineAvailable(QQmlEngine * engine); 
};

