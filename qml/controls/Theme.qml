pragma Singleton
import QtQuick 2.15
import QtQuick.Controls 2.15

Control {
    id: root
    property bool dark: true
    property real blockclocksize: (5 / 12)
    readonly property ColorSet color: dark ? darkColorSet : lightColorSet
    readonly property ImageSet image: dark ? darkImageSet : lightImageSet
    readonly property TextSet text: TextSet {}

    AppSettings {
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

    component TextStyle: QtObject {
        required property string family
        required property string styleName
        required property int pixelSize

        // Default line height is 140% of pixelSize; each role overrides with the spec value.
        // Use with `lineHeightMode: Text.FixedHeight` on the consumer Text element.
        property int lineHeight: Math.round(pixelSize * 1.4)

        readonly property font font: Qt.font({
            family: family,
            styleName: styleName,
            pixelSize: pixelSize
        })
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
        neutral0: "#000000"
        neutral1: "#121212"
        neutral2: "#222222"
        neutral3: "#383838"
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
        neutral0: "#FFFFFF"
        neutral1: "#F6F6F6"
        neutral2: "#EEEEEE"
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

    component TextSet: QtObject {
        id: textSetRoot
        readonly property string family: "BitcoinCoreSans"
        readonly property string monoFamily: "Roboto Mono"

        // Headers — Semi Bold
        readonly property TextStyle display: TextStyle {
            family: textSetRoot.family
            styleName: "Semi Bold"
            pixelSize: 36
            lineHeight: 44
        }
        readonly property TextStyle headline: TextStyle {
            family: textSetRoot.family
            styleName: "Semi Bold"
            pixelSize: 28
            lineHeight: 34
        }
        readonly property TextStyle title: TextStyle {
            family: textSetRoot.family
            styleName: "Semi Bold"
            pixelSize: 24
            lineHeight: 28
        }
        readonly property TextStyle subtitle: TextStyle {
            family: textSetRoot.family
            styleName: "Semi Bold"
            pixelSize: 21
            lineHeight: 25
        }
        readonly property TextStyle heading: TextStyle {
            family: textSetRoot.family
            styleName: "Semi Bold"
            pixelSize: 18
            lineHeight: 21
        }
        readonly property TextStyle subheading: TextStyle {
            family: textSetRoot.family
            styleName: "Semi Bold"
            pixelSize: 15
            lineHeight: 18
        }

        // Body — Regular
        readonly property TextStyle lead: TextStyle {
            family: textSetRoot.family
            styleName: "Regular"
            pixelSize: 24
            lineHeight: 36
        }
        readonly property TextStyle bodyLarge: TextStyle {
            family: textSetRoot.family
            styleName: "Regular"
            pixelSize: 21
            lineHeight: 31
        }
        readonly property TextStyle body: TextStyle {
            family: textSetRoot.family
            styleName: "Regular"
            pixelSize: 18
            lineHeight: 27
        }
        readonly property TextStyle description: TextStyle {
            family: textSetRoot.family
            styleName: "Regular"
            pixelSize: 15
            lineHeight: 22
        }
        readonly property TextStyle caption: TextStyle {
            family: textSetRoot.family
            styleName: "Regular"
            pixelSize: 13
            lineHeight: 19
        }

        // Controls
        readonly property TextStyle button: TextStyle {
            family: textSetRoot.family
            styleName: "Regular"
            pixelSize: 18
            lineHeight: 22
        }
        readonly property TextStyle buttonStrong: TextStyle {
            family: textSetRoot.family
            styleName: "Semi Bold"
            pixelSize: 18
            lineHeight: 22
        }

        // Menu
        readonly property TextStyle menuItem: TextStyle {
            family: textSetRoot.family
            styleName: "Regular"
            pixelSize: 15
            lineHeight: 22
        }

        // Mono — Roboto Mono Regular
        readonly property TextStyle monoLead: TextStyle {
            family: textSetRoot.monoFamily
            styleName: "Regular"
            pixelSize: 24
            lineHeight: 36
        }
        readonly property TextStyle monoBody: TextStyle {
            family: textSetRoot.monoFamily
            styleName: "Regular"
            pixelSize: 18
            lineHeight: 27
        }
        readonly property TextStyle monoDescription: TextStyle {
            family: textSetRoot.monoFamily
            styleName: "Regular"
            pixelSize: 15
            lineHeight: 22
        }
        readonly property TextStyle monoCaption: TextStyle {
            family: textSetRoot.monoFamily
            styleName: "Regular"
            pixelSize: 13
            lineHeight: 19
        }
    }

    function toggleDark() {
        dark = !dark
    }
}
