// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Layouts 1.15
import "../../controls"

OnboardingView {
    id: root
    objectName: "walletImportErrorPage"
    showNavigationBar: false
    usesSharedNavigation: true
    property bool modalView: false
    signal cancel()
    signal retryRequested()

    title: ""
    heading: walletController.walletImportErrorTitle
    subheading: walletController.walletImportErrorDescription
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
            color: Theme.color.amber
        }
        Icon {
            anchors.centerIn: parent
            source: "qrc:/icons/cross.svg"
            color: Theme.color.white
            size: 26
        }
    }

    childView: ColumnLayout {
        objectName: "importWalletErrorView"
        CoreText {
            objectName: "importWalletErrorHelpText"
            Layout.fillWidth: true
            visible: text.length > 0
            text: walletController.walletImportErrorHelpText
            color: Theme.color.neutral6
            font: Theme.text.caption.font
            wrap: true
            horizontalAlignment: Text.AlignHCenter
        }
    }
}
