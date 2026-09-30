// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../../controls"
import "../../components"

Page {
    id: root
    objectName: "nodeOverview"
    background: Rectangle { color: Theme.color.neutral0 }
    padding: 0

    readonly property real pageSidePadding: width < 640 ? 16 : 40
    readonly property real overviewContentWidth: Math.max(0, Math.min(1100, overviewScroll.availableWidth - pageSidePadding * 2))
    readonly property bool compact: overviewContentWidth < 720
    property var informationRows: []
    readonly property var notifications: {
        const items = []
        if (nodeModel.faulted) {
            items.push({ text: nodeModel.startupError || qsTr("The node encountered an error."), icon: "error", color: Theme.color.red, priority: 100 })
        }
        // Active Core warnings take precedence over connection and sync notices.
        for (const warning of nodeModel.notificationWarnings) {
            if (!items.some(function(item) { return item.text === warning.text }))
                items.push({ text: warning.text, icon: warning.priority >= 80 ? "alert-filled" : "info-filled", color: Theme.color.amber, priority: warning.priority })
        }
        if (typeof networkStatusModel !== "undefined" && networkStatusModel.networkOffline) {
            items.push({ text: qsTr("Your node is offline. Check your network connection."), icon: "network-light", color: Theme.color.amber, priority: 60 })
        } else if (nodeModel.pause) {
            items.push({ text: qsTr("Your node is paused. Resume to connect to the network."), icon: "info-filled", color: Theme.color.neutral7, priority: 40 })
        } else if (!nodeModel.faulted && nodeModel.numPeers > 0 && nodeModel.initialSyncComplete) {
            items.push({ text: qsTr("Your node is up to date. All blocks verified."), icon: "check", color: Theme.color.green, priority: 10 })
        }
        return items.sort(function(a, b) { return b.priority - a.priority })
    }

    function refreshInformation() {
        const rows = nodeModel.nodeInformationRows()
        informationRows = ["network", "client-version", "startup-time", "last-block-time"].map(function(id) {
            return rows.find(function(row) { return row.id === id })
        }).filter(function(row) { return row !== undefined })
    }

    function openPeers() {
        const stack = root.StackView.view
        if (stack && !stack.busy && stack.depth === 1) stack.push(peersPage)
    }

    Component.onCompleted: refreshInformation()
    onVisibleChanged: {
        if (visible) refreshInformation()
        else {
            informationPopup.close()
            notificationsPopup.close()
        }
    }
    Timer {
        interval: 5000
        running: root.visible
        repeat: true
        onTriggered: root.refreshInformation()
    }
    Connections {
        target: nodeModel
        function onBlockTipHeightChanged() { if (root.visible) root.refreshInformation() }
    }

    ScrollView {
        id: overviewScroll
        objectName: "nodeOverviewScroll"
        anchors.fill: parent
        contentWidth: availableWidth
        contentHeight: overviewColumn.implicitHeight + 64
        clip: true
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

        Item {
            width: overviewScroll.availableWidth
            height: overviewColumn.implicitHeight + 64
            ColumnLayout {
                id: overviewColumn
                width: root.overviewContentWidth
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.top
                anchors.topMargin: 28
                spacing: 28

                RowLayout {
                    Layout.fillWidth: true
                    CoreText {
                        objectName: "nodeOverviewTitle"
                        Layout.fillWidth: true
                        text: qsTr("Node")
                        font: Theme.text.headline.font
                        lineHeight: Theme.text.headline.lineHeight
                        lineHeightMode: Text.FixedHeight
                        horizontalAlignment: Text.AlignLeft
                    }
                    NeutralButton {
                        objectName: "nodePauseButton"
                        text: nodeModel.pause ? qsTr("Resume node") : qsTr("Pause node")
                        enabled: !nodeModel.faulted
                        onClicked: nodeModel.pause = !nodeModel.pause
                    }
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: root.compact ? 1 : 2
                    columnSpacing: 28
                    rowSpacing: 24

                    Item {
                        id: clockPanel
                        objectName: "nodeOverviewClockPanel"
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        Layout.alignment: Qt.AlignTop
                        Layout.preferredHeight: root.compact ? Math.min(width, 420) : Math.max(width, detailsColumn.implicitHeight)
                        BlockClock {
                            anchors.centerIn: parent
                            parentWidth: clockPanel.width
                            parentHeight: Math.min(clockPanel.height, 520)
                            fillAvailableSpace: true
                            showNetworkIndicator: false
                            renderingActive: root.visible
                        }
                    }

                    ColumnLayout {
                        id: detailsColumn
                        objectName: "nodeOverviewSections"
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        Layout.alignment: Qt.AlignTop
                        spacing: 24

                        ColumnLayout {
                            objectName: "nodeNotificationsSection"
                            Layout.fillWidth: true
                            spacing: 8
                            SectionTitle {
                                objectName: "nodeNotificationsSectionTitle"
                                //: Section title on the Node overview. Opens all node notifications.
                                text: qsTr("Notifications")
                                onClicked: notificationsPopup.open()
                            }
                            ToastBanner {
                                objectName: "nodeTopNotificationBanner"
                                Layout.fillWidth: true
                                textObjectName: "nodeTopNotification"
                                text: root.notifications.length ? root.notifications[0].text : qsTr("No current notifications.")
                                iconSource: root.notifications.length ? "image://images/" + root.notifications[0].icon : "image://images/info-filled"
                                tintColor: root.notifications.length ? root.notifications[0].color : Theme.color.neutral6
                                textColor: Theme.color.neutral9
                                backgroundOpacity: 0.12
                            }
                        }

                        SectionTitle {
                            objectName: "nodePeersSection"
                            titleObjectName: "nodePeersSectionTitle"
                            //: Section title on the Node overview. The entire section opens the peer list.
                            text: qsTr("Peers")
                            onClicked: root.openPeers()
                            FormSection {
                                Layout.fillWidth: true
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 0
                                    PeerCount { objectName: "nodeConnectedPeers"; count: nodeModel.numPeers; label: qsTr("Connected") }
                                    PeerCount { objectName: "nodeOutboundPeers"; count: nodeModel.numOutboundPeers; label: qsTr("Outbound") }
                                    PeerCount { objectName: "nodeInboundPeers"; count: nodeModel.numInboundPeers; label: qsTr("Inbound") }
                                }
                            }
                        }

                        ColumnLayout {
                            objectName: "nodeInformationSection"
                            Layout.fillWidth: true
                            spacing: 8
                            SectionTitle {
                                objectName: "nodeInformationSectionTitle"
                                //: Section title on the Node overview. Opens detailed information about the node.
                                text: qsTr("Node information")
                                onClicked: informationPopup.open()
                            }
                            FormSection {
                                Layout.fillWidth: true
                                Column {
                                    Layout.fillWidth: true
                                    // Keep changing delegates outside the Quick Layout engine.
                                    Repeater {
                                        model: root.informationRows
                                        delegate: FormRow {
                                            id: informationRow
                                            required property var modelData
                                            required property int index
                                            width: parent.width
                                            implicitWidth: 0
                                            title: modelData.label
                                            titleColor: Theme.color.neutral7
                                            titleTextStyle: Theme.text.caption
                                            showDivider: index < root.informationRows.length - 1
                                            trailingItem: CoreText {
                                                Layout.maximumWidth: informationRow.width * 0.6
                                                objectName: "nodeOverviewValue_" + informationRow.modelData.id
                                                text: informationRow.modelData.value
                                                font: Theme.text.description.font
                                                horizontalAlignment: Text.AlignRight
                                                wrapMode: Text.WrapAnywhere
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    NodeInformationPopup { id: informationPopup; objectName: "nodeOverviewInformationPopup"; parent: Overlay.overlay }
    NodeNotificationsPopup { id: notificationsPopup; parent: Overlay.overlay; notifications: root.notifications }
    Component { id: peersPage; PeersView {} }

    component SectionTitle: AbstractButton {
        id: sectionButton
        default property alias body: sectionColumn.data
        property string titleObjectName: ""

        Layout.fillWidth: true
        Layout.minimumWidth: 0
        padding: 0
        implicitHeight: contentItem.implicitHeight
        implicitWidth: contentItem.implicitWidth
        hoverEnabled: enabled && AppMode.isDesktop
        focusPolicy: Qt.StrongFocus
        Accessible.name: text
        opacity: down ? 0.7 : hovered ? 0.85 : 1

        HoverHandler { cursorShape: Qt.PointingHandCursor }

        background: FocusBorder {
            visible: sectionButton.visualFocus
            borderRadius: 6
        }

        contentItem: ColumnLayout {
            id: sectionColumn
            spacing: 8

            RowLayout {
                id: titleRow
                Layout.fillWidth: true
                Layout.leftMargin: 4
                Layout.rightMargin: 4
                spacing: titleMetrics.advanceWidth(" ")

                FontMetrics {
                    id: titleMetrics
                    font: sectionLabel.font
                }

                CoreText {
                    id: sectionLabel
                    objectName: sectionButton.titleObjectName
                    Layout.minimumWidth: 0
                    Layout.maximumWidth: Math.max(0, titleRow.width - sectionChevron.size - titleRow.spacing * 2)
                    text: sectionButton.text
                    font: Theme.text.subheading.font
                    lineHeight: Theme.text.subheading.lineHeight
                    lineHeightMode: Text.FixedHeight
                    horizontalAlignment: Text.AlignLeft
                    wrap: false
                    elide: Text.ElideRight
                }
                CaretRightIcon {
                    id: sectionChevron
                    objectName: sectionButton.objectName + "Chevron"
                    size: sectionLabel.font.pixelSize
                    Layout.preferredWidth: size
                    Layout.preferredHeight: size
                    Layout.alignment: Qt.AlignVCenter
                    color: Theme.color.neutral9
                }
                Item { Layout.fillWidth: true }
            }
        }
    }
    component PeerCount: FormRow {
        property int count: 0
        property string label: ""
        Layout.fillWidth: true
        Layout.preferredWidth: 1
        implicitWidth: 0
        minimumRowHeight: 54
        title: label
        titleColor: Theme.color.neutral7
        titleTextStyle: Theme.text.caption
        showDivider: false
        bodyItem: CoreText {
            Layout.fillWidth: true
            text: count
            font: Theme.text.description.font
            horizontalAlignment: Text.AlignLeft
        }
    }
}
