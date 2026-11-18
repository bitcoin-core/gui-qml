// Copyright (c) 2011-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_MACDOCKICONHANDLER_H
#define BITCOIN_QML_MODELS_MACDOCKICONHANDLER_H

#include <QObject>

// Stock Core Qt also defines a global MacDockIconHandler. Keep this app
// handoff type and its generated meta-object in an independent namespace.
namespace appdesktop {
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

} // namespace appdesktop

#endif // BITCOIN_QML_MODELS_MACDOCKICONHANDLER_H
