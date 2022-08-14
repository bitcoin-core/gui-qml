pragma Singleton
import QtQuick

QtObject {
    property bool dark: true
    property QtObject color: QtObject {
        property color white: "#FFFFFF"
        property color background: dark ? "black" : "white"
        property color orange: dark ? "#F89B2A" : "#F7931A"
        property color red: dark ? "#EC6363" : "#EB5757"
        property color green: dark ? "#36B46B" : "#27AE60"
        property color blue: dark ? "#3CA3DE" : "#2D9CDB"
        property color purple: dark ? "#C075DC" : "#BB6BD9"
        property color neutral0: dark ? "#000000" : "#FFFFFF"
        property color neutral1: dark ? "#1A1A1A" : "#F8F8F8"
        property color neutral2: dark ? "#2D2D2D" : "#F4F4F4"
        property color neutral3: dark ? "#444444" : "#EDEDED"
        property color neutral4: dark ? "#5C5C5C" : "#DEDEDE"
        property color neutral5: dark ? "#787878" : "#BBBBBB"
        property color neutral6: dark ? "#949494" : "#999999"
        property color neutral7: dark ? "#B0B0B0" : "#777777"
        property color neutral8: dark ? "#CCCCCC" : "#404040"
        property color neutral9: dark ? "#FFFFFF" : "#000000"
    }
    property QtObject image: QtObject {
        property url blocktime: dark ? "image://images/blocktime-dark" : "image://images/blocktime-light"
        property url network: dark ? "image://images/network-dark" : "image://images/network-light"
    }
    function toggleDark() {
        dark = !dark
    }
}
