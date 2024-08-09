// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import org.bitcoincore.qt 1.0

import "../../controls"

Button {
    id: root

    function formatSatoshis(satoshis) {
        var highlightColor = Theme.color.neutral9
        var zeroColor = Theme.color.neutral7

        if (root.checked || root.hovered) {
            highlightColor = zeroColor = Theme.color.orange
        }

        // Convert satoshis to bitcoins
        var bitcoins = satoshis / 100000000;

        // Format bitcoins to a fixed 8 decimal places string
        var bitcoinStr = bitcoins.toFixed(8);

        // Split the bitcoin string into integer and fractional parts
        var parts = bitcoinStr.split('.');

        // Add spaces for every 3 digits in the integer part
        parts[0] = parts[0].replace(/\B(?=(\d{3})+(?!\d))/g, ' ');

        // Highlight the first significant digit and all following digits in the integer part
        var significantFound = false;
        parts[0] = parts[0].replace(/(\d)/g, function(match) {
            if (!significantFound && match !== '0') {
                significantFound = true;
            }
            if (significantFound) {
                return '<font color="' + highlightColor + '">' + match + '</font>';
            }
            return match;
        });

        // Add spaces for every 3 digits in the decimal part
        parts[1] = parts[1].replace(/\B(?=(\d{3})+(?!\d))/g, ' ');
        if (significantFound) {
            parts[1] = '<font color="' + highlightColor + '">' + parts[1] + '</font>';
        } else {
            // Highlight the first significant digit and all following digits in the fractional part
            significantFound = false;
            parts[1] = parts[1].replace(/(\d)/g, function(match) {
                if (!significantFound && match !== '0') {
                    significantFound = true;
                }
                if (significantFound) {
                    return '<font color="' + highlightColor + '">' + match + '</font>';
                }
                return match;
            });
        }

        // Concatenate the parts back together
        var formattedBitcoins = parts.join('.');

        // Format the text with the Bitcoin symbol
        var formattedText = `<font color="${highlightColor}">₿</font> ${formattedBitcoins}`;

        // Highlight zero in a different color if satoshis are zero
        if (satoshis === 0) {
            formattedText = `<font color="${zeroColor}">₿ 0.00</font>`;
        }

        return formattedText;
    }

    property color bgActiveColor: Theme.color.neutral2
    property color textColor: Theme.color.neutral7
    property color textHoverColor: Theme.color.orange
    property color textActiveColor: Theme.color.orange
    property color iconColor: "transparent"
    property string iconSource: ""
    property bool showBalance: true
    property bool showIcon: true

    checkable: true
    hoverEnabled: AppMode.isDesktop
    implicitHeight: 60
    implicitWidth: 220
    bottomPadding: 0
    topPadding: 0
    clip: true

    contentItem: RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 5
        anchors.rightMargin: 5
        clip: true
        spacing: 5
        Icon {
            id: icon
            visible: root.showIcon
            source: "image://images/singlesig-wallet"
            color: Theme.color.neutral8
            size: 30
            Layout.minimumWidth: 30
            Layout.preferredWidth: 30
            Layout.maximumWidth: 30
        }
        ColumnLayout {
            spacing: 2
            CoreText {
                horizontalAlignment: Text.AlignLeft
                Layout.fillWidth: true
                wrap: false
                id: buttonText
                font.pixelSize: 13
                text: root.text
                color: root.textColor
                bold: true
                visible: root.text !== ""
            }
            CoreText {
                id: balanceText
                visible: root.showBalance
                text: formatSatoshis(12300)
                color: Theme.color.neutral7
            }
        }
    }

    background: Rectangle {
        id: bg
        height: root.height
        width: root.width
        radius: 5
        color: Theme.color.neutral3
        visible: root.hovered || root.checked

        FocusBorder {
            visible: root.visualFocus
        }

        Behavior on color {
            ColorAnimation { duration: 150 }
        }
    }

    states: [
        State {
            name: "CHECKED"; when: root.checked
            PropertyChanges { target: buttonText; color: root.textActiveColor }
            PropertyChanges { target: icon; color: root.textActiveColor }
            PropertyChanges { target: balanceText; color: root.textActiveColor }
        },
        State {
            name: "HOVER"; when: root.hovered
            PropertyChanges { target: buttonText; color: root.textHoverColor }
            PropertyChanges { target: icon; color: root.textHoverColor }
            PropertyChanges { target: balanceText; color: root.textHoverColor }
        }
    ]
}
