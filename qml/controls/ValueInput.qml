// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15

TextInput {
    id: root
    required property string parentState
    property string description: ""
    property bool filled: false
    property int descriptionSize: 18
    property color textColor: root.filled ? Theme.color.neutral9 : Theme.color.neutral5
    enabled: true
    state: root.parentState
    validator: IntValidator{}
    maximumLength: 5

    states: [
        State {
            name: "ACTIVE"
            PropertyChanges { target: root; textColor: Theme.color.orange }
        },
        State {
            name: "HOVER"
            PropertyChanges {
                target: root
                textColor: root.filled ? Theme.color.orangeLight1 : Theme.color.neutral5
            }
        },
        State {
            name: "DISABLED"
            PropertyChanges {
                target: root
                enabled: false
                textColor: Theme.color.neutral4
            }
        }
    ]

    font.family: "Inter"
    font.styleName: "Regular"
    font.pixelSize: root.descriptionSize
    color: root.textColor
    text: root.description
    horizontalAlignment: Text.AlignRight
    wrapMode: Text.WordWrap

    Behavior on color {
        ColorAnimation { duration: 150 }
    }

    function checkValidity(minVal, maxVal, input) {
        if (input < minVal || input > maxVal) {
            return false
        } else {
            return true
        }
    }
}
