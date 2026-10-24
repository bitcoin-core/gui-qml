pragma Singleton
import QtQuick 2.15
import QtQuick.Controls 2.15

import org.bitcoincore.qt 1.0

import "../themes"

Control {
    id: root
    property string paletteId: "core-dark"
    readonly property bool dark: color.isDark
    property real blockclocksize: (5 / 12)
    readonly property list<Palette> palettes: [
        CoreDark { id: coreDark },
        CoreLight { id: coreLight }
    ]
    readonly property Palette color: activePalette
    readonly property ImageSet image: dark ? darkImageSet : lightImageSet
    readonly property TextSet text: TextSet {}

    property Palette activePalette: coreDark
    property bool settingsLoaded: false

    AppSettings {
        id: settings
        // Still written so that builds without palettes open in the same brightness.
        property bool dark: true
        property alias blockclocksize: root.blockclocksize
    }

    // Builds before palettes only stored `dark`, which defaults to true.
    function migratedPaletteId(storedId, storedDark) {
        if (storedId !== undefined && storedId !== "") return storedId
        return storedDark === false ? coreLight.paletteId : coreDark.paletteId
    }

    // An unknown id (a removed palette, or one from a newer build) keeps the remembered brightness.
    function paletteFor(id, fallbackDark) {
        for (let i = 0; i < palettes.length; ++i) {
            if (palettes[i].paletteId === id) return palettes[i]
        }
        return fallbackDark ? coreDark : coreLight
    }

    onPaletteIdChanged: {
        if (!settingsLoaded) return
        activePalette = paletteFor(paletteId, activePalette.isDark)
        GlobalSettings.setValue("palette", paletteId)
    }

    // The palette is shared by every chain; `dark` stays per chain, where older builds read it.
    Component.onCompleted: {
        paletteId = migratedPaletteId(String(GlobalSettings.value("palette", "")), settings.dark)
        activePalette = paletteFor(paletteId, settings.dark)
        settingsLoaded = true
        GlobalSettings.setValue("palette", paletteId)
        settings.dark = Qt.binding(function() { return root.dark })
    }

    component ImageSet: QtObject {
        required property url blocktime
        required property url network
        required property url storage
        required property url tooltipArrow
        required property url onboardingClockFace
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

    ImageSet {
        id: darkImageSet
        blocktime: "image://images/blocktime-dark"
        network: "image://images/network-dark"
        storage: "image://images/storage-dark"
        tooltipArrow: "qrc:/icons/tooltip-arrow-dark"
        onboardingClockFace: "qrc:/icons/onboarding-clock-face-dark.svg"
    }

    ImageSet {
        id: lightImageSet
        blocktime: "image://images/blocktime-light"
        network: "image://images/network-light"
        storage: "image://images/storage-light"
        tooltipArrow: "qrc:/icons/tooltip-arrow-light"
        onboardingClockFace: "qrc:/icons/onboarding-clock-face-light.svg"
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
        readonly property TextStyle captionStrong: TextStyle {
            family: textSetRoot.family
            styleName: "Semi Bold"
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
        readonly property TextStyle menuItemStrong: TextStyle {
            family: textSetRoot.family
            styleName: "Semi Bold"
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
}
