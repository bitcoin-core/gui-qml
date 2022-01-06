pragma Singleton
import QtQuick 2.15

QtObject {
    property bool dark: false
    property QtObject color: QtObject {
        property color background: dark ? "black" : "white"
        property color orange: dark ? "#F89B2A" : "#F7931A"
        property color red: dark ? "#EC6363" : "#EB5757"
        property color green: dark ? "#36B46B" : "#27AE60"
        property color blue: dark ? "#3CA3DE" : "#2D9CDB"
        property color purple: dark ? "#C075DC" : "#BB6BD9"
        property color neutral0: dark ? "#FFFFFF" : "#000000"
        property color neutral1: dark ? "#F8F8F8" : "#1A1A1A"
        property color neutral2: dark ? "#F4F4F4" : "#2D2D2D"
        property color neutral3: dark ? "#EDEDED" : "#444444"
        property color neutral4: dark ? "#DEDEDE" : "#5C5C5C"
        property color neutral5: dark ? "#BBBBBB" : "#787878"
        property color neutral6: dark ? "#999999" : "#949494"
        property color neutral7: dark ? "#777777" : "#B0B0B0"
        property color neutral8: dark ? "#404040" : "#CCCCCC"
        property color neutral9: dark ? "#000000" : "#FFFFFF"
    }
    function toggleDark() {
        dark = !dark
    }
}
