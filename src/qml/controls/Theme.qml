pragma Singleton
import QtQuick 2.15
import QtQuick.Controls 2.15
import Qt.labs.settings 1.0

Control {
    id: root
    property bool dark: true
    property real blockclocksize: (5/12)
    readonly property ColorSet color: dark ? darkColorSet : lightColorSet
    readonly property ImageSet image: dark ? darkImageSet : lightImageSet

    Settings {
        id: settings
        property alias dark: root.dark
        property alias blockclocksize: root.blockclocksize
    }

    component ColorSet: QtObject {
        required property color white
        required property color background
        required property color orange
        required property color orangeLight1
        required property color orangeLight2
        required property color red
        required property color green
        required property color blue
        required property color amber
        required property color purple
        required property color transparent
        required property color neutral0
        required property color neutral1
        required property color neutral2
        required property color neutral3
        required property color neutral4
        required property color neutral5
        required property color neutral6
        required property color neutral7
        required property color neutral8
        required property color neutral9
        required property var confirmationColors
    }

    component ImageSet: QtObject {
        required property url blocktime
        required property url network
        required property url storage
        required property url tooltipArrow
    }

    ColorSet {
        id: darkColorSet
        white: "#FFFFFF"
        background: "black"
        orange: "#F89B2A"
        orangeLight1: "#FFAD4A"
        orangeLight2: "#FFBF72"
        red: "#EC6363"
        green: "#36B46B"
        blue: "#3CA3DE"
        amber: "#C9B500"
        purple: "#C075DC"
        transparent: "#00000000"
        neutral0: "#000000"
        neutral1: "#1A1A1A"
        neutral2: "#2D2D2D"
        neutral3: "#444444"
        neutral4: "#5C5C5C"
        neutral5: "#787878"
        neutral6: "#949494"
        neutral7: "#B0B0B0"
        neutral8: "#CCCCCC"
        neutral9: "#FFFFFF"
        confirmationColors: [
            "#FF1C1C", // red
            "#ED6E46",
            "#EE8847",
            "#EFA148",
            "#F0BB49",
            "#F1D54A", // yellow
        ]
    }

    ColorSet {
        id: lightColorSet
        white: "#FFFFFF"
        background: "white"
        orange: "#F7931A"
        orangeLight1: "#FFAD4A"
        orangeLight2: "#FFBF72"
        red: "#EB5757"
        green: "#27AE60"
        blue: "#2D9CDB"
        amber: "#C9B500"
        purple: "#BB6BD9"
        transparent: "#00000000"
        neutral0: "#FFFFFF"
        neutral1: "#F8F8F8"
        neutral2: "#F4F4F4"
        neutral3: "#EDEDED"
        neutral4: "#DEDEDE"
        neutral5: "#BBBBBB"
        neutral6: "#999999"
        neutral7: "#777777"
        neutral8: "#404040"
        neutral9: "#000000"
        confirmationColors: [
            "#FF1C1C", // red
            "#ED6E46",
            "#EE8847",
            "#EFA148",
            "#F0BB49",
            "#F1D54A", // yellow
        ]
    }

    ImageSet {
        id: darkImageSet
        blocktime: "image://images/blocktime-dark"
        network: "image://images/network-dark"
        storage: "image://images/storage-dark"
        tooltipArrow: "qrc:/icons/tooltip-arrow-dark"
    }

    ImageSet {
        id: lightImageSet
        blocktime: "image://images/blocktime-light"
        network: "image://images/network-light"
        storage: "image://images/storage-light"
        tooltipArrow: "qrc:/icons/tooltip-arrow-light"
    }

    function toggleDark() {
        dark = !dark
    }
}
