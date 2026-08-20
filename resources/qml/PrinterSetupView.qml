import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt.labs.platform
import "."

Page {
    id: root
    required property StackView stackView
    required property var appState
    required property Theme theme

    background: Rectangle { color: theme.bg }

    // Tracks which ICC dropdown to update after a file is chosen.
    property string iccDialogTarget: "output" // "output" | "inputCMYK" | "deviceLink"
    property string linearizationDialogTarget: "printerLinearization"
    property string sdkConnectionState: nocaiDirectPrint.connected ? "connected" : "notConnected"
    property string sdkSelectedPrinterName: ""

    // In-memory ICC list for dropdowns; populated from backend and user uploads.
    ListModel { id: iccProfileModel }
    ListModel { id: deviceLinkModel }
    ListModel { id: sdkPrinterModel }

    property bool _syncingTabs: false

    // Static capability map used for the simulated Nocai devices.
    property var nocaiPrinterCapabilities: {
        "X-33": {
            resolutions: ["720x720", "720x1440", "720x2160"],
            mediaSizes: ["A0", "A1", "A2", "A3", "A4", "A5", "A6", "Letter", "Legal", "Tabloid", "12x18", "18x24", "24x36", "24x48", "32x48"],
            duplexModes: ["None"],
            colorModes: ["CMYK", "CMYKWW", "CMYKWV"]
        },

        "X-24": {
            resolutions: ["720x720", "720x1440", "720x2160"],
            mediaSizes: ["A2", "A3", "A4", "A5", "A6", "Letter", "Legal", "Tabloid", "12x18", "18x24", "24x36"],
            duplexModes: ["None"],
            colorModes: ["CMYK", "CMYKWW", "CMYKWV"]
        },

        "X-36 Studio": {
            resolutions: ["720x720", "720x1440", "720x2160"],
            mediaSizes: ["A2", "A3", "A4", "A5", "A6", "Letter", "Legal", "Tabloid", "12x18", "18x24", "24x36", "24x48", "32x48"],
            duplexModes: ["None"],
            colorModes: [
                "4: CMYK",
                "5: CMYK+W",
                "6: CMYK+Lc+Lm",
                "7: CMYK+Lc+Lm+W",
                "8: CMYK+Lc+Lm+Lk+LLk",
                "10: CMYK+Lc+Lm+Lk+LLk+W+V"
            ]
        }
    }

    function hasPrinterSelected() {
        return appState.selectedPrinter && appState.selectedPrinter.length > 0
    }

    function activeBackend() {
        if (!hasPrinterSelected()) return null
        return appState.usingMultiInkPrinter ? printJobMultiInk : printJobCMYK
    }

    function normalizePath(urlOrPath) {
        const s = (urlOrPath || "").toString()
        return s.startsWith("file://") ? s.slice(7) : s
    }

    function isX36MultiInk() {
        return appState.usingSimulatedPrinter
               && appState.usingMultiInkPrinter
               && colorManager.directPrintSdkFamilyForPrinter(appState.selectedPrinter) === "multi-ink"
    }

    function isSdkCapablePrinter() {
        return appState.usingSimulatedPrinter
               && colorManager.directPrintSdkFamilyForPrinter(appState.selectedPrinter).length > 0
    }

    function setDirectSetting(key, value) {
        colorManager.setDirectPrintSetting(key, value)
    }

    function rememberSdkPrinter(printer) {
        appState.sdkSelectedPrinterIndex = printer.index
        appState.sdkSelectedPrinterName = printer.name
        sdkSelectedPrinterName = printer.name
        setDirectSetting("selectedPrinterIndex", printer.index)
        setDirectSetting("selectedPrinterName", printer.name)
    }

    function refreshSdkPrinters() {
        sdkPrinterModel.clear()
        appState.configureDirectPrintSdk()
        const ok = nocaiDirectPrint.refreshPrinters()
        const printers = nocaiDirectPrint.printers
        for (let i = 0; i < printers.length; ++i)
            sdkPrinterModel.append(printers[i])

        if (ok && sdkPrinterModel.count === 1) {
            const onlyPrinter = sdkPrinterModel.get(0)
            rememberSdkPrinter(onlyPrinter)
            nocaiDirectPrint.choosePrinter(onlyPrinter.index)
        }

        syncSdkPrinterCombo()
        toast.show(ok ? strings.trKey("printerSetup.toast.sdkPrintersRefreshed")
                      : strings.trKey("printerSetup.toast.sdkUnavailable") + nocaiDirectPrint.lastError)
        return ok
    }

    function connectSdkPrinter() {
        if (nocaiDirectPrint.maintenanceBusy) {
            toast.show(strings.trKey("printerSetup.toast.sdkBusy"))
            return
        }

        appState.configureDirectPrintSdk()
        sdkConnectionState = "connecting"
        const started = nocaiDirectPrint.startMaintenanceAction(
            "ConnectPrinter",
            {"printerIndex": appState.sdkSelectedPrinterIndex})
        if (!started) {
            sdkConnectionState = "failed"
            toast.show(strings.trKey("printerSetup.toast.connectFailed") + nocaiDirectPrint.lastError)
        }
    }

    // The printer service outlives this page, while sdkPrinterModel does not.
    // Rehydrate the page model whenever it is created or the service list changes.
    function syncSdkPrintersFromService() {
        sdkPrinterModel.clear()
        const printers = nocaiDirectPrint.printers
        for (let i = 0; i < printers.length; ++i)
            sdkPrinterModel.append(printers[i])

        syncSdkPrinterCombo()
    }

    Connections {
        target: nocaiDirectPrint
        function onPrintersChanged() {
            root.syncSdkPrintersFromService()
        }

        function onMaintenanceActionFinished(action, succeeded, result, errorMessage) {
            if (action === "ConnectPrinter" && root.sdkConnectionState === "connecting") {
                root.syncSdkPrintersFromService()
                root.sdkConnectionState = succeeded ? "connected" : "failed"
                toast.show(succeeded
                           ? strings.trKey("printerSetup.toast.sdkConnected")
                           : strings.trKey("printerSetup.toast.connectFailed") + errorMessage)
                if (succeeded)
                    Qt.callLater(root.refreshSdkStatusAndInfo)
                return
            }
            if (action === "GetPrinterStatus") {
                root.sdkConnectionState = succeeded ? "connected" : "unavailable"
                toast.show(succeeded
                           ? strings.trKey("printerSetup.toast.statusRefreshed")
                           : strings.trKey("printerSetup.toast.statusUnavailable") + errorMessage)
                return
            }
            if (action === "ReconnectPrinter" || action === "RestartPrinterService") {
                root.sdkConnectionState = succeeded ? "connected" : "failed"
            }
        }
    }

    function refreshSdkStatusAndInfo() {
        if (nocaiDirectPrint.maintenanceBusy) {
            toast.show(strings.trKey("printerSetup.toast.sdkBusy"))
            return
        }
        if (!nocaiDirectPrint.startMaintenanceAction("GetPrinterStatus", {}))
            toast.show(strings.trKey("printerSetup.toast.statusRefreshFailed") + nocaiDirectPrint.lastError)
    }

    function syncSdkPrinterCombo() {
        sdkPrinterCombo.currentIndex = -1
        sdkSelectedPrinterName = ""
        let savedNameMatch = -1
        for (let i = 0; i < sdkPrinterModel.count; ++i) {
            const printer = sdkPrinterModel.get(i)
            if (printer.index === appState.sdkSelectedPrinterIndex) {
                sdkPrinterCombo.currentIndex = i
                sdkSelectedPrinterName = printer.name
                break
            }
            if ((appState.sdkSelectedPrinterName || "").length > 0
                    && printer.name === appState.sdkSelectedPrinterName)
                savedNameMatch = i
        }

        // Discovery indexes can change between SDK sessions. The saved name is
        // the stable fallback and updates the index used by direct printing.
        if (sdkPrinterCombo.currentIndex < 0 && savedNameMatch >= 0) {
            sdkPrinterCombo.currentIndex = savedNameMatch
            rememberSdkPrinter(sdkPrinterModel.get(savedNameMatch))
        } else if (sdkPrinterCombo.currentIndex < 0 && sdkPrinterModel.count === 1) {
            sdkPrinterCombo.currentIndex = 0
            rememberSdkPrinter(sdkPrinterModel.get(0))
        } else if (sdkPrinterCombo.currentIndex >= 0
                   && appState.sdkSelectedPrinterName !== sdkSelectedPrinterName) {
            rememberSdkPrinter(sdkPrinterModel.get(sdkPrinterCombo.currentIndex))
        }
    }

	function currentOutputProfileInkMode() {
		if (appState.usingMultiInkPrinter) {
		    return appState.multiInkInkMode || 4
		}

		// Non-multi-ink Nocai path:
		// treat as Family A fallback for now
		return 4
	}

	function resolvedOutputProfileForCurrentSelection() {
		if (!hasPrinterSelected())
		    return ""

		const inkMode = currentOutputProfileInkMode()
		return colorManager.effectiveOutputProfileForPrinterAndInkMode(appState.selectedPrinter, inkMode)
	}

	function applyResolvedOutputProfileToBackend() {
		const backend = activeBackend()
		if (!backend)
		    return

		const resolved = resolvedOutputProfileForCurrentSelection()
		if (resolved && resolved.length > 0) {
		    backend.setDefaultOutputICCProfile(resolved)
		}
	}

	function currentFamilyKey() {
		return colorManager.outputProfileFamilyForInkMode(currentOutputProfileInkMode())
	}

	function resolvedLinearizationForCurrentSelection() {
		if (!hasPrinterSelected())
		    return ""

		const inkMode = currentOutputProfileInkMode()
		return colorManager.effectiveLinearizationPathForPrinterAndInkMode(appState.selectedPrinter, inkMode)
	}

	function syncUIFromAppState() {

		// --- Simulated / Nocai path ---
		if (appState.usingSimulatedPrinter) {
		    const selected = appState.selectedPrinter || ""

		    // Pre-select Nocai model if one is already chosen
		    const nocaiNames = ["X-33", "X-36 Studio"]
		    const nocaiIndex = nocaiNames.indexOf(selected)
		    if (nocaiIndex >= 0)
		        nocaiPrinterComboBox.currentIndex = nocaiIndex

		    const isMultiInk = colorManager.directPrintSdkFamilyForPrinter(selected) === "multi-ink"
		    appState.usingMultiInkPrinter = isMultiInk
            appState.configureDirectPrintSdk()

		    // Choose backend and ensure assets/ICC are ready
		    let backend
		    if (isMultiInk) {
		        backend = printJobMultiInk
		        printJobMultiInk.prepareAssets()
		    } else {
		        backend = printJobCMYK
		        printJobCMYK.prepareAssets()
		    }

		    // Rebuild ICC list from backend
		    iccProfileModel.clear()
		    const profiles = backend.getAvailableICCProfiles()
		    for (let i = 0; i < profiles.length; ++i) {
		        iccProfileModel.append(profiles[i])
		    }

		    // Sync Output ICC dropdown
			const resolvedOutput = root.resolvedOutputProfileForCurrentSelection()
			if (resolvedOutput && resolvedOutput.length > 0) {
				backend.setDefaultOutputICCProfile(resolvedOutput)
			}

			const currentDefault = (resolvedOutput && resolvedOutput.length > 0)
					? resolvedOutput
					: backend.getDefaultOutputICCProfile()

			iccProfileDropdown.currentIndex = -1
			for (let i = 0; i < iccProfileModel.count; ++i) {
				if (normalizePath(iccProfileModel.get(i).path) === normalizePath(currentDefault)) {
					iccProfileDropdown.currentIndex = i
					break
				}
			}

		    // Sync Input CMYK dropdown
		    const currentInputCmyk = backend.getDefaultInputCMYKProfile()
		    inputCmykDropdown.currentIndex = -1
		    for (let i = 0; i < iccProfileModel.count; ++i) {
		        if (normalizePath(iccProfileModel.get(i).path) === normalizePath(currentInputCmyk)) {
		            inputCmykDropdown.currentIndex = i
		            break
		        }
		    }

		    // Sync toggle
		    useInputCmykSwitch.checked = backend.checkDefaultInputCMYK()

		    // Sync Ink Layout for multi-ink printer
		    if (isMultiInk) {
		        for (let i = 0; i < inkLayoutCombo.model.count; ++i) {
		            const elem = inkLayoutCombo.model.get(i)
		            if (elem.value === appState.multiInkInkMode) {
		                inkLayoutCombo.currentIndex = i
		                break
		            }
		        }
		        printJobMultiInk.setInkMode(appState.multiInkInkMode)
		        printJobMultiInk.enableDefaultInputCMYK(true)
		    }

		    // Guard in case the DeviceLink UI hasn't been instantiated yet
		    if (typeof deviceLinkModel !== "undefined" &&
		        typeof deviceLinkSwitch !== "undefined" &&
		        typeof deviceLinkDropdown !== "undefined") {

		        deviceLinkModel.clear()

		        if (isMultiInk) {
		            const links = printJobMultiInk.getAvailableDeviceLinkProfiles()
		            for (let i = 0; i < links.length; ++i) {
		                deviceLinkModel.append(links[i])
		            }

		            // Toggle from backend
		            deviceLinkSwitch.checked = printJobMultiInk.isDeviceLinkEnabled()

		            // Sync dropdown selection from backend default
		            const currentDL = printJobMultiInk.getDefaultDeviceLinkProfile()
		            deviceLinkDropdown.currentIndex = -1
		            for (let i = 0; i < deviceLinkModel.count; ++i) {
		                if (deviceLinkModel.get(i).path === currentDL) {
		                    deviceLinkDropdown.currentIndex = i
		                    break
		                }
		            }
		        } else {
		            // Not multi-ink -> ensure it's off/empty
		            deviceLinkSwitch.checked = false
		            deviceLinkDropdown.currentIndex = -1
		        }
		    }

		// --- Network printer path ---
		} else if (hasPrinterSelected()) {
		    const printers = printJobOutput.detectedPrinters
		    if (printers && printers.length) {
		        for (let i = 0; i < printers.length; ++i) {
		            if (printers[i] === appState.selectedPrinter) {
		                printerComboBox.currentIndex = i
		                break
		            }
		        }
		    }

		    // OPTIONAL: if DeviceLink controls exist, hard-disable them on network tab
		    if (typeof deviceLinkSwitch !== "undefined") {
		        deviceLinkSwitch.checked = false
		    }
		    if (typeof deviceLinkDropdown !== "undefined") {
		        deviceLinkDropdown.currentIndex = -1
		    }
		    if (typeof deviceLinkModel !== "undefined") {
		        deviceLinkModel.clear()
		    }
		}

        if (typeof sdkPrinterCombo !== "undefined")
            syncSdkPrinterCombo()
	}

    function goBack() {
        if (root.stackView && root.stackView.depth > 1) {
            root.stackView.pop()
            return
        }

        if (StackView.view)
            StackView.view.pop()
    }

    function doSave() {
        if (!hasPrinterSelected()) return
        colorManager.selectedPrinter = appState.selectedPrinter
        colorManager.save()
        toast.show(strings.trKey("printerSetup.toast.complete") + appState.selectedPrinter)
        goBack()
    }

	Component.onCompleted: {
		printJobOutput.refreshDetectedPrinters()

        // App defaults: X-33 using the legacy standard-CMYK SDK path.
		if (!appState.selectedPrinter || appState.selectedPrinter.length === 0) {
		    appState.selectedPrinter = "X-33"
		    appState.usingSimulatedPrinter = true
		    appState.usingMultiInkPrinter = false
		    printJobCMYK.enableDefaultInputCMYK(true)
		}

		_syncingTabs = true
		printerTabs.currentIndex = (appState.usingSimulatedPrinter ? 0 : 1)
		_syncingTabs = false

		Qt.callLater(syncUIFromAppState)
		Qt.callLater(syncSdkPrintersFromService)
	}

    onVisibleChanged: {
		if (visible) {
		    _syncingTabs = true
		    printerTabs.currentIndex = (appState.usingSimulatedPrinter ? 0 : 1)
		    _syncingTabs = false

		    Qt.callLater(syncUIFromAppState)
		    Qt.callLater(syncSdkPrintersFromService)
		}
    }

    Rectangle {
        id: headerBar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: theme.appBarHeight
        color: theme.surface

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            spacing: 10

            ThemedButton {
                text: strings.trKey("common.back")
                theme: root.theme
                Layout.preferredWidth: root.theme.headerButtonWidth(root.width)
                padding: 12
                font.pixelSize: 15
                onClicked: root.goBack()
            }

            Item { Layout.fillWidth: true }

            Label {
                text: strings.trKey("printerSetup.title")
                color: theme.text
                font.pixelSize: root.theme.headerTitleSize(root.width)
                font.weight: Font.Medium
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.alignment: Qt.AlignVCenter
            }

            Item { Layout.fillWidth: true }

            ThemedButton {
                text: strings.trKey("common.save")
                theme: root.theme
                Layout.preferredWidth: root.theme.headerButtonWidth(root.width)
                padding: 12
                font.pixelSize: 15
                enabled: hasPrinterSelected()
                onClicked: root.doSave()
            }
        }
    }

    ScrollView {
        id: scroll
        anchors.top: headerBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: theme.pageMargin
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: scroll.availableWidth
            spacing: 14
            Layout.alignment: Qt.AlignHCenter

            // =========================
            // Printer Mode
            // =========================
            Pane {
                Layout.fillWidth: true
                padding: 12

                background: Rectangle {
                    color: theme.surface
                    radius: 12
                    border.width: 1
                    border.color: theme.divider
                }

                ColumnLayout {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: theme.boundedWidth(parent.width, 520)
                    spacing: 12

                    Label {
                        text: strings.trKey("printerSetup.printerMode")
                        color: theme.text
                        font.pixelSize: theme.sectionTitleSize
                        font.weight: Font.Medium
                        Layout.alignment: Qt.AlignHCenter
                    }

                    Rectangle { height: 1; Layout.fillWidth: true; color: theme.divider; opacity: 0.8 }

                    TabBar {
                        id: printerTabs
                        Layout.fillWidth: true
                        TabButton { text: strings.trKey("printerSetup.nocaiPrinter") }
                        TabButton { text: strings.trKey("printerSetup.networkPrinter") }

                        onCurrentIndexChanged: {
                            if (root._syncingTabs) return

                            if (currentIndex === 0) {
                                appState.usingSimulatedPrinter = true
                            } else if (currentIndex === 1) {
                                appState.usingSimulatedPrinter = false
                                appState.usingMultiInkPrinter = false
                            }
                            Qt.callLater(root.syncUIFromAppState)
                        }
                    }

                    StackLayout {
                        id: tabStack
                        currentIndex: printerTabs.currentIndex
                        Layout.fillWidth: true

                        // --- Tab 0: Nocai (simulated) printers ---
                        ColumnLayout {
                            id: nocaiTab
                            spacing: 10
                            Layout.fillWidth: true

                            Label {
                                text: strings.trKey("printerSetup.selectNocaiPrinter")
                                color: theme.text
                                font.weight: Font.Medium
                                Layout.alignment: Qt.AlignHCenter
                            }

                            ComboBox {
                                id: nocaiPrinterComboBox
                                Layout.fillWidth: true
                                model: [
                                    "X-33",
                                    "X-36 Studio"
                                ]

                                onActivated: {
                                    const selected = currentText
                                    if (!selected || selected.length <= 0) return

                                    appState.selectedPrinter = selected
                                    colorManager.selectedPrinter = selected
                                    appState.usingSimulatedPrinter = true

                                    const isMultiInk = colorManager.directPrintSdkFamilyForPrinter(selected) === "multi-ink"
                                    appState.usingMultiInkPrinter = isMultiInk
                                    appState.configureDirectPrintSdk()

									if (isMultiInk) {
										const validModes = [4, 5, 6, 7, 8, 10]
										if (validModes.indexOf(appState.multiInkInkMode) === -1) {
											appState.multiInkInkMode = 10
										}

										for (let i = 0; i < inkLayoutCombo.model.count; ++i) {
											const elem = inkLayoutCombo.model.get(i)
											if (elem.value === appState.multiInkInkMode) {
												inkLayoutCombo.currentIndex = i
												break
											}
										}

										printJobMultiInk.setInkMode(appState.multiInkInkMode)
										printJobMultiInk.enableDefaultInputCMYK(true)
									}

                                    let backend
									if (isMultiInk) {
										backend = printJobMultiInk
										printJobMultiInk.prepareAssets()
									} else {
										backend = printJobCMYK
										printJobCMYK.prepareAssets()
									}

									iccProfileModel.clear()
									const profiles = backend.getAvailableICCProfiles()
									for (let i = 0; i < profiles.length; ++i)
										iccProfileModel.append(profiles[i])

									Qt.callLater(root.syncUIFromAppState)

									toast.show((isMultiInk ? strings.trKey("printerSetup.multiInkPrefix") : "")
                                               + strings.trKey("printerSetup.toast.nocaiSelected") + selected)
                                }
                            }

                            // Ink layout selection – only for X-36 Studio.
                            ColumnLayout {
                                visible: appState.usingSimulatedPrinter
                                         && colorManager.directPrintSdkFamilyForPrinter(appState.selectedPrinter) === "multi-ink"
                                Layout.fillWidth: true
                                spacing: 8

                                Label { text: strings.trKey("printerSetup.inkLayout"); color: theme.text; font.bold: true }

                                ComboBox {
                                    id: inkLayoutCombo
                                    Layout.fillWidth: true

                                    model: ListModel {
                                        ListElement { label: "4 – CMYK";                    value: 4  }
                                        ListElement { label: "5 – CMYK+W";                  value: 5  }
                                        ListElement { label: "6 – CMYK+Lc+Lm";              value: 6  }
                                        ListElement { label: "7 – CMYK+Lc+Lm+W";            value: 7  }
                                        ListElement { label: "8 – CMYK+Lc+Lm+Lk+LLk";       value: 8  }
                                        ListElement { label: "10 – CMYK+Lc+Lm+Lk+LLk+W+V";  value: 10 }
                                    }
                                    textRole: "label"

									onActivated: {
										const elem = model.get(currentIndex)
										const mode = elem.value
										appState.multiInkInkMode = mode
										printJobMultiInk.setInkMode(mode)

										root.applyResolvedOutputProfileToBackend()
										Qt.callLater(root.syncUIFromAppState)

										toast.show(strings.trKey("printerSetup.toast.inkLayoutSet") + elem.label)
									}
                                }
                            }

	                            ColumnLayout {
	                                visible: root.isSdkCapablePrinter()
	                                Layout.fillWidth: true
	                                spacing: 10

	                                Rectangle { height: 1; Layout.fillWidth: true; color: theme.divider; opacity: 0.8 }

	                                Label {
	                                    text: strings.trKey("printerSetup.outputMode")
	                                    color: theme.text
	                                    font.bold: true
	                                    Layout.alignment: Qt.AlignHCenter
	                                }

	                                ComboBox {
	                                    id: directOutputModeCombo
	                                    Layout.fillWidth: true
                                    model: ListModel {
                                        ListElement { labelKey: "printerSetup.prnGeneration"; value: "prn" }
                                        ListElement { labelKey: "printerSetup.directToPrint"; value: "direct" }
                                    }
                                    delegate: ItemDelegate {
                                        width: directOutputModeCombo.width
                                        text: strings.trKey(labelKey)
                                    }
                                    contentItem: Text {
                                        text: directOutputModeCombo.currentIndex >= 0
                                              ? strings.trKey(directOutputModeCombo.model.get(directOutputModeCombo.currentIndex).labelKey)
                                              : ""
                                        color: root.theme.text
                                        verticalAlignment: Text.AlignVCenter
                                        elide: Text.ElideRight
                                    }
                                    currentIndex: appState.multiInkOutputMode === "direct" ? 1 : 0

                                    onActivated: {
                                        const selected = model.get(currentIndex)
                                        appState.multiInkOutputMode = selected.value
                                        colorManager.setMultiInkOutputMode(selected.value)
                                        toast.show(strings.trKey("printerSetup.toast.outputModeSet") + strings.trKey(selected.labelKey))
                                    }
                                }

                                Label {
                                    text: appState.multiInkOutputMode === "direct"
                                          ? (root.isX36MultiInk()
                                             ? strings.trKey("printerSetup.directMode.newSdkHelp")
                                             : strings.trKey("printerSetup.directMode.x33Help"))
                                          : strings.trKey("printerSetup.prnMode.help")
	                                    color: theme.subtext
	                                    wrapMode: Text.WordWrap
	                                    Layout.fillWidth: true
	                                    horizontalAlignment: Text.AlignHCenter
	                                }


                            }
                        }

                        // --- Tab 1: Network printers ---
                        ColumnLayout {
                            id: networkTab
                            spacing: 10
                            Layout.fillWidth: true

                            Label {
                                text: strings.trKey("printerSetup.selectNetworkPrinter")
                                color: theme.text
                                font.weight: Font.Medium
                            }

                            ComboBox {
                                id: printerComboBox
                                Layout.fillWidth: true
                                model: printJobOutput.detectedPrinters

                                onActivated: {
                                    const name = currentText
                                    if (printJobOutput.loadPrinter(name)) {
                                        appState.selectedPrinter = name
                                        appState.usingSimulatedPrinter = false
                                        appState.usingMultiInkPrinter = false

                                        // Warm the backend capability lists
                                        printJobOutput.supportedResolutions()
                                        printJobOutput.supportedMediaSizes()
                                        printJobOutput.supportedDuplexModes()
                                        printJobOutput.supportedColorModes()

                                        toast.show(strings.trKey("printerSetup.toast.networkLoaded") + name)
                                    } else {
                                        toast.show(strings.trKey("printerSetup.toast.networkLoadFailed") + name)
                                    }
                                }
                            }

                            ThemedButton {
                                text: strings.trKey("printerSetup.refreshList")
                                theme: root.theme
                                padding: 12
                                font.pixelSize: 15
                                onClicked: printJobOutput.refreshDetectedPrinters()
                            }
                        }

		                    }
		                }
		            }


            // =========================
            // Direct Print SDK
            // =========================
            Pane {
                Layout.fillWidth: true
                padding: 12
                visible: root.isSdkCapablePrinter() && appState.multiInkOutputMode === "direct"

                background: Rectangle {
                    color: theme.surface
                    radius: 12
                    border.width: 1
                    border.color: theme.divider
                }

                ColumnLayout {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: theme.boundedWidth(parent.width, 520)
                    spacing: 10

                                    Rectangle { Layout.preferredHeight: 1; Layout.fillWidth: true; color: theme.divider; opacity: 0.65 }

                                    Label {
                                        text: strings.trKey("printerSetup.directPrintSdk")
                                        color: theme.text
                                        font.bold: true
                                        Layout.alignment: Qt.AlignHCenter
                                    }

                                    ThemedButton {
                                        text: strings.trKey("printerSetup.refreshSdkPrinters")
                                        theme: root.theme
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 40
                                        enabled: !nocaiDirectPrint.maintenanceBusy
                                        onClicked: root.refreshSdkPrinters()
                                    }

                                ComboBox {
                                    id: sdkPrinterCombo
                                    Layout.fillWidth: true
                                    model: sdkPrinterModel
                                    textRole: "name"
                                    displayText: currentIndex >= 0
                                                 ? currentText
                                                 : strings.trKey("printerSetup.noSdkPrinterSelected")

	                                    onActivated: {
	                                        if (sdkPrinterModel.count <= 0) return
	                                        const selected = sdkPrinterModel.get(currentIndex)
	                                        root.rememberSdkPrinter(selected)
	                                        nocaiDirectPrint.choosePrinter(selected.index)
	                                    }
	                                }

	                                GridLayout {
	                                    Layout.alignment: Qt.AlignHCenter
	                                    Layout.fillWidth: true
	                                    columns: theme.gridColumns(width, 2, 150)
	                                    columnSpacing: 10
	                                    rowSpacing: 10

	                                    ThemedButton {
	                                        text: root.sdkConnectionState === "connecting"
	                                                  ? strings.trKey("printerSetup.connecting")
                                                  : (root.sdkConnectionState === "connected" ? strings.trKey("common.connected") : strings.trKey("common.connect"))
	                                        theme: root.theme
	                                        Layout.fillWidth: true
	                                        Layout.preferredHeight: 40
	                                        enabled: !nocaiDirectPrint.maintenanceBusy
	                                        background: Rectangle {
	                                            radius: 6
	                                            color: root.sdkConnectionState === "connected"
	                                                   ? root.theme.accent2
	                                                   : (root.sdkConnectionState === "connecting" ? root.theme.accent : root.theme.danger)
	                                            border.width: 1
	                                            border.color: root.theme.divider
	                                        }
	                                        onClicked: root.connectSdkPrinter()
	                                    }

	                                    ThemedButton {
	                                        text: strings.trKey("printerSetup.refreshStatus")
	                                        theme: root.theme
	                                        Layout.fillWidth: true
	                                        Layout.preferredHeight: 40
	                                        enabled: !nocaiDirectPrint.maintenanceBusy
	                                        onClicked: root.refreshSdkStatusAndInfo()
	                                    }
	                                }


                                    ThemedButton {
                                        text: strings.trKey("printerSetup.printerSettings")
                                        theme: root.theme
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 42
                                        onClicked: root.stackView.push(
                                            "qrc:/qml/PrinterSettingsView.qml", {
                                                "stackView": root.stackView,
                                                "appState": root.appState,
                                                "theme": root.theme
                                            })
                                    }

                                }
            }

            // DeviceLink controls (MultiInk only)
			Pane {
				Layout.fillWidth: true
				padding: 12
				visible: appState.usingSimulatedPrinter && appState.usingMultiInkPrinter

				background: Rectangle {
					color: theme.surface
					radius: 12
					border.width: 1
					border.color: theme.divider
				}

				ColumnLayout {
					anchors.horizontalCenter: parent.horizontalCenter
					width: theme.boundedWidth(parent.width, 520)
					spacing: 10

					Label {
						text: strings.trKey("printerSetup.deviceLink")
						color: theme.text
						font.pixelSize: 16
						font.weight: Font.Medium
						Layout.alignment: Qt.AlignHCenter
					}

					Rectangle { height: 1; Layout.fillWidth: true; color: theme.divider; opacity: 0.8 }

					RowLayout {
						Layout.fillWidth: true
						spacing: 10

						Label {
						    text: strings.trKey("printerSetup.enableDeviceLink")
						    color: theme.text
						    wrapMode: Text.WordWrap
						    Layout.fillWidth: true
						}

						Switch {
                            Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
						    id: deviceLinkSwitch
						    checked: false
						    onToggled: {
						        printJobMultiInk.enableDeviceLink(checked)
						        toast.show(checked ? strings.trKey("printerSetup.toast.deviceLinkEnabled")
                                                           : strings.trKey("printerSetup.toast.deviceLinkDisabled"))
						    }
						}
					}

					RowLayout {
						Layout.fillWidth: true
						spacing: 10
						enabled: deviceLinkSwitch.checked

						ComboBox {
						    id: deviceLinkDropdown
						    Layout.fillWidth: true
						    model: deviceLinkModel
						    textRole: "name"
						    displayText: currentIndex >= 0 ? currentText : strings.trKey("common.none")

						    onActivated: {
						        if (deviceLinkModel.count <= 0) return
						        const selected = deviceLinkModel.get(currentIndex)
						        printJobMultiInk.setDefaultDeviceLinkProfile(selected.path)
						    }
						}

						ThemedButton {
						    text: strings.trKey("common.upload")
						    theme: root.theme
						    onClicked: {
						        iccDialogTarget = "deviceLink"
						        iccUploadDialog.open()
						    }
						}
					}
				}
			}


            // =========================
            // ICC Profiles (Nocai only)
            // =========================
            Pane {
                Layout.fillWidth: true
                padding: 16
                enabled: appState.usingSimulatedPrinter

                background: Rectangle {
                    color: theme.surface
                    radius: 12
                    border.width: 1
                    border.color: theme.divider
					opacity: appState.usingSimulatedPrinter ? 1.0 : 0.6
                }

                ColumnLayout {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: theme.boundedWidth(parent.width, 520)
                    spacing: 12

                    Label {
                        text: strings.trKey("printerSetup.iccProfiles")
                        color: theme.text
                        font.pixelSize: theme.sectionTitleSize
                        font.weight: Font.Medium
                        Layout.alignment: Qt.AlignHCenter
                    }

                    Rectangle { height: 1; Layout.fillWidth: true; color: theme.divider; opacity: 0.8 }

                    Label { text: strings.trKey("printerSetup.defaultOutputIcc"); color: theme.text; font.bold: true }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        ComboBox {
                            id: iccProfileDropdown
                            Layout.fillWidth: true
                            model: iccProfileModel
                            textRole: "name"
                            displayText: currentIndex >= 0 ? currentText : strings.trKey("common.none")

							onActivated: {
								if (!root.appState.usingSimulatedPrinter) return
								if (iccProfileModel.count <= 0) return
								if (!root.appState.selectedPrinter || root.appState.selectedPrinter.length <= 0) return

								const selected = iccProfileModel.get(currentIndex)
								const backend = root.activeBackend()
								if (backend) backend.setDefaultOutputICCProfile(selected.path)

								const inkMode = root.currentOutputProfileInkMode()
								const family = colorManager.outputProfileFamilyForInkMode(inkMode)
								colorManager.setPrinterFamilyOutputProfile(root.appState.selectedPrinter, family, selected.path)
							}
                        }

                        ThemedButton {
                            text: strings.trKey("common.upload")
                            theme: root.theme
                            onClicked: { iccDialogTarget = "output"; iccUploadDialog.open() }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        Label {
                            text: strings.trKey("printerSetup.useDefaultInputCmyk")
                            color: theme.text
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }

                        Switch {
                            Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                            id: useInputCmykSwitch
                            checked: root.activeBackend() ? root.activeBackend().checkDefaultInputCMYK() : false
                            onToggled: {
                                const backend = root.activeBackend()
                                if (backend) backend.enableDefaultInputCMYK(checked)
                            }
                        }
                    }

                    Label { text: strings.trKey("printerSetup.defaultInputCmyk"); color: theme.text; font.bold: true }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        ComboBox {
                            id: inputCmykDropdown
                            Layout.fillWidth: true
                            model: iccProfileModel
                            textRole: "name"
                            displayText: currentIndex >= 0 ? currentText : strings.trKey("common.none")
                            enabled: useInputCmykSwitch.checked

                            onActivated: {
                                if (!enabled) return
                                if (iccProfileModel.count <= 0) return
                                const selected = iccProfileModel.get(currentIndex)
                                const backend = root.activeBackend()
                                if (backend) backend.setDefaultInputCMYKProfile(selected.path)
                            }
                        }

                        ThemedButton {
                            text: strings.trKey("common.upload")
                            theme: root.theme
                            enabled: useInputCmykSwitch.checked
                            onClicked: { iccDialogTarget = "inputCMYK"; iccUploadDialog.open() }
                        }
                    }

                    Label {
						visible: appState.usingMultiInkPrinter
						text: strings.trKey("printerSetup.linearizationXml")
						color: theme.text
						font.bold: true
					}

					RowLayout {
						visible: appState.usingMultiInkPrinter
						Layout.fillWidth: true
						spacing: 10

						Label {
							Layout.fillWidth: true
							text: {
								const p = root.resolvedLinearizationForCurrentSelection()
								return (p && p.length > 0) ? p : strings.trKey("common.none")
							}
							color: theme.subtext
							wrapMode: Text.WrapAnywhere
						}

						ThemedButton {
							text: strings.trKey("common.load")
							theme: root.theme
							onClicked: linearizationUploadDialog.open()
						}

						ThemedButton {
							text: strings.trKey("common.clear")
							theme: root.theme
							enabled: root.resolvedLinearizationForCurrentSelection().length > 0
							onClicked: {
								if (!root.appState.selectedPrinter || root.appState.selectedPrinter.length <= 0)
									return

								const family = root.currentFamilyKey()
								colorManager.setPrinterFamilyLinearizationPath(root.appState.selectedPrinter, family, "")
								Qt.callLater(root.syncUIFromAppState)
								toast.show(strings.trKey("printerSetup.toast.linearizationCleared"))
							}
						}
					}

                    FileDialog {
                        id: iccUploadDialog
						title: (iccDialogTarget === "deviceLink")
                               ? strings.trKey("printerSetup.selectDeviceLink.title")
                               : strings.trKey("printerSetup.selectIcc.title")
                        nameFilters: ["ICC Profiles (*.icc *.icm)", "All Files (*)"]
                        fileMode: FileDialog.OpenFile

                        onAccepted: {
							const path = normalizePath(iccUploadDialog.file.toString())
							const name = path.split("/").pop()
							const backend = root.activeBackend()

							if (iccDialogTarget === "deviceLink") {
								deviceLinkModel.append({ name: name, path: path })
								printJobMultiInk.addDeviceLinkProfile(name, path)
								deviceLinkDropdown.currentIndex = deviceLinkModel.count - 1
								printJobMultiInk.setDefaultDeviceLinkProfile(path)
								toast.show(strings.trKey("printerSetup.toast.deviceLinkAdded") + name)
								return
							}

							iccProfileModel.append({ name: name, path: path })
							if (backend) backend.addICCProfile(name, path)

							if (iccDialogTarget === "output") {
								iccProfileDropdown.currentIndex = iccProfileModel.count - 1
								if (backend) backend.setDefaultOutputICCProfile(path)

								if (root.appState.selectedPrinter && root.appState.selectedPrinter.length > 0) {
									const inkMode = root.currentOutputProfileInkMode()
									const family = colorManager.outputProfileFamilyForInkMode(inkMode)
									colorManager.setPrinterFamilyOutputProfile(root.appState.selectedPrinter, family, path)
								}
							} else if (iccDialogTarget === "inputCMYK") {
								inputCmykDropdown.currentIndex = iccProfileModel.count - 1
								if (backend) backend.setDefaultInputCMYKProfile(path)
								colorManager.defaultInputProfile = path
							}

							toast.show(strings.trKey("printerSetup.toast.iccAdded") + name)
						}
                    }

                    FileDialog {
						id: linearizationUploadDialog
						title: strings.trKey("printerSetup.selectLinearization.title")
						nameFilters: ["Linearization XML (*.xml)", "All Files (*)"]
						fileMode: FileDialog.OpenFile

						onAccepted: {
							const path = normalizePath(linearizationUploadDialog.file.toString())

							if (!root.appState.selectedPrinter || root.appState.selectedPrinter.length <= 0)
								return

							const family = root.currentFamilyKey()
							colorManager.setPrinterFamilyLinearizationPath(root.appState.selectedPrinter, family, path)

							Qt.callLater(root.syncUIFromAppState)
							toast.show(strings.trKey("printerSetup.toast.linearizationUpdated"))
						}
					}
                }
            }

            // =========================
            // Selected Printer Details
            // =========================
            Pane {
                Layout.fillWidth: true
                padding: 16
                visible: hasPrinterSelected()

                background: Rectangle {
                    color: theme.surface
                    radius: 12
                    border.width: 1
                    border.color: theme.divider
                }

                ColumnLayout {
					anchors.horizontalCenter: parent.horizontalCenter
					width: theme.boundedWidth(parent.width, 640)
					spacing: 10

                    Label {
                        text: strings.trKey("printerSetup.selectedPrinterDetails")
                        color: theme.text
                        font.pixelSize: theme.sectionTitleSize
                        font.weight: Font.Medium
                        Layout.alignment: Qt.AlignHCenter
                    }

                    Rectangle { height: 1; Layout.fillWidth: true; color: theme.divider; opacity: 0.8 }

                    Label { text: strings.trKey("printerSetup.details.name") + appState.selectedPrinter; color: theme.text }
                    Label {
                        text: strings.trKey("printerSetup.details.nocaiPrinter")
                              + (appState.usingSimulatedPrinter
                                 ? strings.trKey("common.yes") : strings.trKey("common.no"))
                        color: theme.text
                    }

                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: theme.text
                        text: strings.trKey("printerSetup.details.supportedResolutions") +
                              (appState.usingSimulatedPrinter
                               ? (nocaiPrinterCapabilities[appState.selectedPrinter] && nocaiPrinterCapabilities[appState.selectedPrinter].resolutions
                                  ? nocaiPrinterCapabilities[appState.selectedPrinter].resolutions.join(", ")
                                  : "(unknown)")
                               : printJobOutput.supportedResolutions().join(", "))
                    }

                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: theme.text
                        text: strings.trKey("printerSetup.details.mediaSizes") +
                              (appState.usingSimulatedPrinter
                               ? (nocaiPrinterCapabilities[appState.selectedPrinter] && nocaiPrinterCapabilities[appState.selectedPrinter].mediaSizes
                                  ? nocaiPrinterCapabilities[appState.selectedPrinter].mediaSizes.join(", ")
                                  : "(unknown)")
                               : printJobOutput.supportedMediaSizes().join(", "))
                    }

                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: theme.text
                        text: strings.trKey("printerSetup.details.duplexModes") +
                              (appState.usingSimulatedPrinter
                               ? (nocaiPrinterCapabilities[appState.selectedPrinter] && nocaiPrinterCapabilities[appState.selectedPrinter].duplexModes
                                  ? nocaiPrinterCapabilities[appState.selectedPrinter].duplexModes.join(", ")
                                  : "(unknown)")
                               : printJobOutput.supportedDuplexModes().join(", "))
                    }

                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: theme.text
                        text: strings.trKey("printerSetup.details.colorModes") +
                              (appState.usingSimulatedPrinter
                               ? (nocaiPrinterCapabilities[appState.selectedPrinter] && nocaiPrinterCapabilities[appState.selectedPrinter].colorModes
                                  ? nocaiPrinterCapabilities[appState.selectedPrinter].colorModes.join(", ")
                                  : "(unknown)")
                               : printJobOutput.supportedColorModes().join(", "))
                    }

	                    Label {
	                        visible: appState.usingSimulatedPrinter
	                        Layout.fillWidth: true
	                        wrapMode: Text.WordWrap
	                        color: theme.subtext
                        text: appState.usingMultiInkPrinter
	                              ? (strings.trKey("printerSetup.details.inkLayout")
                                     + appState.multiInkInkMode
                                     + strings.trKey("printerSetup.details.channelsSuffix"))
	                              : strings.trKey("printerSetup.details.x33InkLayout")
	                    }

	                }
	            }

            Item { height: 6 }
        }
    }

    Toast {
        id: toast
        parent: Overlay.overlay
    }
}
