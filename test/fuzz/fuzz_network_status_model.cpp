// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/networkstatusmodel.h>

#include <QByteArray>
#include <QApplication>
#include <QString>

#include <QtNetwork/QNetworkInformation>

#include <cstddef>
#include <cstdint>

static int argc = 1;
static char arg0[] = "fuzz_network_status_model";
static char* argv[] = {arg0, nullptr};
static QApplication app(argc, argv);

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    // Construct without system reachability monitoring so the fuzzer stays
    // deterministic and does not depend on the host network state.
    NetworkStatusModel model(/*monitor=*/false);

    if (size > 0) {
        const uint8_t value = data[0] % 6;
        QNetworkInformation::Reachability reachability = QNetworkInformation::Reachability::Unknown;
        switch (value) {
        case 0: reachability = QNetworkInformation::Reachability::Unknown; break;
        case 1: reachability = QNetworkInformation::Reachability::Disconnected; break;
        case 2: reachability = QNetworkInformation::Reachability::Local; break;
        case 3: reachability = QNetworkInformation::Reachability::Site; break;
        case 4: reachability = QNetworkInformation::Reachability::Online; break;
        case 5: reachability = static_cast<QNetworkInformation::Reachability>(999); break;
        }
        model.setReachability(reachability);
    }

    (void)model.reachabilityAvailable();
    (void)model.networkOffline();
    (void)model.reachability();
    return 0;
}
