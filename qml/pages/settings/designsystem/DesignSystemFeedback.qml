// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Layouts 1.15

import "../../../controls"
import "../../../components"

DesignSystemSection {
    id: root

    // ToastBanner fades in when it becomes visible, so the samples start hidden.
    property bool toastBannersShown: false

    Component.onCompleted: toastBannersShown = true

    title: "Feedback"
    description: "Banners, pills, tooltips, toasts and loading indicators."

    DesignSystemSpecimen {
        objectName: "designSystemInfoBanner"
        Layout.fillWidth: true
        name: "InfoBanner"

        DesignSystemState {
            width: parent.width
            label: "Horizontal, title, message"
            InfoBanner {
                Layout.fillWidth: true
                iconSource: "image://images/info-filled"
                title: "Pruned node"
                message: "Older blocks are deleted to save space."
            }
        }
        DesignSystemState {
            width: parent.width
            label: "primaryButtonText, dismissButtonText"
            InfoBanner {
                Layout.fillWidth: true
                iconSource: "image://images/alert-filled"
                message: "A newer wallet format is available."
                primaryButtonText: "Migrate"
                dismissButtonText: "Later"
            }
        }
        DesignSystemState {
            width: 380
            label: "Vertical, showsCloseButton"
            InfoBanner {
                Layout.fillWidth: true
                bannerLayout: InfoBanner.Layout.Vertical
                showsCloseButton: true
                iconSource: "image://images/info-filled"
                title: "Back up your wallet"
                message: "Save a copy of your wallet file somewhere safe."
                primaryButtonText: "Back up"
            }
        }
    }

    DesignSystemSpecimen {
        objectName: "designSystemSettingsRestartNotice"
        Layout.fillWidth: true
        name: "SettingsRestartNotice"

        DesignSystemState {
            width: 460
            label: "rest"
            SettingsRestartNotice {}
        }
    }

    DesignSystemSpecimen {
        objectName: "designSystemToastBanner"
        Layout.fillWidth: true
        name: "ToastBanner"

        DesignSystemState {
            width: 380
            label: "tintColor: green"
            ToastBanner {
                Layout.fillWidth: true
                shown: root.toastBannersShown
                tintColor: Theme.color.green
                iconSource: "image://images/check"
                text: "Signature verified."
            }
        }
        DesignSystemState {
            width: 380
            label: "tintColor: red"
            ToastBanner {
                Layout.fillWidth: true
                shown: root.toastBannersShown
                tintColor: Theme.color.red
                iconSource: "image://images/alert-filled"
                text: "The message could not be verified."
            }
        }
        DesignSystemState {
            width: 380
            label: "tintColor: blue (default)"
            ToastBanner {
                Layout.fillWidth: true
                shown: root.toastBannersShown
                iconSource: "image://images/info-filled"
                text: "Commands run against the live node."
            }
        }
        DesignSystemState {
            width: 380
            label: "actionText, showsCloseButton"
            ToastBanner {
                Layout.fillWidth: true
                shown: root.toastBannersShown
                tintColor: Theme.color.red
                iconSource: "image://images/alert-filled"
                text: "Connection lost."
                actionText: "Retry"
                showsCloseButton: true
            }
        }
        DesignSystemState {
            label: "replay"
            NeutralButton {
                text: "Replay"
                onClicked: {
                    root.toastBannersShown = false
                    root.toastBannersShown = true
                }
            }
        }
    }

    DesignSystemSpecimen {
        objectName: "designSystemStatusPill"
        Layout.fillWidth: true
        name: "StatusPill"

        DesignSystemState {
            label: "accentColor: orange"
            StatusPill { text: "Syncing" }
        }
        DesignSystemState {
            label: "accentColor: green"
            StatusPill { text: "Connected"; accentColor: Theme.color.green }
        }
        DesignSystemState {
            label: "accentColor: red"
            StatusPill { text: "Offline"; accentColor: Theme.color.red }
        }
        DesignSystemState {
            label: "showDot: false"
            StatusPill { text: "Watch only"; accentColor: Theme.color.blue; showDot: false }
        }
    }

    DesignSystemSpecimen {
        objectName: "designSystemNetworkIndicator"
        Layout.fillWidth: true
        name: "NetworkIndicator"
        note: "A badge on every network but main. Screen readers read it as text; it takes no focus."

        DesignSystemState {
            label: "TEST"
            NetworkIndicator { chainModelRef: QtObject { readonly property string networkName: "TEST" } }
        }
        DesignSystemState {
            label: "TESTNET4"
            NetworkIndicator { chainModelRef: QtObject { readonly property string networkName: "TESTNET4" } }
        }
        DesignSystemState {
            label: "SIGNET"
            NetworkIndicator { chainModelRef: QtObject { readonly property string networkName: "SIGNET" } }
        }
        DesignSystemState {
            label: "REGTEST, shorten: true"
            NetworkIndicator {
                textSize: 11
                shorten: true
                chainModelRef: QtObject { readonly property string networkName: "REGTEST" }
            }
        }
    }

    DesignSystemSpecimen {
        objectName: "designSystemConfirmationPill"
        Layout.fillWidth: true
        name: "ConfirmationPill"

        DesignSystemState {
            label: "statusKnown: false"
            ConfirmationPill { statusKnown: false }
        }
        DesignSystemState {
            label: "inactive: true"
            ConfirmationPill { inactive: true; confirmations: 0 }
        }
        // Positioners keep changing Repeater delegates out of the Qt 6.4 layout engine.
        Repeater {
            model: [0, 1, 2, 5, 6, 100]

            delegate: DesignSystemState {
                id: confirmationSample
                required property int modelData
                label: "confirmations: " + modelData
                ConfirmationPill { confirmations: confirmationSample.modelData }
            }
        }
    }

    DesignSystemSpecimen {
        objectName: "designSystemTooltip"
        Layout.fillWidth: true
        name: "Tooltip"

        DesignSystemState {
            label: "shown: true"
            Tooltip { text: "Peers" }
        }
        DesignSystemState {
            label: "iconSource"
            Tooltip {
                text: "Synced"
                iconSource: "image://images/check"
            }
        }
        DesignSystemState {
            label: "shown on hover"
            Item {
                implicitWidth: tooltipButton.implicitWidth
                implicitHeight: tooltipButton.implicitHeight + hoverTooltip.implicitHeight + 8

                NeutralButton {
                    id: tooltipButton
                    text: "Hover me"
                }

                Tooltip {
                    id: hoverTooltip
                    anchors.top: tooltipButton.bottom
                    anchors.topMargin: 8
                    anchors.horizontalCenter: tooltipButton.horizontalCenter
                    shown: tooltipButton.hovered
                    text: "Settings"
                }
            }
        }
    }

    DesignSystemSpecimen {
        objectName: "designSystemSkeleton"
        Layout.fillWidth: true
        name: "Skeleton"

        DesignSystemState {
            label: "loading: true"
            ColumnLayout {
                spacing: 8
                Skeleton { Layout.preferredWidth: 220; Layout.preferredHeight: 18 }
                Skeleton { Layout.preferredWidth: 160; Layout.preferredHeight: 14 }
            }
        }
        DesignSystemState {
            label: "loading: false"
            Skeleton { Layout.preferredWidth: 220; Layout.preferredHeight: 18; loading: false }
        }
    }

    DesignSystemSpecimen {
        objectName: "designSystemSpinningIndicator"
        Layout.fillWidth: true
        name: "SpinningIndicator"

        DesignSystemState {
            label: "running: true"
            SpinningIndicator { running: true }
        }
        DesignSystemState {
            label: "color: orange"
            SpinningIndicator { running: true; color: Theme.color.orange }
        }
        DesignSystemState {
            label: "running: false"
            SpinningIndicator {}
        }
    }
}
