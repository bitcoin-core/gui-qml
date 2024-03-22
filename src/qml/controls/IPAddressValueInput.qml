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
    validator: RegExpValidator { regExp: /[0-9.:]*/ } // Allow only digits, dots, and colons

    maximumLength: 21

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

    function isValidIPPort(input)
    {
        var parts = input.split(":");
        if (parts.length !== 2) return false;
        if (parts[1].length === 0) return false; // port part is empty
        var ipAddress = parts[0];
        var ipAddressParts = ipAddress.split(".");
        if (ipAddressParts.length !== 4) return false;
        for (var i = 0; (i < ipAddressParts.length); i++) {
            if (ipAddressParts[i].length === 0) return false; // ip group number part is empty
            if (parseInt(ipAddressParts[i]) > 255) return false;
        }
        var port = parseInt(parts[1]);
        if (port < 1 || port > 65535) return false;
        return true;
    }

    // Connections element to ensure validation on editing finished
    Connections {
        target: root
        function onTextChanged() {
            // Validate the input whenever editing is finished
            validInput = isValidIPPort(root.text);
        }
    }
}
