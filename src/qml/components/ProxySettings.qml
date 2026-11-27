// Copyright (c) 2023 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../controls"

import org.bitcoincore.qt 1.0

ColumnLayout {
    id: root
    property var settingsModel: optionsModel
    property var coreSettingsModel: settingsModel.coreSettings
    property string proxyLocationHeader: qsTr("Proxy location")
    readonly property var proxySetting: coreSettingsModel.entry("proxy")
    readonly property var onionSetting: coreSettingsModel.entry("onion")
    property bool proxyEnabled: proxySetting.enabled
    property string proxyAddress: proxySetting.address.length > 0
                                  ? proxySetting.address
                                  : proxySetting.defaultAddress()
    property string proxyValidationError: proxySetting.validate(proxyAddress)
    property bool torEnabled: onionSetting.enabled
    property string torAddress: onionSetting.address.length > 0
                                ? onionSetting.address
                                : onionSetting.defaultAddress()
    property string torValidationError: onionSetting.validate(torAddress)

    signal proxyEnabledEdited(bool enabled)
    signal proxyAddressEdited(string address, string validationError)
    signal torEnabledEdited(bool enabled)
    signal torAddressEdited(string address, string validationError)

    spacing: 4
    Header {
        headerBold: true
        center: false
        header: qsTr("Default Proxy")
        headerSize: 24
        description: qsTr("Run peer connections through a proxy (SOCKS5) for improved privacy. The default proxy supports connections via IPv4, IPv6 and Tor.")
        descriptionSize: 15
        Layout.bottomMargin: 10
    }
    Separator { Layout.fillWidth: true }
    Setting {
        id: defaultProxyEnable
        Layout.fillWidth: true
        header: qsTr("Enable")
        state: root.proxySetting.canEdit ? "FILLED" : "DISABLED"
        infoText: root.proxySetting.infoText
        showInfoText: infoText.length > 0
        actionItem: OptionSwitch {
            objectName: "proxyEnableSwitch"
            checked: root.proxyEnabled
            onToggled: {
                root.proxyEnabledEdited(checked)
            }
        }
        onClicked: {
            loadedItem.toggle()
            loadedItem.toggled()
        }
    }
    Separator { Layout.fillWidth: true }
    Setting {
        id: defaultProxy
        objectName: "proxyAddressSetting"
        Layout.fillWidth: true
        header: proxyLocationHeader
        errorText: loadedItem ? loadedItem.validationError : ""
        state: root.proxyEnabled && root.proxySetting.canEdit ? "FILLED" : "DISABLED"
        showErrorText: loadedItem && !loadedItem.validInput && root.proxyEnabled
        infoText: root.proxySetting.infoText
        showInfoText: !showErrorText && infoText.length > 0
        actionItem: ProxyLocationInput {
            objectName: "proxyAddressInput"
            parentState: defaultProxy.visualState
            accessibleName: qsTr("Default proxy location")
            description: root.proxyAddress
            Component.onCompleted: {
                if (text !== "") filled = true
                validationError = root.proxyValidationError
                validInput = validationError.length === 0
            }
            onDescriptionChanged: {
                validationError = root.proxyValidationError
                validInput = validationError.length === 0
                filled = text !== ""
            }
            onTextChanged: {
                validationError = root.proxySetting.validate(text)
                validInput = validationError.length === 0
                root.proxyAddressEdited(text, validationError)
            }
            onEditingFinished: {
                validationError = root.proxySetting.validate(text)
                validInput = validationError.length === 0
                root.proxyAddressEdited(text, validationError)
                if (validInput) filled = true
            }
        }
        onClicked: {
            loadedItem.beginEdit()
        }
    }
    Separator { Layout.fillWidth: true }
    Header {
        headerBold: true
        center: false
        header: qsTr("Tor Proxy")
        headerSize: 24
        description: qsTr("Run Tor connections through a dedicated proxy.")
        descriptionSize: 15
        Layout.topMargin: 35
        Layout.bottomMargin: 10
    }
    Separator { Layout.fillWidth: true }
    Setting {
        id: torProxyEnable
        Layout.fillWidth: true
        header: qsTr("Enable")
        state: root.onionSetting.canEdit ? "FILLED" : "DISABLED"
        infoText: root.onionSetting.infoText
        showInfoText: infoText.length > 0
        actionItem: OptionSwitch {
            objectName: "torEnableSwitch"
            checked: root.torEnabled
            onToggled: {
                root.torEnabledEdited(checked)
            }
        }
        onClicked: {
            loadedItem.toggle()
            loadedItem.toggled()
        }
    }
    Separator { Layout.fillWidth: true }
    Setting {
        id: torProxy
        objectName: "torAddressSetting"
        Layout.fillWidth: true
        header: proxyLocationHeader
        errorText: loadedItem ? loadedItem.validationError : ""
        state: root.torEnabled && root.onionSetting.canEdit ? "FILLED" : "DISABLED"
        showErrorText: loadedItem && !loadedItem.validInput && root.torEnabled
        infoText: root.onionSetting.infoText
        showInfoText: !showErrorText && infoText.length > 0
        actionItem: ProxyLocationInput {
            objectName: "torAddressInput"
            parentState: torProxy.visualState
            accessibleName: qsTr("Tor proxy location")
            description: root.torAddress
            Component.onCompleted: {
                if (text !== "") filled = true
                validationError = root.torValidationError
                validInput = validationError.length === 0
            }
            onDescriptionChanged: {
                validationError = root.torValidationError
                validInput = validationError.length === 0
                filled = text !== ""
            }
            onTextChanged: {
                validationError = root.onionSetting.validate(text)
                validInput = validationError.length === 0
                root.torAddressEdited(text, validationError)
            }
            onEditingFinished: {
                validationError = root.onionSetting.validate(text)
                validInput = validationError.length === 0
                root.torAddressEdited(text, validationError)
                if (validInput) filled = true
            }
        }
        onClicked: {
            loadedItem.beginEdit()
        }
    }
    Separator { Layout.fillWidth: true }
}
