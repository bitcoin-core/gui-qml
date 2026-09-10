// Copyright (c) 2011-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_MACDOCKICONHANDLER_H
#define BITCOIN_QML_MODELS_MACDOCKICONHANDLER_H

#include <QObject>

class MacDockIconHandler : public QObject
{
    Q_OBJECT

public:
    static MacDockIconHandler *instance();

Q_SIGNALS:
    void dockIconClicked();

private:
    MacDockIconHandler();
};

#endif // BITCOIN_QML_MODELS_MACDOCKICONHANDLER_H
