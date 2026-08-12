import QtQuick
import QtQuick.Controls
import QtQuick.Controls as C
import QtQuick.Layouts
import Qt.labs.platform as P
import QtCore
import QtQuick.Window
import "."


// Job list view: selection workflow, persistence (load/save), and PRN generation entry.
Item {
    id: root
    required property StackView stackView
    required property var jobModel
    required property var appState
    required property Theme theme

    // Selection state used by toolbar actions and list highlighting.
    property bool selectionMode: false
    property var selectedIndexes: []
    property string suggestedFilename: ""
    property string selectedLanguage: strings.language
    property bool waitingForNewJobImport: false

    width: parent.width
    height: parent.height
    
    Component.onCompleted: {
    	colorManager.selectedPrinter = appState.selectedPrinter
	}
	
	Connections {
		target: appState
		function onSelectedPrinterChanged() {
			colorManager.selectedPrinter = appState.selectedPrinter
		}
	}

	// Toggle a single index in/out of the selection.
    function toggleSelection(index) {
            const exists = selectedIndexes.includes(index)
            const updated = selectedIndexes.slice()

            if (exists) {
                const i = updated.indexOf(index)
                updated.splice(i, 1)
            } else {
                updated.push(index)
            }

            selectedIndexes = updated
	}

		
	// Readable check for whether an index is selected.
    function isSelected(index) {
        return selectedIndexes.includes(index)
    }


	// True only if every job is selected (keeps toolbar label in sync).
    function areAllJobsSelected() {
        if (selectedIndexes.length !== jobModel.count)
            return false

        for (let i = 0; i < jobModel.count; ++i) {
            if (selectedIndexes.indexOf(i) === -1)
                return false
        }
        return true
    }


	// Select all jobs currently in the model.
    function selectAll() {
        const total = jobModel.count
        if (total === 0)
            return

        const all = []
        for (let i = 0; i < total; ++i) {
            all.push(i)
        }

        selectedIndexes = all
    }


    // Clear multi-selection.
    function deselectAll() {
        selectedIndexes = []
    }


    function directPrintSettings() {
        return {
            selectedPrinterIndex: appState.sdkSelectedPrinterIndex,
            printDirection: appState.sdkPrintDirection,
            printSpeed: appState.sdkPrintSpeed,
            wcSequence: appState.sdkWcSequence,
            eclosionGrade: appState.sdkEclosionGrade,
            headSelect: appState.sdkHeadSelect,
            whiteInkPercent: appState.sdkWhiteInkPercent,
            whiteInkPassCount: appState.sdkWhiteInkPassCount,
            varnishInkPercent: appState.sdkVarnishInkPercent,
            varnishInkPassCount: appState.sdkVarnishInkPassCount,
            headVoltage: appState.sdkHeadVoltage,
            disableUv0: appState.sdkDisableUv0,
            disableUv1: appState.sdkDisableUv1,
            disableUv2: appState.sdkDisableUv2,
            disableUv3: appState.sdkDisableUv3,
            disableUv4: appState.sdkDisableUv4,
            disableUv5: appState.sdkDisableUv5,
            carReset: appState.sdkCarReset,
            stripBlank: appState.sdkStripBlank,
            blankDistance: appState.sdkBlankDistance,
            pass: appState.sdkPass,
            vsdMode: appState.sdkVsdMode
        }
    }

    function beginOutputProgress(mode) {
        appState.outputProgressMode = mode
        appState.outputProgressPhase = "rasterizing"
        appState.isGeneratingPRN = true
    }

    function endOutputProgress() {
        appState.isGeneratingPRN = false
        appState.outputProgressPhase = ""
        appState.outputProgressMode = ""
    }

    function outputProgressText() {
        switch (appState.outputProgressPhase) {
        case "generatingPrn":
            return strings.trKey("jobs.progress.generatingPrn")
        case "printing":
            return strings.trKey("jobs.progress.printing")
        case "rasterizing":
            return strings.trKey("jobs.progress.rasterizing")
        default:
            return appState.outputProgressMode === "direct"
                    ? strings.trKey("jobs.progress.preparingPrint")
                    : strings.trKey("jobs.progress.preparingPrn")
        }
    }

    function languageDisplayName(code) {
        if (code === "zh-Hans")
            return strings.trKey("language.chineseSimplified")
        return strings.trKey("language.english")
    }

    function refreshJobListView() {
        stackView.replace("qrc:/qml/JobListView.qml", {
            stackView: stackView,
            appState: appState,
            jobModel: jobModel,
            theme: root.theme
        })
    }

    function pushJobDetails(index) {
        stackView.push("qrc:/qml/JobDetailsView.qml", {
            jobIndex: index,
            stackView: stackView,
            appState: appState,
            jobModel: jobModel,
            theme: root.theme
        })
    }

    function createJobFromImage(sourcePath, openDetails, showResult) {
        const shouldOpenDetails = openDetails === undefined ? true : openDetails
        const shouldShowResult = showResult === undefined ? true : showResult
        const index = jobModel.addJobFromImage(sourcePath)
        if (index >= 0) {
            if (shouldShowResult)
                toast.show(strings.trKey("jobs.toast.imageJobCreated"))
            if (shouldOpenDetails)
                pushJobDetails(index)
        } else {
            const message = jobModel.lastError()
            if (shouldShowResult)
                toast.show(message.length > 0 ? message : strings.trKey("jobs.toast.imageJobFailed"))
        }
        return index
    }

    function isSupportedDroppedImage(sourceUrl) {
        const path = String(sourceUrl).split(/[?#]/)[0].toLowerCase()
        return /\.(jpeg|jpg|png|bmp|tiff|tif|svg|pdf)$/.test(path)
    }

    function supportedDroppedImages(urls) {
        const images = []
        if (!urls)
            return images

        for (let i = 0; i < urls.length; ++i) {
            const sourceUrl = String(urls[i])
            if (isSupportedDroppedImage(sourceUrl))
                images.push(sourceUrl)
        }
        return images
    }

    function createJobsFromDroppedImages(urls) {
        const images = supportedDroppedImages(urls)
        if (images.length === 0) {
            toast.show(strings.trKey("jobs.toast.dropUnsupported"))
            return
        }

        if (images.length === 1) {
            createJobFromImage(images[0])
            return
        }

        let createdCount = 0
        let failureMessage = ""
        for (let i = 0; i < images.length; ++i) {
            if (createJobFromImage(images[i], false, false) >= 0) {
                ++createdCount
            } else if (failureMessage.length === 0) {
                failureMessage = jobModel.lastError()
            }
        }

        if (createdCount === images.length) {
            toast.show(strings.trKey("jobs.toast.imagesDropped"))
        } else if (createdCount > 0) {
            toast.show(strings.trKey("jobs.toast.imagesDropPartial"))
        } else {
            toast.show(failureMessage.length > 0
                       ? failureMessage
                       : strings.trKey("jobs.toast.imageJobFailed"))
        }
    }

    function removeSelectedJobs() {
        const sorted = selectedIndexes.slice().sort((a, b) => b - a)
        for (let i = 0; i < sorted.length; ++i)
            jobModel.removeJob(sorted[i])
        selectedIndexes = []
    }

    Connections {
        target: imageImportManager
        function onImageReady(localFilePath) {
            if (!root.waitingForNewJobImport)
                return
            root.waitingForNewJobImport = false
            root.createJobFromImage(localFilePath)
        }
        function onCanceled() {
            if (!root.waitingForNewJobImport)
                return
            root.waitingForNewJobImport = false
        }
        function onFailed(message) {
            if (!root.waitingForNewJobImport)
                return
            root.waitingForNewJobImport = false
            toast.show(message)
        }
    }


    function printSelectedSdkJobDirectly() {
        if (!appState.supportsDirectPrint || !appState.supportsRipProcessing) {
            toast.show(strings.trKey("jobs.toast.directUnavailable"))
            return
        }
        appState.configureDirectPrintSdk()
        if (!nocaiDirectPrint.available) {
            const sdkError = nocaiDirectPrint.lastError
            toast.show(sdkError.length > 0 ? sdkError : strings.trKey("jobs.toast.directUnavailable"))
            return
        }

        const job = jobModel.getJob(selectedIndexes[0])
        var directJob = Object.assign({}, job)
        directJob.inkMode = appState.multiInkInkMode
        directJob.directPrintSettings = directPrintSettings()
        beginOutputProgress("direct")
        if (appState.usingMultiInkPrinter) {
            console.log("Routing to the newer-model MultiInk SDK backend with inkMode =", appState.multiInkInkMode)
            printJobMultiInk.runDirectPrint(directJob)
        } else {
            console.log("Routing X-33 to the legacy SDK through the standard CMYK backend")
            printJobCMYK.runDirectPrint(directJob)
        }
    }


	// Direct print path (bypasses file save): generates and sends to the configured printer.
    function printSelectedJobDirectly() {
        if (!appState.supportsCupsPrinting) {
            toast.show(strings.trKey("jobs.toast.cupsUnavailable"))
            return
        }

        const index = selectedIndexes[0]
        const job = jobModel.getJob(index)
        const inputFile = job.imagePath

        const outputPath = "" // Empty because printing directly to printer

        const success = printJobOutput.generatePRN(job, inputFile, outputPath)
        if (success) {
            console.log("Print job sent to printer:", appState.selectedPrinter)
            toast.show(strings.trKey("jobs.toast.printSent"))
        } else {
            console.warn("Failed to print job:", job.name)
            toast.show(strings.trKey("jobs.toast.printFailed"))
        }
    }
    
	function handlePrnFinished(success) {
		if (!appState.isGeneratingPRN)
			return

		endOutputProgress()

		if (success) {
            if (appState.usingSimulatedPrinter && appState.multiInkOutputMode === "direct") {
                console.log("Direct print sent successfully.")
                toast.show(strings.trKey("jobs.toast.sentToPrinter"))
            } else {
                console.log("PRN generated successfully:", outputFileDialog.file)
                toast.show(strings.trKey("jobs.toast.prnGenerated"))
            }
		} else {
            if (appState.usingSimulatedPrinter && appState.multiInkOutputMode === "direct") {
                console.warn("Failed to send direct print job.")
                toast.show(strings.trKey("jobs.toast.sendFailed"))
            } else {
                console.warn(strings.trKey("jobs.toast.prnFailed"))
                toast.show(strings.trKey("jobs.toast.prnFailed"))
            }
		}
	}


	// Main vertical layout for header, toolbars, list, and dialogs.
    ColumnLayout {
        width: parent.width
    	height: parent.height
        spacing: 0

        // Top branding/header with nav to printer setup.
        Rectangle {
			height: 60
			Layout.fillWidth: true
			color: theme.surface

            RowLayout {
				anchors.fill: parent
				anchors.leftMargin: 8
				anchors.rightMargin: 8
				spacing: 10

					Item {
						Layout.preferredWidth: root.width < 380 ? 64 : 132
						Layout.fillHeight: true

						Image {
							source: theme.logoPath
							anchors.left: parent.left
							anchors.verticalCenter: parent.verticalCenter
							width: theme.logoWidth
							height: theme.logoHeight
							fillMode: Image.PreserveAspectFit
							smooth: true
						}
					}

	                Label {
						text: strings.trKey("jobs.title")
						font.pixelSize: root.width < 380 ? 18 : 22
						color: theme.text
						horizontalAlignment: Text.AlignHCenter
						verticalAlignment: Text.AlignVCenter
						Layout.fillWidth: true
						Layout.alignment: Qt.AlignVCenter
					}

					Item {
						Layout.preferredWidth: root.width < 380 ? 80 : 132
						Layout.fillHeight: true

						C.ToolButton {
							id: settingsBtn
							text: strings.trKey("jobs.settings")
							anchors.right: parent.right
							anchors.verticalCenter: parent.verticalCenter
							width: root.width < 380 ? 76 : 112
							height: 36
							hoverEnabled: true
							padding: 10

							background: Rectangle {
								radius: 8
								color: settingsBtn.pressed
									   ? Qt.rgba(root.theme.accent2.r, root.theme.accent2.g, root.theme.accent2.b, 0.22)
									   : (settingsBtn.hovered
										    ? Qt.rgba(root.theme.text.r, root.theme.text.g, root.theme.text.b, 0.10)
										    : "transparent")
								border.width: settingsBtn.hovered || settingsBtn.pressed ? 1 : 0
								border.color: root.theme.divider
							}

							contentItem: Label {
								text: settingsBtn.text
								color: theme.text
								verticalAlignment: Text.AlignVCenter
								horizontalAlignment: Text.AlignHCenter
								elide: Text.ElideRight
							}

							onClicked: {
								const host = root.Window.window ? root.Window.window.contentItem : root
								const p = settingsBtn.mapToItem(host, 0, settingsBtn.height)

								settingsMenu.parent = host
								settingsMenu.x = Math.min(Math.max(8, p.x + settingsBtn.width - settingsMenu.implicitWidth),
                                                         Math.max(8, host.width - settingsMenu.implicitWidth - 8))
								settingsMenu.y = p.y + 6
								settingsMenu.open()
							}
						}
					}


				C.Menu {
					id: settingsMenu
					padding: 6
					parent: root.Window.window ? root.Window.window.contentItem : root
					modal: false

					background: Rectangle {
						color: theme.surface2
						radius: 10
						implicitWidth: 240
						implicitHeight: contentItem ? contentItem.implicitHeight + 12 : 120
						border.width: 1
						border.color: theme.divider
					}

						C.MenuItem {
							id: miLoad
							text: strings.trKey("jobs.load")
							hoverEnabled: true

							background: Rectangle {
								radius: 8
								color: miLoad.pressed
									   ? Qt.rgba(root.theme.accent2.r, root.theme.accent2.g, root.theme.accent2.b, 0.25)
									   : (miLoad.hovered
											? Qt.rgba(root.theme.text.r, root.theme.text.g, root.theme.text.b, 0.12)
											: "transparent")
							}

							contentItem: Label { text: miLoad.text; color: root.theme.text; verticalAlignment: Text.AlignVCenter }

							onTriggered: {
								settingsMenu.close()
								openFileDialog.open()
							}
						}

						C.MenuSeparator { }

						C.MenuItem {
							id: miCreateFromImage
							text: strings.trKey("jobs.createFromImage")
							hoverEnabled: true

							background: Rectangle {
								radius: 8
								color: miCreateFromImage.pressed
									   ? Qt.rgba(root.theme.accent2.r, root.theme.accent2.g, root.theme.accent2.b, 0.25)
									   : (miCreateFromImage.hovered
											? Qt.rgba(root.theme.text.r, root.theme.text.g, root.theme.text.b, 0.12)
											: "transparent")
							}

							contentItem: Label { text: miCreateFromImage.text; color: root.theme.text; verticalAlignment: Text.AlignVCenter }

							onTriggered: {
								settingsMenu.close()
								if (imageImportManager.supportsNativeImagePicker) {
									root.waitingForNewJobImport = true
									imageImportManager.openImageImportChooser()
								} else {
									createJobImageDialog.open()
								}
							}
						}

						C.MenuSeparator { }

						C.MenuItem {
							id: miPrinter
							text: strings.trKey("jobs.printerSetup")
							hoverEnabled: true

							background: Rectangle {
								radius: 8
								color: miPrinter.pressed
									   ? Qt.rgba(root.theme.accent2.r, root.theme.accent2.g, root.theme.accent2.b, 0.25)
									   : (miPrinter.hovered
											? Qt.rgba(root.theme.text.r, root.theme.text.g, root.theme.text.b, 0.12)
											: "transparent")
							}

							contentItem: Label { text: miPrinter.text; color: root.theme.text; verticalAlignment: Text.AlignVCenter }

							onTriggered: {
								settingsMenu.close()
								stackView.push("qrc:/qml/PrinterSetupView.qml", {
									stackView: stackView,
									appState: appState,
									theme: root.theme
								})
							}
						}
						
						C.MenuSeparator { }

						C.MenuItem {
							id: miMaintenance
							text: strings.trKey("jobs.printerMaintenance")
							enabled: nocaiDirectPrint.supportsMaintenance(appState.selectedPrinter)
							hoverEnabled: enabled

							background: Rectangle {
								radius: 8
								color: miMaintenance.pressed
									   ? Qt.rgba(root.theme.accent2.r, root.theme.accent2.g, root.theme.accent2.b, 0.25)
									   : (miMaintenance.hovered
											? Qt.rgba(root.theme.text.r, root.theme.text.g, root.theme.text.b, 0.12)
											: "transparent")
							}

							contentItem: Label {
								text: miMaintenance.text
								color: miMaintenance.enabled ? root.theme.text : root.theme.subtext
								opacity: miMaintenance.enabled ? 1.0 : 0.55
								verticalAlignment: Text.AlignVCenter
							}

							onTriggered: {
								settingsMenu.close()
								stackView.push("qrc:/qml/PrinterMaintenanceView.qml", {
									stackView: stackView,
									appState: appState,
									theme: root.theme
								})
							}
						}

						C.MenuSeparator { }

						C.MenuItem {
							id: miColor
							text: strings.trKey("jobs.colorManagement")
							hoverEnabled: true

							background: Rectangle {
								radius: 8
								color: miColor.pressed
									   ? Qt.rgba(root.theme.accent2.r, root.theme.accent2.g, root.theme.accent2.b, 0.25)
									   : (miColor.hovered
											? Qt.rgba(root.theme.text.r, root.theme.text.g, root.theme.text.b, 0.12)
											: "transparent")
							}

							contentItem: Label { text: miColor.text; color: root.theme.text; verticalAlignment: Text.AlignVCenter }

							onTriggered: {
								settingsMenu.close()
								stackView.push("qrc:/qml/ColorManagementView.qml", {
									stackView: stackView,
									appState: appState,
									theme: root.theme
								})
							}
						}

						C.MenuSeparator { }

						C.MenuItem {
							id: miLanguage
							text: strings.trKey("language.menu")
							hoverEnabled: true

							background: Rectangle {
								radius: 8
								color: miLanguage.pressed
									   ? Qt.rgba(root.theme.accent2.r, root.theme.accent2.g, root.theme.accent2.b, 0.25)
									   : (miLanguage.hovered
											? Qt.rgba(root.theme.text.r, root.theme.text.g, root.theme.text.b, 0.12)
											: "transparent")
							}

							contentItem: Label { text: miLanguage.text; color: root.theme.text; verticalAlignment: Text.AlignVCenter }

							onTriggered: {
								settingsMenu.close()
                                selectedLanguage = strings.language
								languagePopup.open()
							}
						}

						C.MenuSeparator { }

						C.MenuItem {
							id: miDarkMode
							text: root.theme.dark ? strings.trKey("jobs.switchLight") : strings.trKey("jobs.switchDark")
							hoverEnabled: true

							background: Rectangle {
								radius: 8
								color: miDarkMode.pressed
									   ? Qt.rgba(root.theme.accent2.r, root.theme.accent2.g, root.theme.accent2.b, 0.25)
									   : (miDarkMode.hovered
											? Qt.rgba(root.theme.text.r, root.theme.text.g, root.theme.text.b, 0.12)
											: "transparent")
							}
							contentItem: Label { text: miDarkMode.text; color: root.theme.text; verticalAlignment: Text.AlignVCenter }
							onTriggered: root.theme.dark = !root.theme.dark
						}
				}

                Popup {
                    id: languagePopup
                    parent: Overlay.overlay
                    modal: true
                    focus: true
                    width: Math.min(Math.max(root.width - 32, 240), 360)
                    x: Math.round((root.width - width) / 2)
                    y: Math.round((root.height - height) / 2)
                    padding: 18
                    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

                    background: Rectangle {
                        color: root.theme.surface2
                        radius: 8
                        border.width: 1
                        border.color: root.theme.divider
                    }

                    contentItem: ColumnLayout {
                        spacing: 14

                        Label {
                            text: strings.trKey("language.title")
                            color: root.theme.text
                            font.pixelSize: 20
                            font.weight: Font.Medium
                            Layout.fillWidth: true
                        }

                        Label {
                            text: strings.trKey("language.subtitle")
                            color: root.theme.subtext
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }

                        Label {
                            text: strings.trKey("language.current") + ": " + root.languageDisplayName(strings.language)
                            color: root.theme.subtext
                            font.pixelSize: 13
                            Layout.fillWidth: true
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4

                            Repeater {
                                model: strings.availableLanguages

                                delegate: RadioDelegate {
                                    id: languageOption
                                    Layout.fillWidth: true
                                    text: root.languageDisplayName(modelData.code)
                                    checked: root.selectedLanguage === modelData.code
                                    onClicked: root.selectedLanguage = modelData.code

                                    contentItem: Label {
                                        text: languageOption.text
                                        color: root.theme.text
                                        verticalAlignment: Text.AlignVCenter
                                        leftPadding: languageOption.indicator.width + languageOption.spacing
                                    }
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10

                            Item { Layout.fillWidth: true }

                            ThemedButton {
                                text: strings.trKey("common.cancel")
                                theme: root.theme
                                onClicked: languagePopup.close()
                            }

                            ThemedButton {
                                text: strings.trKey("language.apply")
                                theme: root.theme
                                onClicked: {
                                    const changed = strings.language !== root.selectedLanguage
                                    strings.language = root.selectedLanguage
                                    languagePopup.close()
                                    if (changed) {
                                        toast.show(strings.trKey("language.changed"))
                                        Qt.callLater(root.refreshJobListView)
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }


        // Toolbar controlling selection lifecycle and save/remove actions.
        Frame {
            Layout.fillWidth: true
            Layout.preferredHeight: selectionMode && root.width < 430 ? 118 : 62
            padding: 10
			background: Rectangle { color: theme.surface2 }

            RowLayout {
				anchors.fill: parent
				anchors.leftMargin: 8
				anchors.rightMargin: 8
				spacing: 10
                visible: !selectionMode

                ThemedButton {
					text: strings.trKey("jobs.new")
					theme: root.theme
					padding: 12
					font.pixelSize: 15
					onClicked: jobModel.addJob("New Print Job")
				}
				
		        Item { Layout.fillWidth: true }

				ThemedButton {
					text: strings.trKey("jobs.select")
					theme: root.theme
					padding: 12
					font.pixelSize: 15
					onClicked: {
						selectedIndexes = []
						selectionMode = true
					}
				}
            }

            GridLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                visible: selectionMode
                columns: root.theme.gridColumns(width, 4, 104)
                columnSpacing: 8
                rowSpacing: 8

                readonly property int actionButtonWidth: Math.floor((width - (columns - 1) * columnSpacing) / columns)

				ThemedButton {
					Layout.preferredWidth: parent.actionButtonWidth
                    Layout.minimumWidth: 0
                    Layout.preferredHeight: 42
					text: strings.trKey("jobs.cancelSelection")
					theme: root.theme
					onClicked: {
						selectedIndexes = []
						selectionMode = false
					}
				}

				ThemedButton {
					Layout.preferredWidth: parent.actionButtonWidth
                    Layout.minimumWidth: 0
                    Layout.preferredHeight: 42
					text: areAllJobsSelected() ? strings.trKey("jobs.deselectAll") : strings.trKey("jobs.selectAll")
					theme: root.theme
					onClicked: areAllJobsSelected() ? deselectAll() : selectAll()
				}

				ThemedButton {
					Layout.preferredWidth: parent.actionButtonWidth
                    Layout.minimumWidth: 0
                    Layout.preferredHeight: 42
					text: strings.trKey("jobs.remove")
					enabled: selectedIndexes.length > 0
					theme: root.theme
					onClicked: removeJobsDialog.open()
				}

                ThemedButton {
                    Layout.preferredWidth: parent.actionButtonWidth
                    Layout.minimumWidth: 0
                    Layout.preferredHeight: 42
                    text: strings.trKey("jobs.save")
                    enabled: selectedIndexes.length > 0
       				theme: root.theme
                    onClicked: {
                        let jobName = jobModel.getJob(selectedIndexes[0]).name || "UntitledJob"
                        const downloads = StandardPaths.writableLocation(StandardPaths.DownloadLocation)
                        const fullPath = downloads + "/" + jobName.replace(/[^a-zA-Z0-9_-]/g, "_") + ".json"
                        saveFileDialog.currentFile = fullPath
                        saveFileDialog.open()
                    }
                }
            }
        }


        // Selection status readout; shown only while selection mode is active.
        Label {
            visible: selectionMode
            text: selectedIndexes.length + strings.trKey("jobs.selectedSuffix")
            font.pixelSize: 14
			color: theme.subtext
            Layout.topMargin: 10
            Layout.bottomMargin: 10
            horizontalAlignment: Text.AlignHCenter
            Layout.fillWidth: true
        }


        // Scrollable job list with click-to-open or click-to-toggle-select behavior.
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 12
            clip: true
            
            ScrollBar.vertical.policy: ScrollBar.AlwaysOff
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            contentWidth: availableWidth

            ListView {
                id: jobListView
                width: parent.width
                clip: true
                model: jobModel
                spacing: 6
                Layout.alignment: Qt.AlignHCenter

				// Each row supports selection highlight and navigation into details.
                delegate: ItemDelegate {
                    width: jobListView.width
                    hoverEnabled: true
                    highlighted: selectionMode && isSelected(index)
                    
					background: Rectangle {
						radius: 10
						color: highlighted
							   ? Qt.rgba(root.theme.accent.r, root.theme.accent.g, root.theme.accent.b, 0.18)
							   : (parent.hovered
								    ? Qt.rgba(root.theme.text.r, root.theme.text.g, root.theme.text.b, 0.08)
								    : "transparent")
						border.width: highlighted ? 1 : 0
						border.color: root.theme.accent
					}

                    onClicked: {
                        if (selectionMode) {
                            toggleSelection(index)
                        } else {
                            stackView.push("qrc:/qml/JobDetailsView.qml", {
                                jobIndex: index,
	                                stackView: stackView,
	                                appState: appState,
	                                jobModel: jobModel,
	                                theme: root.theme
	                            })
                        }
                    }


					// Row content: optional checkbox + job name.
                    contentItem: RowLayout {
						Layout.fillWidth: true
						Layout.preferredHeight: parent.height
                        spacing: 10

                        CheckBox {
                            visible: selectionMode
                            checked: isSelected(index)
                            onToggled: toggleSelection(index)
                        }

                        Label {
                            text: model.name
                            Layout.fillWidth: true
                            verticalAlignment: Text.AlignVCenter
                            color: theme.subtext
                        }
                    }
                }
            }
        }

        // Bottom toolbar for creating/loading jobs and kicking off print/PRN.
        Rectangle {
            Layout.fillWidth: true
            height: 50
			color: theme.surface

            RowLayout {
                anchors.centerIn: parent
                spacing: 20

				// Print entry point: either open save dialog (simulated printer) or send directly.
                ThemedButton {
                    text: strings.trKey("jobs.print")
					visible: selectionMode
					enabled: selectedIndexes.length > 0
					theme: root.theme

					padding: 14
					font.pixelSize: 16
					
                    onClicked: {                    
						const job = jobModel.getJob(selectedIndexes[0])
						const jobName = job.name || "UntitledJob"
						const downloads = StandardPaths.writableLocation(StandardPaths.DownloadLocation)
						const fullPath = downloads + "/" + jobName.replace(/[^a-zA-Z0-9_-]/g, "_") + ".prn"
						outputFileDialog.currentFile = fullPath

                        if (appState.usingSimulatedPrinter) {
                            if (appState.multiInkOutputMode === "direct"
                                    && (appState.selectedPrinter === "X-33"
                                        || appState.selectedPrinter === "X-36NC (Photo Printer)"))
                                printSelectedSdkJobDirectly()
                            else
                                outputFileDialog.open()
                        } else {
                            printSelectedJobDirectly()
                        }
                    }
                }
            }
        }


        // File dialog for loading job JSON.
        P.FileDialog {
            id: openFileDialog
            title: strings.trKey("jobs.loadJson.title")
            nameFilters: ["JSON Files (*.json)"]
            fileMode: P.FileDialog.OpenFile
            onAccepted: jobModel.loadFromJson(file)
        }

        P.FileDialog {
            id: createJobImageDialog
            title: strings.trKey("jobs.createFromImage.title")
            nameFilters: ["Images (*.png *.jpg *.jpeg *.bmp *.tif *.tiff *.svg *.pdf)"]
            fileMode: P.FileDialog.OpenFile
            onAccepted: root.createJobFromImage(String(file))
        }

        C.Dialog {
            id: removeJobsDialog
            modal: true
            focus: true
            closePolicy: C.Popup.CloseOnEscape | C.Popup.CloseOnPressOutside
            anchors.centerIn: parent
            width: Math.min(Math.max(parent.width - 48, 240), 360)
            title: strings.trKey("jobs.removeConfirm.title")

            background: Rectangle {
                color: root.theme.surface
                radius: 8
                border.width: 1
                border.color: root.theme.divider
            }

            contentItem: Label {
                text: strings.trKey("jobs.removeConfirm.message")
                color: root.theme.text
                wrapMode: Text.WordWrap
                lineHeight: 1.1
            }

            footer: C.DialogButtonBox {
                alignment: Qt.AlignRight
                spacing: 8

                C.Button {
                    text: strings.trKey("common.cancel")
                    DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
                }

                C.Button {
                    text: strings.trKey("jobs.removeConfirm.remove")
                    enabled: root.selectedIndexes.length > 0
                    DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
                }
            }

            onAccepted: root.removeSelectedJobs()
            onRejected: close()
        }


		// File dialog for saving selected jobs to JSON.
        P.FileDialog {
            id: saveFileDialog
            title: strings.trKey("jobs.saveJson.title")
            nameFilters: ["JSON Files (*.json)"]
            fileMode: P.FileDialog.SaveFile
            defaultSuffix: "json"
            onAccepted: jobModel.saveToJson(file, selectedIndexes)
        }


		// File dialog for choosing PRN output path when using the simulated printer.
        P.FileDialog {
            id: outputFileDialog
            title: strings.trKey("jobs.prnDestination.title")
            nameFilters: ["PRN Files (*.prn)", "All Files (*)"]
            fileMode: P.FileDialog.SaveFile
	    	defaultSuffix: "prn"
            onAccepted: {
                const outputPath = file
                const job = jobModel.getJob(selectedIndexes[0])

				root.beginOutputProgress("prn")
				
				if (appState.usingMultiInkPrinter == true) {
                    if (!appState.supportsRipProcessing) {
                        toast.show(strings.trKey("jobs.toast.prnUnavailable"))
                        root.endOutputProgress()
                        return
                    }

				    // Clone the job map so we can inject MultiInk-specific fields
					var multiInkJob = Object.assign({}, job)

					// Pass current ink mode into the pipeline
					multiInkJob.inkMode = appState.multiInkInkMode
                    multiInkJob.directPrintSettings = directPrintSettings()

					console.log("Routing to Nocai MultiInk backend with inkMode =", appState.multiInkInkMode)
					printJobMultiInk.runPRNGeneration(multiInkJob, outputPath)
				}
				else {
                    if (!appState.supportsRipProcessing) {
                        toast.show(strings.trKey("jobs.toast.prnUnavailable"))
                        root.endOutputProgress()
                        return
                    }

					console.log("Routing to standard Nocai backend")
			        printJobCMYK.runPRNGeneration(job, outputPath)
				}
            }
        }

		// Connect to PRN completion signal to update UI and notify user.
		Connections {
			target: printJobCMYK

			function onOutputPhaseChanged(phase) {
				if (appState.isGeneratingPRN)
					appState.outputProgressPhase = phase
			}

			function onPrnGenerationFinished(success) {
				root.handlePrnFinished(success)
			}
		}

		Connections {
			target: printJobMultiInk

			function onOutputPhaseChanged(phase) {
				if (appState.isGeneratingPRN)
					appState.outputProgressPhase = phase
			}

			function onPrnGenerationFinished(success) {
				root.handlePrnFinished(success)
			}
		}

		// Lightweight toast for transient feedback.
        Toast {
            id: toast
            parent: Overlay.overlay
        }
    }
    
    // Desktop file drops use the same validated/copying import path as Upload Image.
    DropArea {
        id: imageDropArea
        anchors.fill: parent
        z: 900

        property bool hasSupportedImages: false

        onEntered: function(dragEvent) {
            hasSupportedImages = dragEvent.hasUrls
                    && root.supportedDroppedImages(dragEvent.urls).length > 0
            if (dragEvent.hasUrls)
                dragEvent.accept(Qt.CopyAction)
        }

        onPositionChanged: function(dragEvent) {
            hasSupportedImages = dragEvent.hasUrls
                    && root.supportedDroppedImages(dragEvent.urls).length > 0
            if (dragEvent.hasUrls)
                dragEvent.accept(Qt.CopyAction)
        }

        onExited: hasSupportedImages = false

        onDropped: function(dropEvent) {
            const droppedUrls = dropEvent.urls
            const supportedImages = root.supportedDroppedImages(droppedUrls)
            hasSupportedImages = false
            if (!dropEvent.hasUrls || supportedImages.length === 0) {
                dropEvent.accepted = false
                if (dropEvent.hasUrls)
                    toast.show(strings.trKey("jobs.toast.dropUnsupported"))
                return
            }

            // Force copy semantics: the source files must never be moved or deleted.
            dropEvent.accept(Qt.CopyAction)
            root.createJobsFromDroppedImages(supportedImages)
        }

        Rectangle {
            anchors.fill: parent
            anchors.margins: 18
            visible: imageDropArea.containsDrag
            radius: 14
            color: imageDropArea.hasSupportedImages
                   ? Qt.rgba(root.theme.accent.r, root.theme.accent.g, root.theme.accent.b, 0.20)
                   : Qt.rgba(root.theme.danger.r, root.theme.danger.g, root.theme.danger.b, 0.18)
            border.width: 3
            border.color: imageDropArea.hasSupportedImages ? root.theme.accent : root.theme.danger

            Column {
                anchors.centerIn: parent
                width: Math.min(parent.width - 40, 420)
                spacing: 8

                Label {
                    width: parent.width
                    text: imageDropArea.hasSupportedImages
                          ? strings.trKey("jobs.dropImages")
                          : strings.trKey("jobs.dropUnsupported")
                    color: root.theme.text
                    font.pixelSize: 20
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                }

                Label {
                    width: parent.width
                    text: strings.trKey("jobs.dropImages.formats")
                    color: root.theme.subtext
                    font.pixelSize: 14
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                }
            }
        }
    }

    
    // Full-screen progress overlay while PRN generation runs.
    Item {
        id: spinnerOverlay
		anchors.fill: parent
        visible: appState.isGeneratingPRN
        z: 999

        Rectangle {
            anchors.fill: parent
            color: "#00000088"


			// Simple circular spinner built from rectangles with rotation animation.
			Item {
				width: 64
				height: 64
				anchors.horizontalCenter: parent.horizontalCenter
				anchors.verticalCenter: parent.verticalCenter
                transformOrigin: Item.Center

				RotationAnimator on rotation {
					from: 0
					to: 360
					duration: 1000
					loops: Animation.Infinite
					running: spinnerOverlay.visible
				}

				Rectangle {
					anchors.fill: parent
					radius: width / 2
					border.width: 6
					border.color: "#402DD4BF"
					color: "transparent"
				}

				Rectangle {
					width: 6
					height: parent.height / 2
					anchors.top: parent.top
					anchors.horizontalCenter: parent.horizontalCenter
					color: theme.accent
                    radius: width / 2
				}
			}

            Text {
                text: root.outputProgressText()
                anchors.top: parent.verticalCenter
                anchors.horizontalCenter: parent.horizontalCenter
				color: theme.subtext
                font.pixelSize: 18
                anchors.topMargin: 80
            }
        }
    }
}
