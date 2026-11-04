// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import org.bitcoincore.qt 1.0

import "../controls"

DialogPopup {
    id: root
    objectName: "nodeDetailsPopup"

    property NodeStatus status: NodeStatus {}
    property var trafficRef: typeof networkTrafficTower !== "undefined" ? networkTrafficTower : null
    // Rows change with every tip, so compute them only while shown.
    readonly property var rows: root.visible ? root.withSpeedRow(root.status.detailRows()) : []
    property bool moreExpanded: false
    readonly property var moreRows: root.visible && root.moreExpanded
        ? root.status.moreRows().concat(root.trafficMoreRows()) : []
    property var syncSamples: []
    readonly property string syncSpeedText: {
        const samples = root.syncSamples
        if (samples.length < 2) return qsTr("Measuring…")
        const first = samples[0]
        const last = samples[samples.length - 1]
        const minutes = (last.time - first.time) / 60000
        if (minutes < 5 / 60) return qsTr("Measuring…")
        const rate = (last.height - first.height) / minutes
        return qsTr("~%1 blocks/min").arg(rate >= 10 ? Math.round(rate) : rate.toFixed(1))
    }
    // True when this popup turned traffic sampling on.
    property bool ownsTrafficActivity: false
    property var aboutRows: []
    onMoreExpandedChanged: if (moreExpanded) root.loadAboutRows()
    signal actionTriggered(string action)

    title: qsTr("Node details")
    implicitWidth: 480

    onOpened: {
        if (root.status.nodeModelRef !== null && root.status.nodeModelRef.mempoolInfoPollingActive !== undefined) root.status.nodeModelRef.mempoolInfoPollingActive = true
        if (root.trafficRef !== null && !root.trafficRef.active) {
            root.trafficRef.active = true
            root.ownsTrafficActivity = true
        }
    }
    onClosed: {
        root.moreExpanded = false
        root.aboutRows = []
        if (root.status.nodeModelRef !== null && root.status.nodeModelRef.mempoolInfoPollingActive !== undefined) root.status.nodeModelRef.mempoolInfoPollingActive = false
        root.releaseTraffic()
    }
    Component.onDestruction: root.releaseTraffic()

    function releaseTraffic() {
        if (root.ownsTrafficActivity && root.trafficRef !== null) root.trafficRef.active = false
        root.ownsTrafficActivity = false
    }

    // Decimal units as on the network traffic page, one decimal below 100.
    function formatBytes(bytes) {
        const suffixes = ["B", "KB", "MB", "GB", "TB", "PB"]
        let index = 0
        while (bytes >= 1000 && index < suffixes.length - 1) {
            bytes /= 1000
            index++
        }
        return (index > 0 && bytes < 100 ? bytes.toFixed(1) : bytes.toFixed(0)) + " " + suffixes[index]
    }

    function latestRate(list) {
        return list !== undefined && list.length > 0 ? list[0] : 0
    }

    function rateText(list) {
        return qsTr("%1/s").arg(root.formatBytes(root.latestRate(list)))
    }

    function withSpeedRow(rows) {
        if (root.status.key !== "blocks") return rows
        const progress = rows.findIndex(function(row) { return row.key === qsTr("Progress") })
        if (progress < 0) return rows
        let speed = root.syncSpeedText
        if (root.trafficRef !== null) speed += " · ↓ " + root.rateText(root.trafficRef.receivedRateList)
        return rows.slice(0, progress + 1).concat([{ key: qsTr("Speed"), value: speed }], rows.slice(progress + 1))
    }

    // During block sync, Speed already shows the download rate.
    function trafficMoreRows() {
        if (root.trafficRef === null) return []
        const rows = [{ key: qsTr("Downloaded"), value: root.formatBytes(root.trafficRef.totalBytesReceived) }]
        if (root.status.key === "blocks") {
            rows.push({ key: qsTr("Upload speed"), value: "↑ " + root.rateText(root.trafficRef.sentRateList) })
        } else {
            rows.push({ key: qsTr("Network speed"), value: "↓ " + root.rateText(root.trafficRef.receivedRateList)
                + " · ↑ " + root.rateText(root.trafficRef.sentRateList) })
        }
        return rows
    }

    // Heights, progress, peers and warnings are shown elsewhere.
    readonly property var aboutLabels: [
        ["clientVersion", qsTr("Version")],
        ["userAgent", qsTr("User agent")],
        ["startupTime", qsTr("Running since")],
        ["dataDir", qsTr("Data directory")],
        ["blocksDir", qsTr("Blocks directory")],
        ["localAddresses", qsTr("Local addresses")]
    ]

    function loadAboutRows() {
        const node = root.status.nodeModelRef
        if (node === null || node.nodeInformationRows === undefined) {
            root.aboutRows = []
            return
        }
        const byId = {}
        for (const row of node.nodeInformationRows()) byId[row.id] = row
        const rows = []
        for (const [id, label] of root.aboutLabels) {
            if (byId[id] !== undefined) rows.push({ key: label, value: byId[id].value, url: byId[id].url, path: byId[id].path, mono: byId[id].mono })
        }
        root.aboutRows = rows
    }

    function sampleSyncSpeed() {
        if (root.status.nodeModelRef === null) return
        const now = Date.now()
        const samples = root.syncSamples.filter(function(sample) { return now - sample.time <= 60000 })
        samples.push({ time: now, height: root.status.nodeModelRef.blockTipHeight })
        root.syncSamples = samples
    }

    Timer {
        id: syncSpeedTimer
        interval: 2000
        repeat: true
        triggeredOnStart: true
        running: root.visible && root.status.key === "blocks"
        onRunningChanged: if (!running) root.syncSamples = []
        onTriggered: root.sampleSyncSpeed()
    }

    contentItem: ColumnLayout {
        spacing: 0

        DialogHeader {
            Layout.fillWidth: true
            Layout.bottomMargin: Theme.space.l
            title: root.title
            closeButtonObjectName: "nodeDetailsCloseButton"
            onCloseRequested: root.close()
        }

        RowLayout {
            Layout.bottomMargin: Theme.space.ml
            spacing: Theme.space.m
            Rectangle {
                Layout.alignment: Qt.AlignVCenter
                implicitWidth: Theme.space.m
                implicitHeight: Theme.space.m
                radius: height / 2
                color: root.status.tone
            }
            CoreText {
                objectName: "nodeDetailsStatus"
                text: root.status.label
                textStyle: Theme.text.captionStrong
                wrap: false
            }
            WarningsLink {
                objectName: "nodeDetailsWarnings"
                status: root.status
                showDismissed: true
                onClicked: {
                    root.close()
                    root.actionTriggered("warnings")
                }
            }
        }

        ScrollView {
            id: rowsScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: rowsColumn.implicitHeight
            contentWidth: availableWidth
            clip: true

            // A Column positioner, never a Quick Layout: a Repeater inside one
            // crashes on Qt 6.4.0 to 6.5.0 (QTBUG-111792). See AlertPopup.qml.
            Column {
                id: rowsColumn
                width: rowsScroll.availableWidth
                Repeater {
                    id: rowRepeater
                    objectName: "nodeDetailsRows"
                    // An int model keeps the delegates across tip updates.
                    model: root.rows.length
                    delegate: DetailRow {
                        width: rowsColumn.width
                        valueObjectName: "nodeDetailsValue_" + index
                        row: index < root.rows.length ? root.rows[index] : ({ key: "", value: "" })
                    }
                }

                AbstractButton {
                    id: moreToggle
                    objectName: "nodeDetailsMoreToggle"
                    topPadding: Theme.space.ml
                    bottomPadding: Theme.space.m
                    text: root.moreExpanded ? qsTr("Fewer details") : qsTr("More details")
                    hoverEnabled: AppMode.isDesktop
                    focusPolicy: Qt.StrongFocus
                    Accessible.name: text
                    implicitWidth: moreToggleContent.implicitWidth + leftPadding + rightPadding
                    implicitHeight: moreToggleContent.implicitHeight + topPadding + bottomPadding
                    opacity: down ? 0.7 : hovered ? 0.85 : 1
                    onClicked: root.moreExpanded = !root.moreExpanded

                    HoverHandler { cursorShape: Qt.PointingHandCursor }

                    contentItem: Row {
                        id: moreToggleContent
                        spacing: Theme.space.xs
                        CoreText {
                            anchors.verticalCenter: parent.verticalCenter
                            text: moreToggle.text
                            textStyle: Theme.text.captionStrong
                            wrap: false
                            color: Theme.color.accentText
                        }
                        CaretRightIcon {
                            objectName: "nodeDetailsMoreCaret"
                            anchors.verticalCenter: parent.verticalCenter
                            size: Theme.size.caretDense
                            color: Theme.color.accentText
                            rotation: root.moreExpanded ? -90 : 90
                        }
                    }

                    background: FocusBorder {
                        visible: moreToggle.visualFocus
                        borderRadius: 6
                    }
                }

                Repeater {
                    objectName: "nodeDetailsMoreRows"
                    model: root.moreRows.length
                    delegate: DetailRow {
                        width: rowsColumn.width
                        showTopLine: index > 0
                        valueObjectName: "nodeDetailsMoreValue_" + index
                        row: index < root.moreRows.length ? root.moreRows[index] : ({ key: "", value: "" })
                    }
                }

                CoreText {
                    objectName: "nodeDetailsAboutHeading"
                    visible: root.moreExpanded && root.aboutRows.length > 0
                    width: rowsColumn.width
                    topPadding: Theme.space.xxl
                    bottomPadding: Theme.space.m
                    text: qsTr("About this node")
                    textStyle: Theme.text.subheading
                    horizontalAlignment: Text.AlignLeft
                }
                Repeater {
                    objectName: "nodeDetailsAboutRows"
                    model: root.moreExpanded ? root.aboutRows : []
                    delegate: DetailRow {
                        width: rowsColumn.width
                        valueObjectName: "nodeDetailsAboutValue_" + index
                        row: modelData
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: Theme.space.xl
            spacing: Theme.space.m
            NodeActionButton {
                objectName: "nodeDetailsPrimaryAction"
                Layout.fillWidth: true
                text: root.status.primaryActionText
                accent: root.status.primaryActionAccent
                enabled: root.status.primaryActionEnabled
                busy: root.status.primaryActionIsNetwork && root.status.networkActionPending
                onClicked: {
                    const action = root.status.primaryAction
                    root.close()
                    root.actionTriggered(action)
                }
            }
            NodeActionButton {
                objectName: "nodeDetailsSecondaryAction"
                Layout.fillWidth: true
                visible: root.status.secondaryAction.length > 0
                text: root.status.secondaryActionText
                onClicked: {
                    const action = root.status.secondaryAction
                    root.close()
                    root.actionTriggered(action)
                }
            }
        }
    }

    component DetailRow: Item {
        id: detailRow
        property var row: ({ key: "", value: "" })
        property string valueObjectName: ""
        property bool showTopLine: true
        readonly property bool isLink: row.url !== undefined && row.url.toString().length > 0
        implicitHeight: 44

        Rectangle {
            visible: detailRow.showTopLine
            width: parent.width
            height: 1
            color: Theme.color.surfaceDivider
        }
        CoreText {
            id: keyText
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            text: detailRow.row.key
            textStyle: Theme.text.caption
            wrap: false
            color: Theme.color.neutral7
        }
        CoreText {
            id: valueText
            objectName: detailRow.valueObjectName
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(implicitWidth, detailRow.width - keyText.implicitWidth - 16)
            text: detailRow.row.value
            textStyle: detailRow.row.mono === true ? Theme.text.monoCaption : Theme.text.captionStrong
            wrap: false
            elide: Text.ElideMiddle
            horizontalAlignment: Text.AlignRight
            color: detailRow.isLink ? Theme.color.accentText : Theme.color.neutral9
            opacity: linkArea.pressed ? 0.7 : linkArea.containsMouse ? 0.85 : 1

            MouseArea {
                id: linkArea
                objectName: detailRow.valueObjectName.length > 0 ? detailRow.valueObjectName + "_link" : ""
                anchors.fill: parent
                enabled: detailRow.isLink
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                Accessible.role: Accessible.Link
                Accessible.name: detailRow.row.key + ": " + detailRow.row.value
                Accessible.description: qsTr("Opens the folder in your file manager")
                onClicked: Qt.openUrlExternally(detailRow.row.url)
            }
        }
    }
}
