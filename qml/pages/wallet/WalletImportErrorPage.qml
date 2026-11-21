// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import org.bitcoincore.qt 1.0
import QtQuick.Layouts 1.15
import "../../controls"
import "../../components"

OnboardingView {
    id: root
    objectName: "walletImportErrorPage"
    showNavigationBar: false
    usesSharedNavigation: true
    required property WalletOperationModel operation
    property bool modalView: false
    signal cancel()
    signal retryRequested()

    title: ""
    heading: root.operation.errorTitle
    subheading: root.operation.errorDescription
    headingObjectName: "importWalletErrorTitle"
    subheadingObjectName: "importWalletErrorDescription"
    maximumContentWidth: 800
    showBackButton: true
    showCloseButton: modalView
    primaryButtonText: ""
    secondaryButtonText: qsTr("Choose another file")
    secondaryButtonObjectName: "importWalletChooseAnotherFileButton"
    onCloseClicked: root.cancel()
    onSecondaryClicked: root.retryRequested()

    imageView: Item {
        implicitWidth: 72
        implicitHeight: 72
        Rectangle {
            anchors.fill: parent
            radius: width / 2
            color: Theme.color.danger
        }
        Icon {
            anchors.centerIn: parent
            source: "qrc:/icons/cross.svg"
            color: Theme.color.accentForeground
            size: 26
        }
    }

    childView: ColumnLayout {
        objectName: "importWalletErrorView"
        ListView {
            id: errorDetails
            objectName: "importWalletErrorHelpText"
            Layout.fillWidth: true
            Layout.preferredHeight: count > 0 ? Math.min(240, Math.max(48, contentHeight)) : 0
            visible: count > 0
            clip: true
            reuseItems: true
            cacheBuffer: 0
            model: root.operation.errorDetails
            delegate: CoreText {
                required property string content
                width: errorDetails.width
                text: content
                textFormat: Text.PlainText
                color: Theme.color.neutral6
                textStyle: Theme.text.caption
                wrap: true
                horizontalAlignment: Text.AlignLeft
            }
        }
        CoreText {
            Layout.fillWidth: true
            text: root.operation.error.recovery
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
        }
        CoreText {
            Layout.fillWidth: true
            text: root.operation.result.recoveryPaths.join("\n")
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
        }
        WalletOperationWarnings { Layout.fillWidth: true; operation: root.operation }
    }
}
