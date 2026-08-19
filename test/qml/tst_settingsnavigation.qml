pragma ComponentBehavior: Bound

// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.2

import org.bitcoincore.qt 1.0

import "../../qml/components"
import "../../qml/controls"

TestCase {
    id: testCase
    name: "SettingsNavigation"
    when: windowShown
    width: 720
    height: 640

    property int firstCreated: 0
    property int firstDestroyed: 0
    property int pushedCreated: 0
    property int pushedDestroyed: 0
    property int secondCreated: 0
    property int secondDestroyed: 0

    Item {
        id: host
        anchors.fill: parent
    }

    Window {
        id: settingsWindow
        width: 900
        height: 700
        visible: true
    }

    Component {
        id: containerComponent

        SettingsPageContainer {
            width: 480
            height: 560
        }
    }

    Component {
        id: sidebarComponent

        SettingsSidebar {
            width: 190
            height: 300
            currentSectionId: "display"
            groupTitles: ({
                "wallet": "Wallet",
                "general": "General",
                "network": "Network"
            })
            model: [
                { id: "wallet", label: "Wallet", group: "wallet" },
                { id: "display", label: "Display", group: "general" },
                { id: "hidden", label: "Hidden", group: "general", visible: false },
                { id: "connection", label: "Connection", group: "network" }
            ]
        }
    }

    Component {
        id: settingsViewComponent

        SettingsView {
            width: 900
            height: 700
            selectedSectionId: "display"
        }
    }

    Component {
        id: firstPage

        Item {
            objectName: "firstSettingsPage"
            Component.onCompleted: testCase.firstCreated += 1
            Component.onDestruction: testCase.firstDestroyed += 1
        }
    }

    Component {
        id: pushedPage

        Item {
            objectName: "pushedSettingsPage"
            Component.onCompleted: testCase.pushedCreated += 1
            Component.onDestruction: testCase.pushedDestroyed += 1
        }
    }

    Component {
        id: secondPage

        Item {
            objectName: "secondSettingsPage"
            Component.onCompleted: testCase.secondCreated += 1
            Component.onDestruction: testCase.secondDestroyed += 1
        }
    }

    function init() {
        firstCreated = 0
        firstDestroyed = 0
        pushedCreated = 0
        pushedDestroyed = 0
        secondCreated = 0
        secondDestroyed = 0
        AppMode.walletEnabled = true
        AppMode.isDesktop = true
        nodeModel.mempoolInformationAvailable = true
        Theme.dark = true
        Theme.blockclocksize = 5 / 12
        optionsModel.displayUnit = 0
        optionsModel.moneyFontChoice = "embedded"
        optionsModel.maxMempoolSizeMB = 300
        optionsModel.storageSettingsDirty = false
        optionsModel.mempoolSettingsDirty = false
        nodeModel.resetMempoolInfoPollingTestState()
        optionsModel.clearCoreSettingStatusesForTest()
        const proxySetting = optionsModel.coreSettings.entry("proxy")
        const onionSetting = optionsModel.coreSettings.entry("onion")
        proxySetting.enabled = false
        proxySetting.address = proxySetting.defaultAddress()
        onionSetting.enabled = false
        onionSetting.address = onionSetting.defaultAddress()
        testNetworkTrafficTower.active = false
        testDebugLogModel.active = false
    }

    function test_sidebarFiltersGroupsAndEmitsStableSectionId() {
        const sidebar = createTemporaryObject(sidebarComponent, host)
        verify(sidebar !== null)
        compare(sidebar.visibleSections.length, 3)
        verify(findChild(sidebar, "settingsSidebar_wallet") !== null)
        verify(findChild(sidebar, "settingsSidebar_display") !== null)
        verify(findChild(sidebar, "settingsSidebar_hidden") === null)
        const walletGroup = findChild(sidebar, "settingsSidebarGroup_wallet")
        const generalGroup = findChild(sidebar, "settingsSidebarGroup_general")
        const networkGroup = findChild(sidebar, "settingsSidebarGroup_network")
        verify(walletGroup !== null)
        verify(generalGroup !== null)
        verify(networkGroup !== null)
        compare(walletGroup.text, "Wallet")
        compare(generalGroup.text, "General")
        compare(networkGroup.text, "Network")
        compare(walletGroup.font.styleName, "Semi Bold")

        let activatedSection = ""
        sidebar.sectionActivated.connect(function(sectionId) {
            activatedSection = sectionId
        })
        const connection = findChild(sidebar, "settingsSidebar_connection")
        verify(connection !== null)
        connection.clicked()
        compare(activatedSection, "connection")
    }

    function test_settingsViewAppliesRuntimeVisibilityGates() {
        AppMode.walletEnabled = false
        AppMode.isDesktop = false
        nodeModel.mempoolInformationAvailable = false

        const view = createTemporaryObject(settingsViewComponent, settingsWindow.contentItem)
        verify(view !== null)

        compare(view.sectionIsVisible("wallet"), false)
        compare(view.sectionIsVisible("external-signer"), false)
        compare(view.sectionIsVisible("window-behavior"), false)
        compare(view.sectionIsVisible("mempool"), false)
        verify(findChild(view, "settingsSidebar_wallet") === null)
        verify(findChild(view, "settingsSidebar_external-signer") === null)
        verify(findChild(view, "settingsSidebar_window-behavior") === null)
        verify(findChild(view, "settingsSidebar_mempool") === null)

        compare(view.sectionIsVisible("display"), true)
        compare(view.selectedSectionId, "display")
        verify(findChild(view, "settingsSidebar_display") !== null)
    }

    function test_containerLazilyCachesAndRestoresEachSectionStack() {
        const container = createTemporaryObject(containerComponent, host)
        verify(container !== null)

        container.showSection("first", firstPage)
        compare(container.currentSectionId, "first")
        compare(container.depth, 1)
        compare(firstCreated, 1)
        compare(firstDestroyed, 0)

        container.push(pushedPage)
        tryCompare(container, "depth", 2)
        const pushedItem = container.currentItem
        compare(firstDestroyed, 0)
        compare(pushedCreated, 1)
        compare(pushedDestroyed, 0)

        container.showSection("second", secondPage)
        compare(container.currentSectionId, "second")
        compare(container.depth, 1)
        compare(firstDestroyed, 0)
        compare(pushedDestroyed, 0)
        compare(secondCreated, 1)

        container.showSection("first", firstPage)
        compare(container.currentSectionId, "first")
        compare(container.depth, 2)
        compare(container.currentItem, pushedItem)
        compare(firstCreated, 1)
        compare(pushedCreated, 1)
        compare(secondDestroyed, 0)

        container.clear()
        compare(container.depth, 0)
        tryCompare(testCase, "firstDestroyed", 1)
        tryCompare(testCase, "pushedDestroyed", 1)
        tryCompare(testCase, "secondDestroyed", 1)
    }

    function test_selectingCurrentSectionDoesNotReloadItsStack() {
        const container = createTemporaryObject(containerComponent, host)
        verify(container !== null)

        container.showSection("first", firstPage)
        container.push(pushedPage)
        tryCompare(container, "depth", 2)

        container.showSection("first", firstPage)
        compare(container.depth, 2)
        compare(firstCreated, 1)
        compare(firstDestroyed, 0)
        compare(pushedCreated, 1)
        compare(pushedDestroyed, 0)
    }

    function test_settingsViewLazilyCachesSectionsAndIdlesExpensivePages() {
        const view = createTemporaryObject(settingsViewComponent, settingsWindow.contentItem)
        verify(view !== null)
        compare(view.visible, true)
        verify(view.componentForSection("display") !== null)
        compare(view.selectedSectionId, "display")
        tryCompare(view.pageContainer, "depth", 1)
        const displayPage = findChild(view, "settingsv2DisplaySettingsPage")
        verify(displayPage !== null)
        verify(findChild(view, "settingsv2NetworkTrafficSettingsPage") === null)
        verify(findChild(view, "settingsDebugLog") === null)
        compare(testNetworkTrafficTower.active, false)
        compare(testDebugLogModel.active, false)

        view.selectSection("network-traffic")
        tryCompare(view, "selectedSectionId", "network-traffic")
        compare(findChild(view, "settingsv2DisplaySettingsPage"), displayPage)
        const networkTrafficPage = findChild(view, "settingsv2NetworkTrafficSettingsPage")
        verify(networkTrafficPage !== null)
        tryCompare(testNetworkTrafficTower, "active", true)
        const networkTrafficHeading = findChild(view, "settingsv2NetworkTrafficHeading")
        const networkTrafficDescription = findChild(view, "settingsv2NetworkTrafficHeadingDescription")
        const networkTrafficSection = findChild(view, "settingsv2NetworkTrafficSection")
        const networkTrafficRangePicker = findChild(view, "settingsv2NetworkTrafficRangePicker")
        const networkTrafficReceivedGraph = findChild(view, "settingsv2NetworkTrafficReceivedGraph")
        const networkTrafficSentRow = findChild(view, "settingsv2NetworkTrafficSentRow")
        const networkTrafficSentGraph = findChild(view, "settingsv2NetworkTrafficSentGraph")
        verify(networkTrafficHeading !== null)
        verify(networkTrafficDescription !== null)
        verify(networkTrafficSection !== null)
        verify(networkTrafficRangePicker !== null)
        verify(networkTrafficReceivedGraph !== null)
        verify(networkTrafficSentRow !== null)
        verify(networkTrafficSentGraph !== null)
        compare(networkTrafficHeading.descriptionTextStyle.font.pixelSize,
            Theme.text.description.font.pixelSize)
        compare(networkTrafficDescription.horizontalAlignment, Text.AlignHCenter)
        compare(networkTrafficSection.backgroundColor, Theme.color.neutral1)
        compare(networkTrafficSentRow.bottomPadding, 16)
        compare(networkTrafficRangePicker.model.length, 4)
        networkTrafficRangePicker.selected(1, networkTrafficRangePicker.model[1])
        compare(testNetworkTrafficTower.lastFilterWindowSize, 360)

        view.selectSection("debug-log")
        compare(findChild(view, "settingsv2NetworkTrafficSettingsPage"), networkTrafficPage)
        tryCompare(testNetworkTrafficTower, "active", false)
        const debugLogPage = findChild(view, "settingsDebugLog")
        verify(debugLogPage !== null)
        tryCompare(testDebugLogModel, "active", true)

        view.selectSection("network-traffic")
        compare(findChild(view, "settingsv2NetworkTrafficSettingsPage"), networkTrafficPage)
        compare(findChild(view, "settingsDebugLog"), debugLogPage)
        compare(networkTrafficPage.trafficGraphScale, 3600)
        tryCompare(testNetworkTrafficTower, "active", true)
        tryCompare(testDebugLogModel, "active", false)

        view.selectSection("debug-log")
        compare(findChild(view, "settingsDebugLog"), debugLogPage)
        tryCompare(testNetworkTrafficTower, "active", false)
        tryCompare(testDebugLogModel, "active", true)

        view.selectSection("about")
        tryCompare(testDebugLogModel, "active", false)
        verify(findChild(view, "settingsv2AboutSettingsPage") !== null)
        compare(findChild(view, "settingsDebugLog"), debugLogPage)

        view.visible = false
        compare(view.pageContainer.depth, 1)
        tryCompare(testDebugLogModel, "active", false)
        tryCompare(testNetworkTrafficTower, "active", false)
        compare(findChild(view, "settingsDebugLog"), debugLogPage)
        compare(findChild(view, "settingsv2NetworkTrafficSettingsPage"), networkTrafficPage)

        view.visible = true
        compare(view.pageContainer.depth, 1)
        verify(findChild(view, "settingsv2AboutSettingsPage") !== null)
        compare(findChild(view, "settingsDebugLog"), debugLogPage)
        compare(findChild(view, "settingsv2NetworkTrafficSettingsPage"), networkTrafficPage)
        tryCompare(testDebugLogModel, "active", false)
        tryCompare(testNetworkTrafficTower, "active", false)
    }

    function test_settingsViewPreservesAddressStackWhileHidden() {
        const view = createTemporaryObject(settingsViewComponent, settingsWindow.contentItem)
        verify(view !== null)

        view.openWalletAddressHistory()
        tryCompare(view.pageContainer, "depth", 2)
        const addressPage = view.pageContainer.currentItem
        compare(addressPage.objectName, "addressListPage")

        view.visible = false
        compare(view.pageContainer.depth, 2)
        compare(view.pageContainer.currentItem, addressPage)

        view.visible = true
        compare(view.pageContainer.depth, 2)
        compare(view.pageContainer.currentItem, addressPage)
    }

    function test_settingsViewPinsSidebarAndLetsPageContainerGrow() {
        const view = createTemporaryObject(settingsViewComponent, settingsWindow.contentItem)
        verify(view !== null)

        const sidebarSurface = findChild(view, "settingsv2SettingsSidebarSurface")
        const sidebarHeading = findChild(view, "settingsv2SettingsSidebarHeading")
        const displayPage = findChild(view, "settingsv2DisplaySettingsPage")
        verify(sidebarSurface !== null)
        verify(sidebarHeading !== null)
        verify(displayPage !== null)

        tryCompare(sidebarSurface, "x", 0)
        tryCompare(sidebarSurface, "width", view.sidebarWidth)
        compare(sidebarSurface.color, Theme.color.neutral1)
        compare(sidebarHeading.text, "Settings")
        compare(sidebarHeading.font.pixelSize, Theme.text.display.font.pixelSize)
        compare(sidebarHeading.horizontalAlignment, Text.AlignLeft)
        tryCompare(view.pageContainer, "x", view.sidebarWidth)
        tryCompare(view.pageContainer, "width", view.width - view.sidebarWidth)

        verify(displayPage.contentHorizontalPadding >= 24)
        verify(displayPage.contentLayout.width <= displayPage.maximumContentWidth)
        verify(displayPage.contentLayout.width < view.pageContainer.width)
    }

    function test_dataHeavyPagesUseAvailableWidthWithResponsivePadding() {
        const view = createTemporaryObject(settingsViewComponent, settingsWindow.contentItem)
        verify(view !== null)
        view.width = 1500

        view.selectSection("network-traffic")
        const networkTrafficPage = findChild(view, "settingsv2NetworkTrafficSettingsPage")
        verify(networkTrafficPage !== null)
        verify(networkTrafficPage.contentHorizontalPadding >= 24)
        tryCompare(networkTrafficPage.contentLayout, "width",
            networkTrafficPage.scrollView.availableWidth
                - networkTrafficPage.contentHorizontalPadding * 2)
        verify(networkTrafficPage.contentLayout.width > 840)

        view.selectSection("debug-log")
        const debugLogPage = findChild(view, "settingsDebugLog")
        const debugLogContent = findChild(view, "debugLogContentLayout")
        verify(debugLogPage !== null)
        verify(debugLogContent !== null)
        verify(debugLogPage.contentHorizontalPadding >= 24)
        tryCompare(debugLogContent, "width",
            debugLogPage.width - debugLogPage.contentHorizontalPadding * 2)
        verify(debugLogContent.width > 840)
    }

    function test_settingsViewGroupsRelatedDestinations() {
        const view = createTemporaryObject(settingsViewComponent, settingsWindow.contentItem)
        verify(view !== null)

        compare(view.sectionForId("wallet").label, "Wallet settings")
        compare(view.sectionForId("wallet").group, "wallet")
        compare(view.sectionForId("storage").group, "general")
        compare(view.sectionForId("network-traffic").group, "network")
        compare(view.sectionForId("mempool").group, "advanced")
        compare(view.sectionForId("rpc-console").group, "advanced")
        compare(view.sectionForId("debug-log").group, "advanced")
        compare(view.groupTitles.wallet, "Wallet")
        compare(view.groupTitles.general, "General")
        compare(view.groupTitles.network, "Network")
        compare(view.groupTitles.advanced, "Advanced")
    }

    function test_rpcConsoleUsesSettingsContainerAndTracksVisibility() {
        const view = createTemporaryObject(settingsViewComponent, settingsWindow.contentItem)
        verify(view !== null)
        view.selectSection("rpc-console")

        const page = findChild(view, "settingsv2RpcConsoleSettingsPage")
        const header = findChild(view, "settingsv2RpcConsoleHeader")
        const rpcConsole = findChild(view, "settingsv2RpcConsole")
        verify(page !== null)
        verify(header !== null)
        verify(rpcConsole !== null)
        compare(header.title, "RPC console")
        compare(header.showBackButton, false)
        compare(page.maximumContentWidth, 840)
        verify(page.contentHorizontalPadding >= 24)
        compare(rpcConsole.showHeader, false)
        compare(rpcConsole.tabActive, true)

        view.selectSection("about")
        tryCompare(rpcConsole, "tabActive", false)
    }

    function test_displayPageUsesInlineGenericPickers() {
        const view = createTemporaryObject(settingsViewComponent, settingsWindow.contentItem)
        verify(view !== null)

        const themePicker = findChild(view, "settingsv2DisplayThemePicker")
        const blockStatusSizePicker = findChild(view, "settingsv2DisplayBlockStatusSizePicker")
        const moneyFontPicker = findChild(view, "settingsv2DisplayMoneyFontPicker")
        const displayUnitPicker = findChild(view, "settingsv2DisplayUnitPicker")
        const languageDisclosure = findChild(view, "settingsv2DisplayLanguageDisclosureIndicator")
        const developerSection = findChild(view, "settingsv2DisplayDeveloperSection")
        const designSystemRow = findChild(view, "settingsv2DisplayDesignSystemRow")

        verify(themePicker !== null)
        verify(blockStatusSizePicker !== null)
        verify(moneyFontPicker !== null)
        verify(displayUnitPicker !== null)
        verify(languageDisclosure !== null)
        verify(developerSection !== null)
        verify(designSystemRow !== null)
        compare(languageDisclosure.size, 14)
        compare(developerSection.visible, BuildInfo.isDebug)
        compare(displayUnitPicker.subtitleRole, "description")
        compare(displayUnitPicker.minimumMenuWidth, 400)
        tryVerify(function() { return displayUnitPicker.itemAtIndex(3) !== null })
        compare(displayUnitPicker.itemAtIndex(0).subtitle,
            "8 decimal places (0.00000001 BTC = 1 sat)")
        compare(displayUnitPicker.itemAtIndex(1).subtitle,
            "5 decimal places (0.00001 mBTC = 1 sat)")
        compare(displayUnitPicker.itemAtIndex(2).subtitle,
            "2 decimal places (0.01 bits = 1 sat)")
        compare(displayUnitPicker.itemAtIndex(3).subtitle,
            "Satoshi, the smallest unit (1 sat = 0.00000001 BTC)")

        themePicker.selected(0, "Light")
        compare(Theme.dark, false)

        compare(blockStatusSizePicker.subtitleRole, "description")
        compare(blockStatusSizePicker.iconRole, "icon")
        compare(blockStatusSizePicker.iconSize, 40)
        compare(blockStatusSizePicker.minimumMenuWidth, 520)
        compare(blockStatusSizePicker.currentText, "Compact")
        tryVerify(function() { return blockStatusSizePicker.itemAtIndex(1) !== null })
        compare(blockStatusSizePicker.itemAtIndex(0).subtitle,
            "For personal use on a computer or smartphone.")
        compare(blockStatusSizePicker.itemAtIndex(1).subtitle,
            "A larger block clock for public display on a tablet or other large screen.")
        compare(blockStatusSizePicker.itemAtIndex(0).rowIconSource.toString(),
            "image://images/blockclock-size-compact")
        compare(blockStatusSizePicker.itemAtIndex(1).rowIconSource.toString(),
            "image://images/blockclock-size-showcase")
        compare(blockStatusSizePicker.itemAtIndex(0).implicitHeight, 52)
        compare(blockStatusSizePicker.itemAtIndex(1).implicitHeight, 52)
        blockStatusSizePicker.activated(1 / 2)
        compare(Theme.blockclocksize, 1 / 2)
        compare(blockStatusSizePicker.currentText, "Showcase")

        compare(moneyFontPicker.subtitleRole, "description")
        compare(moneyFontPicker.minimumMenuWidth, 400)
        compare(moneyFontPicker.currentText, "Roboto Mono")
        tryVerify(function() { return moneyFontPicker.itemAtIndex(1) !== null })
        compare(moneyFontPicker.itemAtIndex(0).subtitle, "Included with Bitcoin Core")
        compare(moneyFontPicker.itemAtIndex(1).subtitle,
            "Uses your operating system’s default monospaced font")
        const embeddedMoneyFontWidth = moneyFontPicker.width
        moneyFontPicker.activated("best_system")
        compare(optionsModel.moneyFontChoice, "best_system")
        compare(moneyFontPicker.currentText, "System Monospace")
        tryVerify(function() { return moneyFontPicker.width > embeddedMoneyFontWidth })
        moneyFontPicker.activated("embedded")
        compare(optionsModel.moneyFontChoice, "embedded")
        compare(moneyFontPicker.currentText, "Roboto Mono")
        tryCompare(moneyFontPicker, "width", embeddedMoneyFontWidth)

        const displayUnits = [
            { value: 1, text: "mBTC" },
            { value: 2, text: "bits" },
            { value: 3, text: "sat" },
            { value: 0, text: "BTC" }
        ]
        for (let index = 0; index < displayUnits.length; ++index) {
            const unit = displayUnits[index]
            displayUnitPicker.activated(unit.value)
            compare(optionsModel.displayUnit, unit.value)
            compare(displayUnitPicker.currentValue, unit.value)
            compare(displayUnitPicker.currentText, unit.text)
        }

        designSystemRow.clicked()
        tryCompare(view.pageContainer, "depth", 2)
        verify(findChild(view, "settingsv2DisplayDesignSystemPage") !== null)
    }

    function test_mempoolPageUsesStandardFormRows() {
        const view = createTemporaryObject(settingsViewComponent, settingsWindow.contentItem)
        verify(view !== null)
        view.selectSection("mempool")

        const transactionsRow = findChild(view, "settingsv2MempoolTransactionsRow")
        const memoryUsedRow = findChild(view, "settingsv2MempoolMemoryUsedRow")
        const sizeLimitRow = findChild(view, "settingsv2MempoolSizeLimitRow")
        const sizeLimitInput = findChild(view, "settingsv2MempoolSizeLimitInput")

        verify(transactionsRow !== null)
        verify(memoryUsedRow !== null)
        verify(sizeLimitRow !== null)
        verify(sizeLimitInput !== null)
        compare(transactionsRow.titleTextStyle.font.pixelSize, Theme.text.description.font.pixelSize)
        compare(transactionsRow.valueTextStyle.font.pixelSize, Theme.text.description.font.pixelSize)
        compare(memoryUsedRow.valueTextStyle.font.pixelSize, Theme.text.description.font.pixelSize)
        verify(findChild(view, "mempoolTransactionsRow") === null)

        sizeLimitRow.text = "512"
        sizeLimitRow.editingFinished()
        compare(optionsModel.maxMempoolSizeMB, 512)
        compare(sizeLimitRow.errorText, "")

        sizeLimitRow.text = "0"
        sizeLimitRow.editingFinished()
        compare(optionsModel.maxMempoolSizeMB, 512)
        verify(sizeLimitRow.errorText.length > 0)
    }

    function test_mempoolPollingAndRestartNoticeFollowVisibility() {
        const view = createTemporaryObject(settingsViewComponent, settingsWindow.contentItem)
        verify(view !== null)
        compare(nodeModel.mempoolInfoPollingActive, false)

        view.selectSection("mempool")
        const mempoolPage = findChild(view, "settingsv2MempoolSettingsPage")
        const restartNotice = findChild(view, "settingsv2MempoolRestartNotice")
        verify(mempoolPage !== null)
        verify(restartNotice !== null)
        tryCompare(nodeModel, "mempoolInfoPollingActive", true)
        compare(restartNotice.visible, false)

        optionsModel.mempoolSettingsDirty = true
        tryCompare(restartNotice, "visible", true)

        view.selectSection("display")
        tryCompare(nodeModel, "mempoolInfoPollingActive", false)
        compare(findChild(view, "settingsv2MempoolSettingsPage"), mempoolPage)

        view.selectSection("mempool")
        tryCompare(nodeModel, "mempoolInfoPollingActive", true)
        view.visible = false
        tryCompare(nodeModel, "mempoolInfoPollingActive", false)
        view.visible = true
        tryCompare(nodeModel, "mempoolInfoPollingActive", true)

        view.destroy()
        wait(0)
        compare(nodeModel.mempoolInfoPollingActive, false)
    }

    function test_connectionProxyPageUsesRedesignedFormAndDraftCommit() {
        const view = createTemporaryObject(settingsViewComponent, settingsWindow.contentItem)
        verify(view !== null)
        view.selectSection("connection")

        const proxySettingsRow = findChild(view, "settingsv2ProxySettingsRow")
        verify(proxySettingsRow !== null)
        proxySettingsRow.clicked()
        tryCompare(view.pageContainer, "depth", 2)

        const proxyPage = findChild(view, "settingsv2ProxySettingsPage")
        const defaultProxySection = findChild(view, "settingsv2DefaultProxySection")
        const torProxySection = findChild(view, "settingsv2TorProxySection")
        const proxySwitch = findChild(view, "settingsv2ProxyEnableSwitch")
        const proxyAddressRow = findChild(view, "settingsv2ProxyAddressRow")
        const saveButton = findChild(view, "settingsv2ProxySettingsSaveButton")

        verify(proxyPage !== null)
        verify(defaultProxySection !== null)
        verify(torProxySection !== null)
        verify(proxySwitch !== null)
        verify(proxyAddressRow !== null)
        verify(saveButton !== null)
        compare(saveButton.text, "Save")
        compare(saveButton.enabled, false)
        compare(proxyAddressRow.titleTextStyle.font.pixelSize, Theme.text.description.font.pixelSize)
        compare(proxyAddressRow.enabled, false)

        proxySwitch.checked = true
        proxySwitch.toggled()
        compare(proxyPage.draftProxyEnabled, true)
        compare(proxyAddressRow.enabled, true)

        proxySwitch.checked = false
        proxySwitch.toggled()
        compare(proxyPage.proxyDraftDirty, false)
        compare(saveButton.enabled, false)

        proxySwitch.checked = true
        proxySwitch.toggled()
        compare(proxyPage.proxyDraftDirty, true)
        compare(saveButton.enabled, true)

        proxyAddressRow.text = ""
        proxyAddressRow.textEdited(proxyAddressRow.text)
        verify(proxyPage.draftProxyValidationError.length > 0)
        compare(saveButton.enabled, false)

        proxyAddressRow.text = "10.0.0.1:9050"
        proxyAddressRow.textEdited(proxyAddressRow.text)
        compare(proxyPage.proxyDraftDirty, true)
        compare(proxyPage.draftProxyValidationError, "")
        compare(saveButton.enabled, true)

        proxyPage.back()
        tryCompare(view.pageContainer, "depth", 2)
        const discardPopup = findChild(settingsWindow.contentItem, "settingsv2DiscardProxyChangesPopup")
        verify(discardPopup !== null)
        tryCompare(discardPopup, "opened", true)
        const cancelButton = findChild(settingsWindow.contentItem, "settingsv2DiscardProxyChangesCancelButton")
        verify(cancelButton !== null)
        cancelButton.clicked()
        tryCompare(discardPopup, "opened", false)

        saveButton.clicked()
        tryCompare(view.pageContainer, "depth", 1)
        const proxySetting = optionsModel.coreSettings.entry("proxy")
        compare(proxySetting.enabled, true)
        compare(proxySetting.address, "10.0.0.1:9050")
    }

    function test_settingsViewCanInstantiateEveryVisibleTopLevelDestination() {
        const view = createTemporaryObject(settingsViewComponent, settingsWindow.contentItem)
        verify(view !== null)

        const destinations = [
            { id: "wallet", objectName: "settingsv2WalletSettingsPage" },
            { id: "external-signer", objectName: "settingsv2ExternalSignerSettingsPage" },
            { id: "display", objectName: "settingsv2DisplaySettingsPage" },
            { id: "window-behavior", objectName: "settingsv2WindowBehaviorSettingsPage" },
            { id: "storage", objectName: "settingsv2StorageSettingsPage" },
            { id: "connection", objectName: "settingsv2ConnectionSettingsPage" },
            { id: "network-traffic", objectName: "settingsv2NetworkTrafficSettingsPage" },
            { id: "mempool", objectName: "settingsv2MempoolSettingsPage" },
            { id: "rpc-console", objectName: "settingsv2RpcConsoleSettingsPage" },
            { id: "debug-log", objectName: "settingsDebugLog" },
            { id: "about", objectName: "settingsv2AboutSettingsPage" }
        ]

        for (let index = 0; index < destinations.length; ++index) {
            const destination = destinations[index]
            view.selectSection(destination.id, true)
            compare(view.selectedSectionId, destination.id)
            tryCompare(view.pageContainer, "depth", 1)
            verify(findChild(view, destination.objectName) !== null,
                   "Expected instantiated destination " + destination.id)
            if (destination.id === "wallet") {
                const walletInfoSection = findChild(view, "settingsv2WalletInfoSection")
                const walletActionsSection = findChild(view, "settingsv2WalletActionsSection")
                verify(walletInfoSection !== null)
                verify(walletActionsSection !== null)
                compare(walletInfoSection.title, "Wallet info")
                compare(walletActionsSection.title, "Wallet actions")
            }
            if (destination.id === "external-signer") {
                const signerPage = findChild(view, "settingsv2ExternalSignerSettingsPage")
                const introduction = findChild(view, "settingsv2ExternalSignerIntroduction")
                const signerSection = findChild(view, "settingsv2ExternalSignerPathSection")
                const signerPathRow = findChild(view, "settingsv2ExternalSignerPathRow")
                const signerFooter = findChild(view, "settingsv2ExternalSignerPathSectionFooter")
                const signerPathInput = findChild(view, "externalSignerPathInput")
                const signerPathFocusBorder = findChild(view, "externalSignerPathFocusBorder")
                const signerStatusIndicator = findChild(view, "externalSignerStatusIndicator")
                const signerStatusText = findChild(view, "externalSignerStatusText")
                const checkDeviceButton = findChild(view, "externalSignerCheckDeviceButton")
                verify(signerPage !== null)
                verify(introduction !== null)
                verify(signerSection !== null)
                verify(signerPathRow !== null)
                verify(signerFooter !== null)
                verify(signerPathInput !== null)
                verify(signerPathFocusBorder !== null)
                verify(signerStatusIndicator !== null)
                verify(signerStatusText !== null)
                verify(checkDeviceButton !== null)
                compare(introduction.title, "")
                compare(introduction.description, "Connect a hardware wallet or another external signing tool.")
                compare(signerPage.maximumContentWidth, 840)
                compare(signerSection.title, "Signer path")
                compare(signerPathRow.topPadding, 16)
                compare(signerPathRow.bottomPadding, 16)
                compare(signerFooter.text,
                    "The add wallet flow can offer external wallets when exactly one supported signer is connected.")
                compare(signerStatusIndicator.color, Theme.color.red)
                compare(checkDeviceButton.text, "Check device")
                compare(signerPathInput.implicitHeight, 37)
                compare(signerPathInput.leftPadding, 15)
                compare(signerPathInput.rightPadding, 10)
                compare(signerPathInput.background.color, Theme.color.neutral2)
                compare(signerPathInput.background.radius, 5)
                compare(signerPathFocusBorder.border.color, Theme.color.orange)
                compare(signerStatusIndicator.width, 10)
                compare(signerStatusIndicator.height, 10)
            }
            if (destination.id === "about") {
                const versionRow = findChild(view, "settingsv2AboutVersionRow")
                const versionValue = findChild(view, "settingsv2AboutVersionRowValue")
                verify(versionRow !== null)
                verify(versionValue !== null)
                compare(versionRow.value, BuildInfo.fullClientVersion)
                compare(versionValue.text, BuildInfo.fullClientVersion)
                verify(versionValue.text.length > 0)
            }
        }
    }
}
