// Copyright (c) 2024 The Bitcoin Core developers
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
    // Expose a property to indicate validity, initial value will be true (no error message displayed)
    property bool validInput: true
    enabled: true
    state: root.parentState
    validator: RegularExpressionValidator { regularExpression: /^[\][0-9a-f.:]+$/i } // Allow only IPv4/ IPv6 chars

    maximumLength: 47

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
}
