pragma Singleton
import QtQuick
import QtQuick.Controls

Control {
    property bool dark: true
    readonly property ColorSet color: dark ? darkColorSet : lightColorSet
    readonly property ImageSet image: dark ? darkImageSet : lightImageSet

    component ColorSet: QtObject {
        required property color white
        required property color background
        required property color orange
        required property color red
        required property color green
        required property color blue
        required property color purple
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
    }

    component ImageSet: QtObject {
        required property url blocktime
        required property url network
        required property url storage
    }

    ColorSet {
        id: darkColorSet
        white: "#FFFFFF"
        background: "black"
        orange: "#F89B2A"
        red: "#EC6363"
        green: "#36B46B"
        blue: "#3CA3DE"
        purple: "#C075DC"
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
    }

    ColorSet {
        id: lightColorSet
        white: "#FFFFFF"
        background: "white"
        orange: "#F7931A"
        red: "#EB5757"
        green: "#27AE60"
        blue: "#2D9CDB"
        purple: "#BB6BD9"
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
    }

    ImageSet {
        id: darkImageSet
        blocktime: "image://images/blocktime-dark"
        network: "image://images/network-dark"
        storage: "image://images/storage-dark"
    }

    ImageSet {
        id: lightImageSet
        blocktime: "image://images/blocktime-light"
        network: "image://images/network-light"
        storage: "image://images/storage-light"
    }

    function toggleDark() {
        dark = !dark
    }
}
