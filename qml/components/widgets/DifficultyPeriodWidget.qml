// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Layouts 1.15
import "../../controls"

DashboardWidget {
    id: root
    objectName: "difficultyPeriodWidget"
    property var periodModel: typeof difficultyPeriodModel !== "undefined" ? difficultyPeriodModel : null
    readonly property bool available: periodModel !== null && periodModel.ready && periodModel.available
    readonly property real progress: available ? Math.max(0, Math.min(1, periodModel.progress)) : 0
    readonly property int progressPercent: Math.floor(progress * 100)
    readonly property real nextChange: available ? periodModel.nextChange : NaN
    readonly property real previousChange: available ? periodModel.previousChange : NaN
    readonly property font progressCaptionFont: {
        const value = root.scaledFont(Theme.text.caption)
        if (!root.expanded) value.pixelSize = Math.round(value.pixelSize * 0.85)
        return value
    }
    Accessible.description: qsTr("Difficulty period progress, estimated next adjustment, previous change, blocks left, estimated time remaining and average block time.")

    Binding { target: root.periodModel; property: "active"; value: root.active; when: root.periodModel !== null }
    Component.onDestruction: if (periodModel) periodModel.active = false

    function number(value, decimals) { return Number(value).toLocaleString(Qt.locale(), 'f', decimals) }
    function changeText(value) {
        if (!isFinite(value)) return "—"
        const rounded = Math.round(value * 100) / 100
        return qsTr("%1%2%").arg(rounded > 0 ? "+" : rounded < 0 ? "−" : "").arg(number(Math.abs(rounded), 2))
    }
    function changeColor(value) {
        if (!isFinite(value) || Math.abs(value) < 0.005) return Theme.color.neutral9
        return value < 0 ? Theme.color.red : Theme.color.green
    }
    function averageText() {
        if (!available || !isFinite(periodModel.averageBlockSeconds)) return "—"
        const seconds = Math.floor(periodModel.averageBlockSeconds)
        return qsTr("%1m %2s").arg(number(Math.floor(seconds / 60), 0)).arg(number(seconds % 60, 0))
    }

    function remainingText() {
        if (!available || !isFinite(periodModel.averageBlockSeconds)) return "—"
        const minutes = Math.max(1, Math.ceil(periodModel.blocksLeft * periodModel.averageBlockSeconds / 60))
        const days = Math.floor(minutes / 1440)
        const hours = Math.floor(minutes / 60) % 24
        if (days > 0) return qsTr("In ≈%1d %2h").arg(number(days, 0)).arg(number(hours, 0))
        if (minutes >= 60) return qsTr("In ≈%1h %2m").arg(number(hours, 0)).arg(number(minutes % 60, 0))
        return qsTr("In ≈%1m").arg(number(minutes, 0))
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: root.expanded ? (root.height < 240 ? 8 : 14) : 6
        CoreText {
            Layout.fillWidth: true
            text: qsTr("Difficulty Period")
            font: root.compact ? root.scaledFont(Theme.text.captionStrong) : root.scaledFont(Theme.text.subheading)
            color: Theme.color.neutral7
            horizontalAlignment: Text.AlignLeft
            wrap: false
            elide: Text.ElideRight
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            ColumnLayout {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.preferredWidth: 2
                spacing: 2
                CoreText {
                    objectName: "difficultyNextChange"
                    text: root.changeText(root.nextChange)
                    font: {
                        const value = root.scaledFont(root.expanded ? Theme.text.widgetDisplay : Theme.text.headline)
                        if (root.expanded) value.pixelSize = Math.round(value.pixelSize * 1.25)
                        return value
                    }
                    color: root.changeColor(root.nextChange)
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignLeft
                    wrap: false
                    fontSizeMode: Text.Fit
                    minimumPixelSize: root.scaledPixelSize(Theme.text.heading)
                }
                CoreText {
                    objectName: "difficultyNextLabel"
                    visible: root.compact || root.expanded
                    text: qsTr("Next adjustment")
                    font: root.scaledFont(Theme.text.caption)
                    color: Theme.color.neutral6
                    horizontalAlignment: Text.AlignLeft
                    wrap: false
                }
            }
            CoreText { visible: !root.compact && !root.expanded; text: qsTr("Next\nadjustment"); font: root.scaledFont(Theme.text.caption); color: Theme.color.neutral6; horizontalAlignment: Text.AlignRight }
            ColumnLayout {
                visible: root.expanded
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.preferredWidth: 1
                Layout.alignment: Qt.AlignBottom
                spacing: 2
                CoreText {
                    objectName: "difficultyPreviousChange"
                    Layout.fillWidth: true
                    text: root.changeText(root.previousChange)
                    font: {
                        const value = root.scaledFont(Theme.text.captionStrong)
                        value.pixelSize *= 2
                        return value
                    }
                    color: root.changeColor(root.previousChange)
                    horizontalAlignment: Text.AlignRight
                    wrap: false
                    fontSizeMode: Text.Fit
                    minimumPixelSize: root.scaledPixelSize(Theme.text.captionStrong)
                }
                CoreText {
                    objectName: "difficultyPreviousLabel"
                    Layout.fillWidth: true
                    text: qsTr("Previous")
                    font: root.scaledFont(Theme.text.caption)
                    color: Theme.color.neutral6
                    horizontalAlignment: Text.AlignRight
                    wrap: false
                }
            }
        }
        Item { visible: root.expanded; Layout.fillHeight: true }
        WidgetSection {
            objectName: "difficultyProgressSection"
            Layout.fillWidth: true
            Layout.preferredHeight: implicitHeight * (root.expanded ? 1.2 : 1)
            padding: root.compact ? 5 : root.expanded ? 12 : 8
            contentItem: ColumnLayout {
                spacing: root.expanded ? 10 : 5
                RowLayout {
                    Layout.fillWidth: true
                    CoreText {
                        objectName: "difficultyBlocksLeft"
                        visible: !root.compact
                        Layout.fillWidth: true
                        text: root.available ? qsTr("%1 blocks left").arg(root.number(root.periodModel.blocksLeft, 0)) : "—"
                        font: root.expanded ? root.scaledFont(Theme.text.subheading) : root.progressCaptionFont
                        horizontalAlignment: Text.AlignLeft
                        wrap: false
                        elide: Text.ElideRight
                    }
                    CoreText {
                        objectName: "difficultyProgress"
                        visible: !root.expanded
                        text: root.available ? qsTr("%1%").arg(root.number(root.progressPercent, 0)) : "—"
                        font: root.progressCaptionFont
                        color: Theme.color.neutral7
                    }
                    CoreText {
                        objectName: "difficultyCompactBlocksLeft"
                        visible: root.compact
                        Layout.fillWidth: true
                        text: root.available ? qsTr("%1 left").arg(root.periodModel.blocksLeft) : "—"
                        font: root.progressCaptionFont
                        color: Theme.color.neutral7
                        horizontalAlignment: Text.AlignRight
                        wrap: false
                        elide: Text.ElideRight
                    }
                    CoreText {
                        objectName: "difficultyTimeRemaining"
                        visible: !root.compact
                        text: root.remainingText()
                        font: root.progressCaptionFont
                        color: Theme.color.neutral7
                        horizontalAlignment: Text.AlignRight
                        wrap: false
                        elide: Text.ElideRight
                    }
                }
                Rectangle {
                    objectName: "difficultyProgressBar"
                    Layout.fillWidth: true
                    Layout.preferredHeight: (root.expanded ? 20 : 4) * root.fontScale
                    radius: height / 2
                    color: Theme.color.neutral2
                    clip: true
                    Rectangle { width: parent.width * root.progress; height: parent.height; radius: parent.radius; color: Theme.color.blue }
                    CoreText { anchors.centerIn: parent; visible: root.expanded; text: root.available ? qsTr("%1%").arg(root.progressPercent) : "—"; font: root.scaledFont(Theme.text.captionStrong); color: Theme.color.neutral9 }
                }
                RowLayout {
                    visible: root.expanded
                    Layout.fillWidth: true
                    CoreText { objectName: "difficultyAverageLabel"; text: qsTr("Avg. block time"); font: root.scaledFont(Theme.text.caption); color: Theme.color.neutral6 }
                    CoreText { objectName: "difficultyAverageBlockTime"; Layout.fillWidth: true; text: root.averageText(); font: root.scaledFont(Theme.text.captionStrong); horizontalAlignment: Text.AlignRight; wrap: false }
                }
            }
        }
    }
}
