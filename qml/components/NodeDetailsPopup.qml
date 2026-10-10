// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import "../controls"

// Opened from the block clock: what the node is doing, and what to do next.
Popup {
    id: root
    objectName: "nodeDetailsPopup"

    property NodeStatus status: NodeStatus {}
    property var trafficRef: typeof networkTrafficTower !== "undefined" ? networkTrafficTower : null
    // Rows follow every block tip, which arrive by the thousand during sync.
    // Compute them only while the popup is shown, and keep a fixed set of row
    // items so an update changes text instead of rebuilding the delegates.
    readonly property var rows: root.visible ? root.withSpeedRow(root.status.detailRows()) : []
    // "More details" stays collapsed until asked for, and its rows are only
    // computed, and its node information only read, while it is open.
    property bool moreExpanded: false
    readonly property var moreRows: root.visible && root.moreExpanded
        ? root.status.moreRows().concat(root.trafficMoreRows()) : []
    // Block sync speed, averaged over the last minute of tip heights.
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
    // Owned only when this popup switched traffic sampling on.
    property bool ownsTrafficActivity: false
    // Facts about this node that rarely change, read when "More details" opens.
    property var aboutRows: []
    onMoreExpandedChanged: if (moreExpanded) root.loadAboutRows()
    signal actionTriggered(string action)

    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    width: parent ? Math.min(440, parent.width - 48) : 440
    height: parent ? Math.min(implicitHeight, parent.height - 48) : implicitHeight
    padding: 24

    // Mempool size and traffic are sampled only while someone is looking.
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

    // Decimal units, like the network traffic settings page, with one decimal
    // below 100 so sync progress stays visible.
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

    // During block sync, one Speed row after Progress: blocks per minute and
    // the download rate that feeds them.
    function withSpeedRow(rows) {
        if (root.status.key !== "blocks") return rows
        const progress = rows.findIndex(function(row) { return row.key === qsTr("Progress") })
        if (progress < 0) return rows
        let speed = root.syncSpeedText
        if (root.trafficRef !== null) speed += " · ↓ " + root.rateText(root.trafficRef.receivedRateList)
        return rows.slice(0, progress + 1).concat([{ key: qsTr("Speed"), value: speed }], rows.slice(progress + 1))
    }

    // Traffic for "More details". The download rate is already in Speed
    // during block sync, so only the upload rate is added then.
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

    // The node information rows, relabelled, in the order this section shows
    // them. Rows covered elsewhere (heights, progress, peers, and warnings,
    // which the banner shows) are left out.
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
            if (byId[id] !== undefined) rows.push({ key: label, value: byId[id].value, url: byId[id].url, path: byId[id].path })
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

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 180; easing.type: Easing.OutCubic }
        NumberAnimation { property: "scale"; from: 0.98; to: 1; duration: 180; easing.type: Easing.OutCubic }
    }

    background: Rectangle {
        color: Theme.color.neutral1
        radius: 12
        border.width: 1
        border.color: Theme.color.neutral3
    }

    contentItem: ColumnLayout {
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.bottomMargin: 16
            CoreText {
                Layout.fillWidth: true
                text: qsTr("Node details")
                bold: true
                font.pixelSize: 18
                horizontalAlignment: Text.AlignLeft
            }
            CloseButton {
                objectName: "nodeDetailsCloseButton"
                size: 32
                onClicked: root.close()
            }
        }

        RowLayout {
            Layout.bottomMargin: 12
            spacing: 8
            Rectangle {
                Layout.alignment: Qt.AlignVCenter
                implicitWidth: 8
                implicitHeight: 8
                radius: 4
                color: root.status.tone
            }
            CoreText {
                objectName: "nodeDetailsStatus"
                text: root.status.label
                bold: true
                font.pixelSize: 14
                wrap: false
            }
            WarningsLink {
                objectName: "nodeDetailsWarnings"
                status: root.status
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
                    // An int model regenerates only when the number of rows
                    // changes, which happens at most when the popup opens.
                    model: root.rows.length
                    delegate: DetailRow {
                        width: rowsColumn.width
                        valueObjectName: "nodeDetailsValue_" + index
                        row: index < root.rows.length ? root.rows[index] : ({ key: "", value: "" })
                    }
                }

                // Text first, then a caret that points the way the section moves.
                AbstractButton {
                    id: moreToggle
                    objectName: "nodeDetailsMoreToggle"
                    topPadding: 12
                    bottomPadding: 8
                    text: root.moreExpanded ? qsTr("Fewer details") : qsTr("More details")
                    hoverEnabled: true
                    focusPolicy: Qt.StrongFocus
                    Accessible.name: text
                    implicitWidth: moreToggleContent.implicitWidth + leftPadding + rightPadding
                    implicitHeight: moreToggleContent.implicitHeight + topPadding + bottomPadding
                    opacity: down ? 0.7 : hovered ? 0.85 : 1
                    onClicked: root.moreExpanded = !root.moreExpanded

                    HoverHandler { cursorShape: Qt.PointingHandCursor }

                    contentItem: Row {
                        id: moreToggleContent
                        spacing: 4
                        CoreText {
                            anchors.verticalCenter: parent.verticalCenter
                            text: moreToggle.text
                            font: Theme.text.captionStrong.font
                            wrap: false
                            color: Theme.color.orange
                        }
                        CaretRightIcon {
                            objectName: "nodeDetailsMoreCaret"
                            anchors.verticalCenter: parent.verticalCenter
                            size: 12
                            color: Theme.color.orange
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
                        // The first row sits right under the toggle.
                        showTopLine: index > 0
                        valueObjectName: "nodeDetailsMoreValue_" + index
                        row: index < root.moreRows.length ? root.moreRows[index] : ({ key: "", value: "" })
                    }
                }

                CoreText {
                    objectName: "nodeDetailsAboutHeading"
                    visible: root.moreExpanded && root.aboutRows.length > 0
                    width: rowsColumn.width
                    topPadding: 24
                    bottomPadding: 8
                    text: qsTr("About this node")
                    bold: true
                    font.pixelSize: 14
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
            Layout.topMargin: 20
            spacing: 8
            NodeActionButton {
                objectName: "nodeDetailsPrimaryAction"
                Layout.fillWidth: true
                implicitHeight: 40
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
                implicitHeight: 40
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

    // A labelled value. Long values are elided in the middle so both ends of
    // a path stay readable. A row with a url is a link: directories open in
    // the system file manager.
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
            color: Theme.color.neutral3
        }
        CoreText {
            id: keyText
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            text: detailRow.row.key
            font.pixelSize: 14
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
            bold: true
            font.pixelSize: 14
            wrap: false
            elide: Text.ElideMiddle
            horizontalAlignment: Text.AlignRight
            color: detailRow.isLink ? Theme.color.orange : Theme.color.neutral9
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
