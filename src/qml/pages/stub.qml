// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Layouts 1.12
import "../components" as BitcoinCoreComponents
import "onboarding" as BitcoinCoreOnboarding


ApplicationWindow {
    id: appWindow
    title: "Bitcoin Core TnG"
    minimumWidth: 750
    minimumHeight: 450
    width: 800
    height: 800
    background: Rectangle {
        color: "black"
    }
    visible: true

    Component.onCompleted: nodeModel.startNodeInitializionThread();

    BitcoinCoreOnboarding.Onboarding00Welcome {
        id: welcomePage
        anchors.centerIn: parent
    }
}
