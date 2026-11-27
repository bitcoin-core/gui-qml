// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_DESKTOPINTENTARGS_H
#define BITCOIN_QML_DESKTOPINTENTARGS_H
#include <qml/desktopintent.h>

namespace desktopintent {
struct Arguments {
    int options_argc{1};
    std::vector<Input> intents;
    QString error;
};
// Qt continues receiving synthetic argv. The actual Core parser receives only
// options before the first request. working_directory is a captured value.
Arguments ParseArguments(int argc, char* const argv[], const QString& producer,
                         const QString& working_directory);
} // namespace desktopintent
#endif // BITCOIN_QML_DESKTOPINTENTARGS_H
