import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import Qt.labs.platform
import "."

Page {
    id: root
    required property var stackView
    required property var appState
    required property Theme theme
    
    background: Rectangle {
    	color: theme.bg
    }

    // --- Model for ICC dropdowns (same pattern as PrinterSetupView) ---
    ListModel { id: iccProfileModel }

    // Tracks which dropdown gets updated after file selection
    property string iccDialogTarget: "defaultOutput" // "defaultOutput" | "defaultInput" | "printerOverride"

    // Multi-ink per-mode params state
    property int selectedMode: 6            // current mode shown in editor (auto-synced)
    property var params: ({})               // editable map for selectedMode
    
    // Centered card width helpers
	readonly property int cardWidthNarrow: 520
	readonly property int cardWidthMedium: 640
	readonly property int cardWidthWide: 760
	
	property var whiteModeOptions: [
		{ label: "Off", value: 0 },
		{ label: "Auto Underbase", value: 1 },
		{ label: "Flood", value: 2 },
		{ label: "Plate", value: 3 }
	]

	property var varnishModeOptions: [
		{ label: "Off", value: 0 },
		{ label: "Over Printed Area", value: 1 },
		{ label: "Flood", value: 2 },
		{ label: "Plate", value: 3 }
	]

	property var specialtyMaskOptions: [
		{ label: "White Mask (w)", value: "w" },
		{ label: "Varnish Mask (v)", value: "v" },
		{ label: "Reuse Black Mask (k)", value: "k" },
		{ label: "Reuse Yellow Mask (y)", value: "y" }
	]

	function comboIndexForValue(model, value, fallbackIndex) {
		for (let i = 0; i < model.length; ++i) {
		    if (model[i].value === value)
		        return i
		}
		return fallbackIndex === undefined ? 0 : fallbackIndex
	}

	function modeHasWhite(mode) {
		return mode === 5 || mode === 7 || mode === 10
	}

	function modeHasVarnish(mode) {
		return mode === 10
	}

    function isMultiInkPrinterName(name) {
        return name === "X-36NC (Photo Printer)"
    }

    function hasPrinterSelected() {
        return appState.selectedPrinter && appState.selectedPrinter.length > 0
    }

    function activeBackend() {
        if (!hasPrinterSelected()) return null
        return isMultiInkPrinterName(appState.selectedPrinter) ? printJobMultiInk : printJobCMYK
    }

    function normalizePath(urlOrPath) {
        const s = (urlOrPath || "").toString()
        return s.startsWith("file://") ? s.slice(7) : s
    }

    function loadParamsForMode(mode) {
        selectedMode = mode
        params = colorManager.getMultiInkParams(mode) || ({})
        syncModeComboIndex()
    }

    function setParam(k, v) {
		// real clone so QML bindings update reliably
		var p = Object.assign({}, params || {})
		p[k] = v
		params = p
		colorManager.setMultiInkParams(selectedMode, p)
	}

    function isModeSupportedForParams(mode) {
        return mode === 5 || mode === 6 || mode === 7 || mode === 8 || mode === 10
    }

    function syncSelectedModeFromPrinter() {
        // If we are on the multi-ink printer, use the backend's active inkMode()
        if (!hasPrinterSelected()) return

        if (isMultiInkPrinterName(appState.selectedPrinter)) {
            const m = printJobMultiInk.inkMode()
            if (m === 4 || m === 5 || m === 6 || m === 7 || m === 8 || m === 10) {
                selectedMode = m
            } else {
                selectedMode = 6
            }
        } else {
            // Non-multiink printers: keep editor default
            selectedMode = 6
        }

        params = colorManager.getMultiInkParams(selectedMode) || ({})
        syncModeComboIndex()
    }

    function syncModeComboIndex() {
        if (!modeCombo || !modeCombo.model) return
        for (let i = 0; i < modeCombo.model.length; ++i) {
            if (modeCombo.model[i].value === selectedMode) {
                modeCombo.currentIndex = i
                return
            }
        }
        // if selectedMode not in model (shouldn't happen now), show nothing selected
        modeCombo.currentIndex = -1
    }
    
    function currentFamilyKey() {
		return colorManager.outputProfileFamilyForInkMode(selectedMode)
	}

	function familyDefaultOutputPath() {
		return colorManager.familyDefaultOutputProfile(currentFamilyKey())
	}

	function printerFamilyOutputPath() {
		if (!appState.selectedPrinter || appState.selectedPrinter.length === 0)
		    return ""
		return colorManager.printerFamilyOutputProfile(appState.selectedPrinter, currentFamilyKey())
	}

	function effectiveFamilyOutputPath() {
		if (!appState.selectedPrinter || appState.selectedPrinter.length === 0)
		    return ""
		return colorManager.effectiveOutputProfileForPrinterAndInkMode(appState.selectedPrinter, selectedMode)
	}

	function familyDefaultLinearizationPath() {
		return colorManager.familyDefaultLinearizationPath(currentFamilyKey())
	}

	function printerFamilyLinearizationPath() {
		if (!appState.selectedPrinter || appState.selectedPrinter.length === 0)
		    return ""
		return colorManager.printerFamilyLinearizationPath(appState.selectedPrinter, currentFamilyKey())
	}

	function effectiveFamilyLinearizationPath() {
		if (!appState.selectedPrinter || appState.selectedPrinter.length === 0)
		    return colorManager.linearizationDataPath || ""
		return colorManager.effectiveLinearizationPathForPrinterAndInkMode(appState.selectedPrinter, selectedMode)
	}

    function rebuildICCModelFromBackend() {
        if (!hasPrinterSelected()) return

        iccProfileModel.clear()

        // Make sure assets exist before asking for profiles
        if (isMultiInkPrinterName(appState.selectedPrinter)) {
            printJobMultiInk.prepareAssets()
        } else {
            printJobCMYK.prepareAssets()
        }

        const backend = activeBackend()
        const profiles = backend.getAvailableICCProfiles()
        for (let i = 0; i < profiles.length; ++i) {
            iccProfileModel.append(profiles[i])
        }
    }

    function indexOfPath(path) {
        for (let i = 0; i < iccProfileModel.count; ++i) {
            if (iccProfileModel.get(i).path === path)
                return i
        }
        return -1
    }

	function syncDropdownsFromManager() {
		defaultInputDropdown.currentIndex = indexOfPath(colorManager.defaultInputProfile)

		familyDefaultOutputDropdown.currentIndex = indexOfPath(familyDefaultOutputPath())
		printerFamilyOutputDropdown.currentIndex = indexOfPath(printerFamilyOutputPath())
	}

    function doSave() {
        const ok = colorManager.save()
        console.log("[ColorManagementView] Save clicked ->", ok ? "OK" : "FAILED")
        toast.show(ok ? strings.trKey("color.toast.saved") : strings.trKey("color.toast.saveFailed"))
    }

    function doReset() {
        colorManager.resetToDefaults()
        console.log("[ColorManagementView] Reset to defaults")
        toast.show(strings.trKey("color.toast.defaultsRestored"))
        syncDropdownsFromManager()
        // Reload params in case defaults changed
        syncSelectedModeFromPrinter()
    }

    Component.onCompleted: {
        colorManager.selectedPrinter = appState.selectedPrinter
        rebuildICCModelFromBackend()
        syncDropdownsFromManager()

        // NEW: auto-sync mode from actual printer selection
        syncSelectedModeFromPrinter()
    }

    Connections {
        target: appState
        function onSelectedPrinterChanged() {
            colorManager.selectedPrinter = appState.selectedPrinter
            rebuildICCModelFromBackend()
            syncDropdownsFromManager()

            // NEW: re-sync mode whenever printer changes
            syncSelectedModeFromPrinter()
        }
    }

	Rectangle {
		id: headerBar
		anchors.top: parent.top
		anchors.left: parent.left
		anchors.right: parent.right
		height: 60
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
			        onClicked: root.stackView.pop()
		    }

		    Item { Layout.fillWidth: true }

		    Label {
		        text: strings.trKey("color.title")
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
			        onClicked: root.doSave()
		    }
		}
	}

    // --- Main content ---
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
			// Reset to Defaults
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

				RowLayout {
					anchors.fill: parent
					spacing: 10

					ColumnLayout {
						Layout.fillWidth: true
						spacing: 2

						Label {
							text: strings.trKey("color.resetDefaults")
							color: theme.text
							font.pixelSize: 16
							font.weight: Font.Medium
						}

						Label {
							text: strings.trKey("color.resetDefaults.help")
							color: theme.subtext
							wrapMode: Text.WordWrap
							Layout.fillWidth: true
						}
					}

					ThemedButton {
						text: strings.trKey("common.reset")
						theme: root.theme
						padding: 12
						font.pixelSize: 15
						onClicked: root.doReset()
					}
				}
			}

			// =========================
			// ICC Profiles
			// =========================
			Pane {
				Layout.fillWidth: true
				padding: 16

				background: Rectangle {
					color: theme.surface
					radius: 12
					border.width: 1
					border.color: theme.divider
				}

				ColumnLayout {
					anchors.horizontalCenter: parent.horizontalCenter
					width: theme.boundedWidth(parent.width, root.cardWidthNarrow)
					spacing: 12

					Label {
						text: strings.trKey("color.iccProfiles")
						color: theme.text
						font.pixelSize: 18
						font.weight: Font.Medium
						Layout.alignment: Qt.AlignHCenter
					}

					Rectangle { height: 1; Layout.fillWidth: true; color: theme.divider; opacity: 0.8 }

					Label { text: strings.trKey("color.familyDefaultOutputIcc"); color: theme.text; font.bold: true }

					RowLayout {
						Layout.fillWidth: true
						spacing: 10

						ComboBox {
						    id: familyDefaultOutputDropdown
						    Layout.fillWidth: true
						    model: iccProfileModel
						    textRole: "name"
						    displayText: currentIndex >= 0 ? currentText : strings.trKey("common.none")

						    onActivated: {
						        if (iccProfileModel.count <= 0) return
						        const selected = iccProfileModel.get(currentIndex)
						        colorManager.setFamilyDefaultOutputProfile(currentFamilyKey(), selected.path)
						    }
						}

						ThemedButton {
						    text: strings.trKey("common.upload")
						    theme: root.theme
						    onClicked: { iccDialogTarget = "familyDefaultOutput"; iccUploadDialog.open() }
						}
					}

					Label { text: strings.trKey("color.defaultInputIcc"); color: theme.text; font.bold: true }

					RowLayout {
						Layout.fillWidth: true
						spacing: 10

						ComboBox {
						    id: defaultInputDropdown
						    Layout.fillWidth: true
						    model: iccProfileModel
						    textRole: "name"
						    displayText: currentIndex >= 0 ? currentText : strings.trKey("common.none")

						    onActivated: {
						        if (iccProfileModel.count <= 0) return
						        const selected = iccProfileModel.get(currentIndex)
						        colorManager.defaultInputProfile = selected.path

						        const backend = activeBackend()
						        if (backend) backend.setDefaultInputCMYKProfile(selected.path)
						    }
						}

						ThemedButton {
						    text: strings.trKey("common.upload")
						    theme: root.theme
						    onClicked: { iccDialogTarget = "defaultInput"; iccUploadDialog.open() }
						}
					}

					Rectangle { height: 1; Layout.fillWidth: true; color: theme.divider; opacity: 0.7 }

					RowLayout {
						Layout.fillWidth: true
						spacing: 10

						Label { text: strings.trKey("color.selectedPrinter"); color: theme.subtext; Layout.preferredWidth: theme.formLabelWidth(parent.width, 160) }
						Label {
						    Layout.fillWidth: true
						    text: appState.selectedPrinter === "" ? strings.trKey("common.none") : appState.selectedPrinter
						    color: theme.text
						    wrapMode: Text.WrapAnywhere
						    opacity: 0.95
						}
					}

					Label { text: strings.trKey("color.printerFamilyOutputOverride"); color: theme.text; font.bold: true }

					RowLayout {
						Layout.fillWidth: true
						spacing: 10

						ComboBox {
						    id: printerFamilyOutputDropdown
						    Layout.fillWidth: true
						    model: iccProfileModel
						    textRole: "name"
						    displayText: currentIndex >= 0 ? currentText : strings.trKey("common.none")
						    enabled: appState.selectedPrinter && appState.selectedPrinter.length > 0

						    onActivated: {
						        if (!enabled || iccProfileModel.count <= 0) return
						        const selected = iccProfileModel.get(currentIndex)
						        colorManager.setPrinterFamilyOutputProfile(appState.selectedPrinter, currentFamilyKey(), selected.path)
						    }
						}

						ThemedButton {
						    text: strings.trKey("common.upload")
						    theme: root.theme
						    enabled: appState.selectedPrinter && appState.selectedPrinter.length > 0
						    onClicked: { iccDialogTarget = "printerFamilyOutput"; iccUploadDialog.open() }
						}
					}

					RowLayout {
						Layout.fillWidth: true
						spacing: 10

						Label { text: strings.trKey("color.effectiveOutput"); color: theme.subtext; Layout.preferredWidth: theme.formLabelWidth(parent.width, 160) }
						Label {
						    Layout.fillWidth: true
						    text: effectiveFamilyOutputPath() === "" ? strings.trKey("common.none") : effectiveFamilyOutputPath()
						    color: theme.text
						    wrapMode: Text.WrapAnywhere
						}
					}

					FileDialog {
						id: iccUploadDialog
						title: "Select ICC Profile"
						nameFilters: ["ICC Profiles (*.icc *.icm)", "All Files (*)"]
						fileMode: FileDialog.OpenFile

						onAccepted: {
						    const path = normalizePath(iccUploadDialog.file.toString())
						    const name = path.split("/").pop()

						    const backend = activeBackend()
						    iccProfileModel.append({ name: name, path: path })
						    if (backend) backend.addICCProfile(name, path)

						    if (iccDialogTarget === "familyDefaultOutput") {
						        familyDefaultOutputDropdown.currentIndex = iccProfileModel.count - 1
						        colorManager.setFamilyDefaultOutputProfile(currentFamilyKey(), path)
						    } else if (iccDialogTarget === "defaultInput") {
						        defaultInputDropdown.currentIndex = iccProfileModel.count - 1
						        colorManager.defaultInputProfile = path
						    } else {
						        printerFamilyOutputDropdown.currentIndex = iccProfileModel.count - 1
						        colorManager.setPrinterFamilyOutputProfile(appState.selectedPrinter, currentFamilyKey(), path)
						    }

						    toast.show("ICC added: " + name)
						}
					}
				}
			}
			
			
			Pane {
				Layout.fillWidth: true
				padding: 16

				background: Rectangle {
					color: theme.surface
					radius: 12
					border.width: 1
					border.color: theme.divider
				}

				ColumnLayout {
					anchors.horizontalCenter: parent.horizontalCenter
					width: theme.boundedWidth(parent.width, root.cardWidthMedium)
					spacing: 12

					Label {
						text: strings.trKey("color.linearization")
						color: theme.text
						font.pixelSize: 18
						font.weight: Font.Medium
						Layout.alignment: Qt.AlignHCenter
					}

					Rectangle { height: 1; Layout.fillWidth: true; color: theme.divider; opacity: 0.8 }

					RowLayout {
						Layout.fillWidth: true
						Label {
						    text: strings.trKey("color.enableLinearization")
						    color: theme.text
						    wrapMode: Text.WordWrap
						    Layout.fillWidth: true
						}
						Switch {
                            Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
						    checked: colorManager.linearizationEnabled
						    onToggled: colorManager.linearizationEnabled = checked
						}
					}

					RowLayout {
						Layout.fillWidth: true
						spacing: 10

						Label { text: strings.trKey("color.defaultXml"); color: theme.text; Layout.preferredWidth: theme.formLabelWidth(parent.width, 180) }

						Label {
						    Layout.fillWidth: true
						    text: familyDefaultLinearizationPath() && familyDefaultLinearizationPath().length > 0
						          ? familyDefaultLinearizationPath()
						          : strings.trKey("common.none")
						    color: theme.subtext
						    wrapMode: Text.WrapAnywhere
						}

						ThemedButton {
						    text: strings.trKey("common.load")
						    theme: root.theme
						    onClicked: familyLinearizationDialog.open()
						}

						ThemedButton {
						    text: strings.trKey("common.clear")
						    theme: root.theme
						    enabled: familyDefaultLinearizationPath().length > 0
						    onClicked: {
						        colorManager.setFamilyDefaultLinearizationPath(currentFamilyKey(), "")
						        toast.show("Default linearization cleared.")
						    }
						}
					}

					RowLayout {
						Layout.fillWidth: true
						spacing: 10

						Label { text: strings.trKey("color.overrideXml"); color: theme.text; Layout.preferredWidth: theme.formLabelWidth(parent.width, 180) }

						Label {
						    Layout.fillWidth: true
						    text: printerFamilyLinearizationPath() && printerFamilyLinearizationPath().length > 0
						          ? printerFamilyLinearizationPath()
						          : strings.trKey("common.none")
						    color: theme.subtext
						    wrapMode: Text.WrapAnywhere
						}

						ThemedButton {
						    text: strings.trKey("common.load")
						    theme: root.theme
						    enabled: appState.selectedPrinter && appState.selectedPrinter.length > 0
						    onClicked: printerLinearizationDialog.open()
						}

						ThemedButton {
						    text: strings.trKey("common.clear")
						    theme: root.theme
						    enabled: printerFamilyLinearizationPath().length > 0
						    onClicked: {
						        if (!appState.selectedPrinter || appState.selectedPrinter.length === 0)
						            return
						        colorManager.setPrinterFamilyLinearizationPath(appState.selectedPrinter, currentFamilyKey(), "")
						        toast.show("Printer linearization override cleared.")
						    }
						}
					}

					RowLayout {
						Layout.fillWidth: true
						spacing: 10

						Label { text: strings.trKey("color.effectiveXml"); color: theme.subtext; Layout.preferredWidth: theme.formLabelWidth(parent.width, 180) }

						Label {
						    Layout.fillWidth: true
						    text: effectiveFamilyLinearizationPath() && effectiveFamilyLinearizationPath().length > 0
						          ? effectiveFamilyLinearizationPath()
						          : strings.trKey("common.none")
						    color: theme.text
						    wrapMode: Text.WrapAnywhere
						}
					}

					Label {
						text: strings.trKey("color.defaultXmlHelp")
						color: theme.subtext
						wrapMode: Text.WordWrap
						Layout.fillWidth: true
					}

					FileDialog {
						id: familyLinearizationDialog
						title: "Select Default Linearization XML"
						nameFilters: ["Linearization XML (*.xml)", "All Files (*)"]
						fileMode: FileDialog.OpenFile

						onAccepted: {
						    const path = normalizePath(familyLinearizationDialog.file.toString())
						    colorManager.setFamilyDefaultLinearizationPath(currentFamilyKey(), path)
						    toast.show("Default linearization updated.")
						}
					}

					FileDialog {
						id: printerLinearizationDialog
						title: "Select Printer Override Linearization XML"
						nameFilters: ["Linearization XML (*.xml)", "All Files (*)"]
						fileMode: FileDialog.OpenFile

						onAccepted: {
						    const path = normalizePath(printerLinearizationDialog.file.toString())
						    if (!appState.selectedPrinter || appState.selectedPrinter.length === 0)
						        return
						    colorManager.setPrinterFamilyLinearizationPath(appState.selectedPrinter, currentFamilyKey(), path)
						    toast.show("Printer linearization override updated.")
						}
					}
				}
			}


			// =========================
			// Dot Strategy Defaults
			// =========================
			Pane {
				Layout.fillWidth: true
				padding: 16

				background: Rectangle {
					color: theme.surface
					radius: 12
					border.width: 1
					border.color: theme.divider
				}

				ColumnLayout {
					anchors.horizontalCenter: parent.horizontalCenter
					width: theme.boundedWidth(parent.width, root.cardWidthMedium)
					spacing: 12

					Label {
						text: strings.trKey("color.dotStrategyDefaults")
						color: theme.text
						font.pixelSize: 18
						font.weight: Font.Medium
						Layout.alignment: Qt.AlignHCenter
					}

					Rectangle { height: 1; Layout.fillWidth: true; color: theme.divider; opacity: 0.8 }

					RowLayout {
						Layout.fillWidth: true
						Label { text: strings.trKey("color.minInkThreshold"); color: theme.text; Layout.preferredWidth: theme.formLabelWidth(parent.width, 180) }
						SpinBox {
						    Layout.fillWidth: true
						    from: 0; to: 255
						    value: colorManager.minInkThreshold
						    editable: true
						    validator: IntValidator { bottom: 0; top: 255 }
						    onValueModified: colorManager.minInkThreshold = value
						}
					}

					RowLayout {
						Layout.fillWidth: true
						Label { text: strings.trKey("color.smallDotThreshold"); color: theme.text; Layout.preferredWidth: theme.formLabelWidth(parent.width, 180) }
						SpinBox {
						    Layout.fillWidth: true
						    from: 0; to: 255
						    value: colorManager.smallDotThreshold
						    editable: true
						    validator: IntValidator { bottom: 0; top: 255 }
						    onValueModified: colorManager.smallDotThreshold = value
						}
					}

					RowLayout {
						Layout.fillWidth: true
						Label { text: strings.trKey("color.mediumDotThreshold"); color: theme.text; Layout.preferredWidth: theme.formLabelWidth(parent.width, 180) }
						SpinBox {
						    Layout.fillWidth: true
						    from: 0; to: 255
						    value: colorManager.medDotThreshold
						    editable: true
						    validator: IntValidator { bottom: 0; top: 255 }
						    onValueModified: colorManager.medDotThreshold = value
						}
					}

					RowLayout {
						Layout.fillWidth: true
						Label { text: strings.trKey("color.enablePromotion"); color: theme.text; wrapMode: Text.WordWrap; Layout.fillWidth: true }
						Switch {
                            Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
						    checked: colorManager.enablePromotion
						    onToggled: colorManager.enablePromotion = checked
						}
					}

					Rectangle { height: 1; Layout.fillWidth: true; color: theme.divider; opacity: 0.7 }

					RowLayout {
						Layout.fillWidth: true
						Label { text: strings.trKey("color.cmyFloorRange"); color: theme.text; Layout.preferredWidth: theme.formLabelWidth(parent.width, 180) }
						SpinBox {
						    Layout.fillWidth: true
						    from: 0; to: 64
						    value: colorManager.floorRangeCMY
						    editable: true
						    validator: IntValidator { bottom: 0; top: 64 }
						    onValueModified: colorManager.floorRangeCMY = value
						}
					}

					RowLayout {
						Layout.fillWidth: true
						Label { text: strings.trKey("color.cmyFloorMax"); color: theme.text; Layout.preferredWidth: theme.formLabelWidth(parent.width, 180) }
						SpinBox {
						    Layout.fillWidth: true
						    from: 0; to: 8
						    value: colorManager.floorMaxCMY
						    editable: true
						    validator: IntValidator { bottom: 0; top: 8 }
						    onValueModified: colorManager.floorMaxCMY = value
						}
					}

					RowLayout {
						Layout.fillWidth: true
						Label { text: strings.trKey("color.kFloorRange"); color: theme.text; Layout.preferredWidth: theme.formLabelWidth(parent.width, 180) }
						SpinBox {
						    Layout.fillWidth: true
						    from: 0; to: 64
						    value: colorManager.floorRangeK
						    editable: true
						    validator: IntValidator { bottom: 0; top: 64 }
						    onValueModified: colorManager.floorRangeK = value
						}
					}

					RowLayout {
						Layout.fillWidth: true
						Label { text: strings.trKey("color.kFloorMax"); color: theme.text; Layout.preferredWidth: theme.formLabelWidth(parent.width, 180) }
						SpinBox {
						    Layout.fillWidth: true
						    from: 0; to: 8
						    value: colorManager.floorMaxK
						    editable: true
						    validator: IntValidator { bottom: 0; top: 8 }
						    onValueModified: colorManager.floorMaxK = value
						}
					}

					RowLayout {
						Layout.fillWidth: true
						Label { text: strings.trKey("color.dotSwap"); color: theme.text; wrapMode: Text.WordWrap; Layout.fillWidth: true }
						Switch {
                            Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
						    checked: colorManager.enableDotSwap
						    onToggled: colorManager.enableDotSwap = checked
						}
					}
				}
			}


            // =========================
            // Multi-Ink Thresholds (Per-Mode)
            // =========================
			Pane {
				Layout.fillWidth: true
				padding: 16
				enabled: true

				background: Rectangle {
					color: theme.surface
					radius: 12
					border.width: 1
					border.color: theme.divider
					opacity: isModeSupportedForParams(root.selectedMode) ? 1.0 : 0.6
				}

				ColumnLayout {
					anchors.horizontalCenter: parent.horizontalCenter
					width: theme.boundedWidth(parent.width, root.cardWidthWide)
					spacing: 12

					Label {
						text: strings.trKey("color.multiInkThresholds")
						color: theme.text
						font.pixelSize: 18
						font.weight: Font.Medium
						Layout.alignment: Qt.AlignHCenter
					}

					Rectangle { height: 1; Layout.fillWidth: true; color: theme.divider; opacity: 0.8 }

					// Hint when disabled
					Label {
						visible: !isModeSupportedForParams(root.selectedMode)
						text: strings.trKey("color.multiInkThresholds.help")
						color: theme.subtext
						wrapMode: Text.WordWrap
						Layout.fillWidth: true
					}

                    RowLayout {
						Layout.fillWidth: true
						spacing: 10

						Label { text: strings.trKey("color.inkMode"); color: theme.text; Layout.preferredWidth: theme.formLabelWidth(parent.width, 180) }

                        ComboBox {
                            id: modeCombo
                            Layout.fillWidth: true

                            model: [
                                { label: "4 – YMCK (no per-mode params)", value: 4 },
								{ label: "5 – YMCK + W", value: 5 },
                                { label: "6 – YMCK + Lm + Lc", value: 6 },
                                { label: "7 – YMCK + Lm + Lc + W", value: 7 },
                                { label: "8 – YMCK + Lm + Lc + Lk + LLk", value: 8 },
                                { label: "10 – YMCK + Lm + Lc + Lk + LLk + W + V", value: 10 }
                            ]
                            textRole: "label"
                            onActivated: root.loadParamsForMode(model[currentIndex].value)
                        }
                    }

					Rectangle {
						visible: selectedMode >= 6
						height: 1
						Layout.fillWidth: true
						color: theme.divider
						opacity: 0.65
					}

                    // --- Light ink controls (only meaningful for 6/7/8/10) ---
					ColumnLayout {
						visible: selectedMode >= 6
						Layout.fillWidth: true
						spacing: 10

						// --- Light ink splits ---
						Label { text: "Light Ink Split (C→C/Lc, M→M/Lm)"; font.bold: true; color: theme.text }

						// C Light Start
						RowLayout {
							Layout.fillWidth: true
							Label { text: "C Light Start"; Layout.preferredWidth: theme.formLabelWidth(parent.width, 180); color: theme.text }
							SpinBox {
								Layout.fillWidth: true
								from: 0; to: 255
								value: params.cLightStart ?? 0
								editable: true
								validator: IntValidator { bottom: 0; top: 255 }
								onValueModified: root.setParam("cLightStart", value)
							}
						}

						// C Light End
						RowLayout {
							Layout.fillWidth: true
							Label { text: "C Light End"; Layout.preferredWidth: theme.formLabelWidth(parent.width, 180); color: theme.text }
							SpinBox {
								Layout.fillWidth: true
								from: 0; to: 255
								value: params.cLightEnd ?? 0
								editable: true
								validator: IntValidator { bottom: 0; top: 255 }
								onValueModified: root.setParam("cLightEnd", value)
							}
						}

						// M Light Start
						RowLayout {
							Layout.fillWidth: true
							Label { text: "M Light Start"; Layout.preferredWidth: theme.formLabelWidth(parent.width, 180); color: theme.text }
							SpinBox {
								Layout.fillWidth: true
								from: 0; to: 255
								value: params.mLightStart ?? 0
								editable: true
								validator: IntValidator { bottom: 0; top: 255 }
								onValueModified: root.setParam("mLightStart", value)
							}
						}

						// M Light End
						RowLayout {
							Layout.fillWidth: true
							Label { text: "M Light End"; Layout.preferredWidth: theme.formLabelWidth(parent.width, 180); color: theme.text }
							SpinBox {
								Layout.fillWidth: true
								from: 0; to: 255
								value: params.mLightEnd ?? 0
								editable: true
								validator: IntValidator { bottom: 0; top: 255 }
								onValueModified: root.setParam("mLightEnd", value)
							}
						}

						// --- Light ink min threshold override ---
						Rectangle { height: 1; Layout.fillWidth: true; color: theme.divider; opacity: 0.45 }

						Label { text: "Light Ink Threshold Override"; font.bold: true; color: theme.text }

						RowLayout {
							Layout.fillWidth: true
							Label {
								text: "Override light inks minInkThreshold"
								color: theme.text
								wrapMode: Text.WordWrap
								Layout.fillWidth: true
							}
							Switch {
                                Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
								checked: params.useLightInkMinThresholdOverride ?? true
								onToggled: root.setParam("useLightInkMinThresholdOverride", checked)
							}
						}

						RowLayout {
							Layout.fillWidth: true
							Label { text: "Light Ink Min Threshold"; Layout.preferredWidth: theme.formLabelWidth(parent.width, 180); color: theme.text }
							SpinBox {
								Layout.fillWidth: true
								from: 0; to: 254
								enabled: (params.useLightInkMinThresholdOverride ?? true)
								value: params.lightInkMinThreshold ?? 2
								editable: true
								validator: IntValidator { bottom: 0; top: 254 }
								onValueModified: root.setParam("lightInkMinThreshold", value)
							}
						}
					}

                    // --- K split only for 8 & 10 ---
                    Rectangle {
                        visible: selectedMode === 8 || selectedMode === 10
                        height: 1
                        Layout.fillWidth: true
                        color: theme.divider
                        opacity: 0.65
                    }

                    Label {
                        visible: selectedMode === 8 || selectedMode === 10
                        text: "K Split (LLk→Lk→K)"
                        font.bold: true
                    }

                    ColumnLayout {
                        visible: selectedMode === 8 || selectedMode === 10
                        Layout.fillWidth: true
                        spacing: 10

                        // T1 Start
                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: "T1 Start"; Layout.preferredWidth: theme.formLabelWidth(parent.width, 180); color: theme.text }
                            SpinBox {
                                Layout.fillWidth: true
                                from: 0; to: 255
                                value: params.kT1Start ?? 0
                                editable: true
                                validator: IntValidator { bottom: 0; top: 255 }
                                onValueModified: root.setParam("kT1Start", value)
                            }
                        }

                        // T1 End
                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: "T1 End"; Layout.preferredWidth: theme.formLabelWidth(parent.width, 180); color: theme.text }
                            SpinBox {
                                Layout.fillWidth: true
                                from: 0; to: 255
                                value: params.kT1End ?? 0
                                editable: true
                                validator: IntValidator { bottom: 0; top: 255 }
                                onValueModified: root.setParam("kT1End", value)
                            }
                        }

                        // T2 Start
                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: "T2 Start"; Layout.preferredWidth: theme.formLabelWidth(parent.width, 180); color: theme.text }
                            SpinBox {
                                Layout.fillWidth: true
                                from: 0; to: 255
                                value: params.kT2Start ?? 0
                                editable: true
                                validator: IntValidator { bottom: 0; top: 255 }
                                onValueModified: root.setParam("kT2Start", value)
                            }
                        }

                        // T2 End
                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: "T2 End"; Layout.preferredWidth: theme.formLabelWidth(parent.width, 180); color: theme.text }
                            SpinBox {
                                Layout.fillWidth: true
                                from: 0; to: 255
                                value: params.kT2End ?? 0
                                editable: true
                                validator: IntValidator { bottom: 0; top: 255 }
                                onValueModified: root.setParam("kT2End", value)
                            }
                        }
                    }

                    // --- Neutral / GCR (modes 5/6/7/8/10) ---
					Rectangle {
						visible: selectedMode === 5 || selectedMode === 6 || selectedMode === 7 || selectedMode === 8 || selectedMode === 10
						height: 1
						Layout.fillWidth: true
						color: theme.divider
						opacity: 0.65
					}

					Label {
						visible: selectedMode === 5 || selectedMode === 6 || selectedMode === 7 || selectedMode === 8 || selectedMode === 10
						text: "Neutral / GCR"
						font.bold: true
					}

					ColumnLayout {
						visible: selectedMode === 5 || selectedMode === 6 || selectedMode === 7 || selectedMode === 8 || selectedMode === 10
						Layout.fillWidth: true
						spacing: 10
						
						RowLayout {
							Layout.fillWidth: true
							Label { text: "Enable GCR"; color: theme.text; wrapMode: Text.WordWrap; Layout.fillWidth: true }
							Switch {
                                Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
								checked: params.gcrEnabled ?? false
								onToggled: root.setParam("gcrEnabled", checked)
							}
						}

						RowLayout {
							Layout.fillWidth: true
							Label { text: "Neutral Gate"; Layout.preferredWidth: theme.formLabelWidth(parent.width, 180); color: theme.text }
							SpinBox {
								Layout.fillWidth: true
								from: 0; to: 255
								value: params.neutralGate ?? 14
								editable: true
								validator: IntValidator { bottom: 0; top: 255 }
								onValueModified: root.setParam("neutralGate", value)
							}
						}
						
						RowLayout {
							Layout.fillWidth: true
							Label { text: "GCR Max Tone"; Layout.preferredWidth: theme.formLabelWidth(parent.width, 180); color: theme.text }
							SpinBox {
								Layout.fillWidth: true
								from: 0; to: 255
								value: params.gcrMaxTone ?? 180
								editable: true
								validator: IntValidator { bottom: 0; top: 255 }
								onValueModified: root.setParam("gcrMaxTone", value)
							}
						}

						RowLayout {
							Layout.fillWidth: true
							Label { text: "GCR Strength"; Layout.preferredWidth: theme.formLabelWidth(parent.width, 180); color: theme.text }
							SpinBox {
								Layout.fillWidth: true
								from: 0; to: 255
								value: params.gcrStrength ?? 160
								editable: true
								validator: IntValidator { bottom: 0; top: 255 }
								onValueModified: root.setParam("gcrStrength", value)
							}
						}

						RowLayout {
							Layout.fillWidth: true
							Label { text: "K Gain"; Layout.preferredWidth: theme.formLabelWidth(parent.width, 180); color: theme.text }
							SpinBox {
								Layout.fillWidth: true
								from: 0; to: 255
								value: params.kGain ?? 220
								editable: true
								validator: IntValidator { bottom: 0; top: 255 }
								onValueModified: root.setParam("kGain", value)
							}
						}

						RowLayout {
							Layout.fillWidth: true
							Label { text: "K Min in Neutral"; Layout.preferredWidth: theme.formLabelWidth(parent.width, 180); color: theme.text }
							SpinBox {
								Layout.fillWidth: true
								from: 0; to: 255
								value: params.kMinInNeutral ?? 6
								editable: true
								validator: IntValidator { bottom: 0; top: 255 }
								onValueModified: root.setParam("kMinInNeutral", value)
							}
						}

						// Fade % is currently only applied in 8 & 10 in the backend (applyNeutralLightInkFade)
						Label {
							visible: selectedMode === 8 || selectedMode === 10
							text: "Light Ink Fade in Neutrals (Color Modes 8/10)"
							opacity: 0.85
						}

						RowLayout {
							visible: selectedMode === 8 || selectedMode === 10
							Layout.fillWidth: true
							Label { text: "LC Fade %"; Layout.preferredWidth: theme.formLabelWidth(parent.width, 180); color: theme.text }
							SpinBox {
								Layout.fillWidth: true
								from: 0; to: 100
								value: params.lcFadePctInNeutral ?? 70
								editable: true
								validator: IntValidator { bottom: 0; top: 100 }
								onValueModified: root.setParam("lcFadePctInNeutral", value)
							}
						}

						RowLayout {
							visible: selectedMode === 8 || selectedMode === 10
							Layout.fillWidth: true
							Label { text: "LM Fade %"; Layout.preferredWidth: theme.formLabelWidth(parent.width, 180); color: theme.text }
							SpinBox {
								Layout.fillWidth: true
								from: 0; to: 100
								value: params.lmFadePctInNeutral ?? 50
								editable: true
								validator: IntValidator { bottom: 0; top: 100 }
								onValueModified: root.setParam("lmFadePctInNeutral", value)
							}
						}
					}


                    // --- Promotion knobs (all modes) ---
                    Rectangle { height: 1; Layout.fillWidth: true; color: theme.divider; opacity: 0.65 }

                    Label { text: "Promotion Thresholds"; font.bold: true; color: theme.text }

                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: "Tone Gate"; Layout.preferredWidth: theme.formLabelWidth(parent.width, 180); color: theme.text }
                        SpinBox {
                            Layout.fillWidth: true
                            from: 0; to: 255
                            value: params.promoToneGate ?? 112
                            editable: true
                            validator: IntValidator { bottom: 0; top: 255 }
                            onValueModified: root.setParam("promoToneGate", value)
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: "Min Neighbor Inked"; Layout.preferredWidth: theme.formLabelWidth(parent.width, 180); color: theme.text }
                        SpinBox {
                            Layout.fillWidth: true
                            from: 0; to: 64
                            value: params.promoMinNeiInked ?? 8
                            editable: true
                            validator: IntValidator { bottom: 0; top: 64 }
                            onValueModified: root.setParam("promoMinNeiInked", value)
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: "Med Lo"; Layout.preferredWidth: theme.formLabelWidth(parent.width, 180); color: theme.text }
                        SpinBox {
                            Layout.fillWidth: true
                            from: 0; to: 255
                            value: params.promoMedLo ?? 18
                            editable: true
                            validator: IntValidator { bottom: 0; top: 255 }
                            onValueModified: root.setParam("promoMedLo", value)
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: "Med Hi"; Layout.preferredWidth: theme.formLabelWidth(parent.width, 180); color: theme.text }
                        SpinBox {
                            Layout.fillWidth: true
                            from: 0; to: 255
                            value: params.promoMedHi ?? 26
                            editable: true
                            validator: IntValidator { bottom: 0; top: 255 }
                            onValueModified: root.setParam("promoMedHi", value)
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: "Kick Bonus"; Layout.preferredWidth: theme.formLabelWidth(parent.width, 180); color: theme.text }
                        SpinBox {
                            Layout.fillWidth: true
                            from: 0; to: 16
                            value: params.promoKickBonus ?? 2
                            editable: true
                            validator: IntValidator { bottom: 0; top: 16 }
                            onValueModified: root.setParam("promoKickBonus", value)
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: "Large Lo"; Layout.preferredWidth: theme.formLabelWidth(parent.width, 180); color: theme.text }
                        SpinBox {
                            Layout.fillWidth: true
                            from: 0; to: 255
                            value: params.promoLrgLo ?? 28
                            editable: true
                            validator: IntValidator { bottom: 0; top: 255 }
                            onValueModified: root.setParam("promoLrgLo", value)
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: "Large Hi"; Layout.preferredWidth: theme.formLabelWidth(parent.width, 180); color: theme.text }
                        SpinBox {
                            Layout.fillWidth: true
                            from: 0; to: 255
                            value: params.promoLrgHi ?? 36
                            editable: true
                            validator: IntValidator { bottom: 0; top: 255 }
                            onValueModified: root.setParam("promoLrgHi", value)
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: "Flat Var Eps"; Layout.preferredWidth: theme.formLabelWidth(parent.width, 180); color: theme.text }
                        SpinBox {
                            Layout.fillWidth: true
                            from: 0; to: 255
                            value: params.promoFlatVarEps ?? 18
                            editable: true
                            validator: IntValidator { bottom: 0; top: 255 }
                            onValueModified: root.setParam("promoFlatVarEps", value)
                        }
                    }
                }
            }
            
            Pane {
				Layout.fillWidth: true
				padding: 16
				enabled: modeHasWhite(root.selectedMode) || modeHasVarnish(root.selectedMode)

				background: Rectangle {
					color: theme.surface
					radius: 12
					border.width: 1
					border.color: theme.divider
					opacity: enabled ? 1.0 : 0.6
				}

				ColumnLayout {
					anchors.horizontalCenter: parent.horizontalCenter
					width: theme.boundedWidth(parent.width, root.cardWidthWide)
					spacing: 12

					Label {
						text: "White / Varnish Defaults (Per-Mode)"
						color: theme.text
						font.pixelSize: 18
						font.weight: Font.Medium
						Layout.alignment: Qt.AlignHCenter
					}

					Rectangle { height: 1; Layout.fillWidth: true; color: theme.divider; opacity: 0.8 }

					Label {
						visible: !(modeHasWhite(root.selectedMode) || modeHasVarnish(root.selectedMode))
						text: "Specialty ink settings are only available for modes with White and/or Varnish."
						color: theme.subtext
						wrapMode: Text.WordWrap
						Layout.fillWidth: true
					}

					// =========================
					// White
					// =========================
					Rectangle {
						visible: modeHasWhite(root.selectedMode)
						height: 1
						Layout.fillWidth: true
						color: theme.divider
						opacity: 0.65
					}

					Label {
						visible: modeHasWhite(root.selectedMode)
						text: "White"
						font.bold: true
						color: theme.text
					}

					ColumnLayout {
						visible: modeHasWhite(root.selectedMode)
						Layout.fillWidth: true
						spacing: 10

						RowLayout {
							Layout.fillWidth: true
							Label { text: "White Mode"; Layout.preferredWidth: theme.formLabelWidth(parent.width, 180); color: theme.text }
							ComboBox {
								Layout.fillWidth: true
								model: root.whiteModeOptions
								textRole: "label"
								currentIndex: root.comboIndexForValue(root.whiteModeOptions, params.whiteMode ?? 0, 0)
								onActivated: root.setParam("whiteMode", model[currentIndex].value)
							}
						}

						RowLayout {
							Layout.fillWidth: true
							Label { text: "White Threshold"; Layout.preferredWidth: theme.formLabelWidth(parent.width, 180); color: theme.text }
							SpinBox {
								Layout.fillWidth: true
								from: 0; to: 255
								value: params.whiteThreshold ?? 8
								editable: true
								validator: IntValidator { bottom: 0; top: 255 }
								onValueModified: root.setParam("whiteThreshold", value)
							}
						}

						RowLayout {
							Layout.fillWidth: true
							Label { text: "White Density"; Layout.preferredWidth: theme.formLabelWidth(parent.width, 180); color: theme.text }
							SpinBox {
								Layout.fillWidth: true
								from: 0; to: 255
								value: params.whiteDensity ?? 255
								editable: true
								validator: IntValidator { bottom: 0; top: 255 }
								onValueModified: root.setParam("whiteDensity", value)
							}
						}

						Rectangle { height: 1; Layout.fillWidth: true; color: theme.divider; opacity: 0.45 }

						RowLayout {
							Layout.fillWidth: true
							Label { text: "Mask Selection"; Layout.preferredWidth: theme.formLabelWidth(parent.width, 180); color: theme.text }
							ComboBox {
								Layout.fillWidth: true
								model: root.specialtyMaskOptions
								textRole: "label"
								currentIndex: root.comboIndexForValue(root.specialtyMaskOptions, params.whiteMaskKey ?? "w", 0)
								onActivated: root.setParam("whiteMaskKey", model[currentIndex].value)
							}
						}

						Rectangle { height: 1; Layout.fillWidth: true; color: theme.divider; opacity: 0.45 }

						RowLayout {
							Layout.fillWidth: true
							Label { text: "Own Screening Settings"; color: theme.text; wrapMode: Text.WordWrap; Layout.fillWidth: true }
							Switch {
                                Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
								checked: params.whiteUseOwnDotStrategy ?? false
								onToggled: root.setParam("whiteUseOwnDotStrategy", checked)
							}
						}

						ColumnLayout {
							visible: params.whiteUseOwnDotStrategy ?? false
							Layout.fillWidth: true
							spacing: 10

							RowLayout {
								Layout.fillWidth: true
								Label { text: "White Small Dot"; Layout.preferredWidth: theme.formLabelWidth(parent.width, 180); color: theme.text }
								SpinBox {
								    Layout.fillWidth: true
								    from: 0; to: 255
								    value: params.whiteSmallDotThreshold ?? 104
								    editable: true
								    validator: IntValidator { bottom: 0; top: 255 }
								    onValueModified: root.setParam("whiteSmallDotThreshold", value)
								}
							}

							RowLayout {
								Layout.fillWidth: true
								Label { text: "White Medium Dot"; Layout.preferredWidth: theme.formLabelWidth(parent.width, 180); color: theme.text }
								SpinBox {
								    Layout.fillWidth: true
								    from: 0; to: 255
								    value: params.whiteMedDotThreshold ?? 168
								    editable: true
								    validator: IntValidator { bottom: 0; top: 255 }
								    onValueModified: root.setParam("whiteMedDotThreshold", value)
								}
							}

							RowLayout {
								Layout.fillWidth: true
								Label { text: strings.trKey("color.enablePromotion"); color: theme.text; wrapMode: Text.WordWrap; Layout.fillWidth: true }
								Switch {
                                    Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
								    checked: params.whiteEnablePromotion ?? false
								    onToggled: root.setParam("whiteEnablePromotion", checked)
								}
							}
						}
					}

					// =========================
					// Varnish
					// =========================
					Rectangle {
						visible: modeHasVarnish(root.selectedMode)
						height: 1
						Layout.fillWidth: true
						color: theme.divider
						opacity: 0.65
					}

					Label {
						visible: modeHasVarnish(root.selectedMode)
						text: "Varnish"
						font.bold: true
						color: theme.text
					}

					ColumnLayout {
						visible: modeHasVarnish(root.selectedMode)
						Layout.fillWidth: true
						spacing: 10

						RowLayout {
							Layout.fillWidth: true
							Label { text: "Varnish Mode"; Layout.preferredWidth: theme.formLabelWidth(parent.width, 180); color: theme.text }
							ComboBox {
								Layout.fillWidth: true
								model: root.varnishModeOptions
								textRole: "label"
								currentIndex: root.comboIndexForValue(root.varnishModeOptions, params.varnishMode ?? 0, 0)
								onActivated: root.setParam("varnishMode", model[currentIndex].value)
							}
						}

						RowLayout {
							Layout.fillWidth: true
							Label { text: "Varnish Threshold"; Layout.preferredWidth: theme.formLabelWidth(parent.width, 180); color: theme.text }
							SpinBox {
								Layout.fillWidth: true
								from: 0; to: 255
								value: params.varnishThreshold ?? 8
								editable: true
								validator: IntValidator { bottom: 0; top: 255 }
								onValueModified: root.setParam("varnishThreshold", value)
							}
						}

						RowLayout {
							Layout.fillWidth: true
							Label { text: "Varnish Density"; Layout.preferredWidth: theme.formLabelWidth(parent.width, 180); color: theme.text }
							SpinBox {
								Layout.fillWidth: true
								from: 0; to: 255
								value: params.varnishDensity ?? 255
								editable: true
								validator: IntValidator { bottom: 0; top: 255 }
								onValueModified: root.setParam("varnishDensity", value)
							}
						}

						Rectangle { height: 1; Layout.fillWidth: true; color: theme.divider; opacity: 0.45 }

						RowLayout {
							Layout.fillWidth: true
							Label { text: "Mask Selection"; Layout.preferredWidth: theme.formLabelWidth(parent.width, 180); color: theme.text }
							ComboBox {
								Layout.fillWidth: true
								model: root.specialtyMaskOptions
								textRole: "label"
								currentIndex: root.comboIndexForValue(root.specialtyMaskOptions, params.varnishMaskKey ?? "v", 1)
								onActivated: root.setParam("varnishMaskKey", model[currentIndex].value)
							}
						}

						Rectangle { height: 1; Layout.fillWidth: true; color: theme.divider; opacity: 0.45 }

						RowLayout {
							Layout.fillWidth: true
							Label { text: "Own Screening Settings"; color: theme.text; wrapMode: Text.WordWrap; Layout.fillWidth: true }
							Switch {
                                Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
								checked: params.varnishUseOwnDotStrategy ?? false
								onToggled: root.setParam("varnishUseOwnDotStrategy", checked)
							}
						}

						ColumnLayout {
							visible: params.varnishUseOwnDotStrategy ?? false
							Layout.fillWidth: true
							spacing: 10

							RowLayout {
								Layout.fillWidth: true
								Label { text: "Varnish Small Dot"; Layout.preferredWidth: theme.formLabelWidth(parent.width, 180); color: theme.text }
								SpinBox {
								    Layout.fillWidth: true
								    from: 0; to: 255
								    value: params.varnishSmallDotThreshold ?? 104
								    editable: true
								    validator: IntValidator { bottom: 0; top: 255 }
								    onValueModified: root.setParam("varnishSmallDotThreshold", value)
								}
							}

							RowLayout {
								Layout.fillWidth: true
								Label { text: "Varnish Medium Dot"; Layout.preferredWidth: theme.formLabelWidth(parent.width, 180); color: theme.text }
								SpinBox {
								    Layout.fillWidth: true
								    from: 0; to: 255
								    value: params.varnishMedDotThreshold ?? 168
								    editable: true
								    validator: IntValidator { bottom: 0; top: 255 }
								    onValueModified: root.setParam("varnishMedDotThreshold", value)
								}
							}

							RowLayout {
								Layout.fillWidth: true
								Label { text: strings.trKey("color.enablePromotion"); color: theme.text; wrapMode: Text.WordWrap; Layout.fillWidth: true }
								Switch {
                                    Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
								    checked: params.varnishEnablePromotion ?? false
								    onToggled: root.setParam("varnishEnablePromotion", checked)
								}
							}
						}
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
