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
    // The Display setting. Reduces motion even when the platform doesn't ask to.
    property bool reduceMotion: false
    // Pickers list palettes in this order.
    readonly property list<Palette> palettes: [
        CoreLight { id: coreLight },
        CoreDark { id: coreDark }
    ]
    readonly property Palette color: activePalette
    readonly property ImageSet image: dark ? darkImageSet : lightImageSet
    property string typeSetId: "core"
    readonly property list<TypeSet> typeSets: [
        CoreTypeSet { id: coreTypeSet }
    ]
    readonly property TypeSet typeSet: typeSetFor(typeSetId)
    // The Display setting's money font; the preinit window's options model has none.
    readonly property var moneyFont: typeof optionsModel !== "undefined" && optionsModel
        && optionsModel.moneyFont !== undefined ? optionsModel.moneyFont : null
    readonly property TextSet text: TextSet {
        typeSet: root.typeSet
        moneyFamily: root.moneyFont ? root.moneyFont.family : root.typeSet.monoFamily
        moneyWeight: root.moneyFont ? root.moneyFont.weight : root.typeSet.monoWeight
    }
    readonly property MotionSet motion: MotionSet { reduceRequested: root.reduceMotion }
    readonly property SpaceSet space: SpaceSet {}
    readonly property RadiusSet radius: RadiusSet {}
    readonly property SizeSet size: SizeSet {}
    // Not `focus`: that is Item's own property.
    readonly property FocusRingSet focusRing: FocusRingSet {}

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

    function typeSetFor(id) {
        for (let i = 0; i < typeSets.length; ++i) {
            if (typeSets[i].typeSetId === id) return typeSets[i]
        }
        return coreTypeSet
    }

    onPaletteIdChanged: {
        if (!settingsLoaded) return
        activePalette = paletteFor(paletteId, activePalette.isDark)
        GlobalSettings.setValue("palette", paletteId)
    }

    onReduceMotionChanged: if (settingsLoaded) GlobalSettings.setValue("reduceMotion", reduceMotion)

    // The palette is shared by every chain; `dark` stays per chain, where older builds read it.
    Component.onCompleted: {
        paletteId = migratedPaletteId(String(GlobalSettings.value("palette", "")), settings.dark)
        activePalette = paletteFor(paletteId, settings.dark)
        reduceMotion = String(GlobalSettings.value("reduceMotion", false)) === "true"
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

    component EasingSet: QtObject {
        readonly property int enter: Easing.OutCubic
        readonly property int exit: Easing.InCubic
        readonly property int move: Easing.InOutCubic
        readonly property int loop: Easing.Linear
    }

    // Reduced motion removes movement and keeps feedback: travel and scale go,
    // fades stay (up to `normal`), color transitions and spinners are unchanged.
    // "off" is for tests only and makes everything instant.
    component MotionSet: QtObject {
        property bool reduceRequested: false
        property string mode: preferredMode(MotionPreference.mode, reduceRequested)
        readonly property bool animated: mode !== "off"
        readonly property bool reduced: mode !== "full"
        readonly property bool decorativeLoops: !reduced

        readonly property int fast: animated ? 100 : 0
        readonly property int normal: animated ? 150 : 0
        readonly property int moderate: animated ? 200 : 0
        readonly property int exit: animated ? 250 : 0
        readonly property int slow: animated ? 300 : 0
        readonly property int navigation: slow
        readonly property int tooltipDelay: animated ? 450 : 0

        readonly property EasingSet easing: EasingSet {}

        readonly property real dialogOffset: reduced ? 0 : 30
        readonly property real popoverScale: reduced ? 1 : 0.96
        readonly property real pressScale: reduced ? 1 : 0.98

        function travel(ms) {
            return reduced ? 0 : ms
        }

        function fade(ms) {
            return mode === "reduced" ? Math.min(ms, normal) : animated ? ms : 0
        }

        function delay(ms) {
            return reduced ? 0 : ms
        }

        // The platform mode, reduced when the app setting asks for it. Tests stay "off".
        function preferredMode(platformMode, reduce) {
            return reduce && platformMode === "full" ? "reduced" : platformMode
        }

        // Undoes an override of mode, so it follows the platform and app settings again.
        function resetMode() {
            mode = Qt.binding(function() { return preferredMode(MotionPreference.mode, reduceRequested) })
        }
    }

    // Spacing scale for spacing, margins and padding. Values between steps
    // (3, 7, 10, 14, 15, ...) should be snapped to the nearest step.
    component SpaceSet: QtObject {
        readonly property int xxs: 2
        readonly property int xs: 4
        readonly property int s: 6
        readonly property int m: 8
        readonly property int ml: 12
        readonly property int l: 16
        readonly property int xl: 20
        readonly property int xxl: 24
        readonly property int xxxl: 32
        readonly property int page: 40
        readonly property int pageWide: 56

        // Inset of content inside a card (FormSection rows and custom layouts).
        readonly property int cardInset: 16
        // Side padding of a page in the compact and regular width classes.
        readonly property int pageCompact: 16
        readonly property int pageRegular: page

        // Side padding of a page whose content has a maximum width
        // (settings and similar pages): compact, regular, then pageWide
        // from SizeClass.wideWidthMin on.
        function pagePadding(width) {
            if (width >= SizeClass.wideWidthMin) return pageWide
            return SizeClass.isCompact(width) ? pageCompact : pageRegular
        }
    }

    // Corner radii by surface type. Pills and circles use height / 2.
    component RadiusSet: QtObject {
        readonly property int xs: 3          // indicators, skeletons, checkbox marks
        readonly property int s: 5           // buttons and small controls
        readonly property int m: 8           // menus, tooltips, segmented picker track
        readonly property int field: 10      // text fields and inputs
        readonly property int popup: 10      // modal dialogs
        readonly property int sheet: 16      // modals hosting a multi-step flow
        readonly property int cardInset: 12  // cards nested in a card or popup
        readonly property int card: 16       // cards on a page
        readonly property int listRow: 16    // selectable rows in a list
        readonly property int sidebarRow: 8  // dense navigation rows
    }

    component IconSizeSet: QtObject {
        readonly property int xs: 12
        readonly property int s: 14
        readonly property int m: 16
        readonly property int l: 20
        readonly property int xl: 24
    }

    // Heights of controls and rows, square icon-button targets and icon artwork.
    component SizeSet: QtObject {
        readonly property int control: 36
        readonly property int controlSmall: 28
        readonly property int controlLarge: 46
        readonly property int field: 52
        readonly property int row: 48
        readonly property int rowWithDescription: 62
        readonly property int iconButton: 40
        readonly property int iconButtonSmall: 28
        readonly property IconSizeSet icon: IconSizeSet {}
        readonly property int caret: 14
        readonly property int caretDense: 12
    }

    // The purple keyboard focus ring drawn by FocusBorder around buttons and rows.
    component FocusRingSet: QtObject {
        readonly property int width: 2
        readonly property int offset: 4
        readonly property int offsetDense: 2
    }

    component TextStyle: QtObject {
        required property string family
        required property int weight
        required property int pixelSize

        // Default line height is 140% of pixelSize; each role overrides with the spec value.
        // Use with `lineHeightMode: Text.FixedHeight` on the consumer Text element.
        property int lineHeight: Math.round(pixelSize * 1.4)

        readonly property font font: Qt.font({
            family: family,
            weight: weight,
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
        required property TypeSet typeSet
        required property string moneyFamily
        required property int moneyWeight
        readonly property string family: typeSet.family
        readonly property string monoFamily: typeSet.monoFamily
        readonly property int regularWeight: typeSet.regularWeight
        readonly property int strongWeight: typeSet.strongWeight
        readonly property int monoWeight: typeSet.monoWeight

        // Headers — Semi Bold
        readonly property TextStyle display: TextStyle {
            family: textSetRoot.family
            weight: textSetRoot.strongWeight
            pixelSize: 36
            lineHeight: 44
        }
        readonly property TextStyle headline: TextStyle {
            family: textSetRoot.family
            weight: textSetRoot.strongWeight
            pixelSize: 28
            lineHeight: 34
        }
        readonly property TextStyle title: TextStyle {
            family: textSetRoot.family
            weight: textSetRoot.strongWeight
            pixelSize: 24
            lineHeight: 28
        }
        readonly property TextStyle subtitle: TextStyle {
            family: textSetRoot.family
            weight: textSetRoot.strongWeight
            pixelSize: 21
            lineHeight: 25
        }
        readonly property TextStyle heading: TextStyle {
            family: textSetRoot.family
            weight: textSetRoot.strongWeight
            pixelSize: 18
            lineHeight: 21
        }
        readonly property TextStyle subheading: TextStyle {
            family: textSetRoot.family
            weight: textSetRoot.strongWeight
            pixelSize: 15
            lineHeight: 18
        }

        // Body — Regular
        readonly property TextStyle lead: TextStyle {
            family: textSetRoot.family
            weight: textSetRoot.regularWeight
            pixelSize: 24
            lineHeight: 36
        }
        readonly property TextStyle bodyLarge: TextStyle {
            family: textSetRoot.family
            weight: textSetRoot.regularWeight
            pixelSize: 21
            lineHeight: 31
        }
        readonly property TextStyle body: TextStyle {
            family: textSetRoot.family
            weight: textSetRoot.regularWeight
            pixelSize: 18
            lineHeight: 27
        }
        readonly property TextStyle description: TextStyle {
            family: textSetRoot.family
            weight: textSetRoot.regularWeight
            pixelSize: 15
            lineHeight: 22
        }
        readonly property TextStyle caption: TextStyle {
            family: textSetRoot.family
            weight: textSetRoot.regularWeight
            pixelSize: 13
            lineHeight: 19
        }
        readonly property TextStyle captionStrong: TextStyle {
            family: textSetRoot.family
            weight: textSetRoot.strongWeight
            pixelSize: 13
            lineHeight: 19
        }

        // Controls
        readonly property TextStyle button: TextStyle {
            family: textSetRoot.family
            weight: textSetRoot.regularWeight
            pixelSize: 18
            lineHeight: 22
        }
        readonly property TextStyle buttonStrong: TextStyle {
            family: textSetRoot.family
            weight: textSetRoot.strongWeight
            pixelSize: 18
            lineHeight: 22
        }

        // Menu
        readonly property TextStyle menuItem: TextStyle {
            family: textSetRoot.family
            weight: textSetRoot.regularWeight
            pixelSize: 15
            lineHeight: 22
        }
        readonly property TextStyle menuItemStrong: TextStyle {
            family: textSetRoot.family
            weight: textSetRoot.strongWeight
            pixelSize: 15
            lineHeight: 22
        }

        // Mono: technical values (addresses, txids, paths, sizes)
        readonly property TextStyle monoLead: TextStyle {
            family: textSetRoot.monoFamily
            weight: textSetRoot.monoWeight
            pixelSize: 24
            lineHeight: 36
        }
        readonly property TextStyle monoBody: TextStyle {
            family: textSetRoot.monoFamily
            weight: textSetRoot.monoWeight
            pixelSize: 18
            lineHeight: 27
        }
        readonly property TextStyle monoDescription: TextStyle {
            family: textSetRoot.monoFamily
            weight: textSetRoot.monoWeight
            pixelSize: 15
            lineHeight: 22
        }
        readonly property TextStyle monoCaption: TextStyle {
            family: textSetRoot.monoFamily
            weight: textSetRoot.monoWeight
            pixelSize: 13
            lineHeight: 19
        }

        // Amounts: the Display setting's money font
        readonly property TextStyle amountLead: TextStyle {
            family: textSetRoot.moneyFamily
            weight: textSetRoot.moneyWeight
            pixelSize: 24
            lineHeight: 36
        }
        readonly property TextStyle amountBody: TextStyle {
            family: textSetRoot.moneyFamily
            weight: textSetRoot.moneyWeight
            pixelSize: 18
            lineHeight: 27
        }
        readonly property TextStyle amountDescription: TextStyle {
            family: textSetRoot.moneyFamily
            weight: textSetRoot.moneyWeight
            pixelSize: 15
            lineHeight: 22
        }
        readonly property TextStyle amountCaption: TextStyle {
            family: textSetRoot.moneyFamily
            weight: textSetRoot.moneyWeight
            pixelSize: 13
            lineHeight: 19
        }
    }
}
