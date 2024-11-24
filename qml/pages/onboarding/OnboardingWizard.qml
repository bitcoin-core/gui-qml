// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../../controls"

PageStack {
    id: root

    signal finished()
    initialItem: cover

    Component {
        id: cover
        OnboardingCover {
            onNext: root.push(strengthen)
        }
    }
    Component {
        id: strengthen
        OnboardingStrengthen {
            onBack: root.pop()
            onNext: root.push(blockclock)
        }
    }
    Component {
        id: blockclock
        OnboardingBlockclock {
            onBack: root.pop()
            onNext: root.push(storageLocation)
        }
    }
    Component {
        id: storageLocation
        OnboardingStorageLocation {
            onBack: root.pop()
            onNext: root.push(storageAmount)
        }
    }
    Component {
        id: storageAmount
        OnboardingStorageAmount {
            onBack: root.pop()
            onNext: root.push(connection)
        }
    }
    Component {
        id: connection
        OnboardingConnection {
            onBack: root.pop()
            onNext: root.finished()
        }
    }
}