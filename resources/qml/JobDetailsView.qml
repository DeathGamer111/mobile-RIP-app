import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt.labs.platform
import QtCore
import "."

// Job details & settings editor for a single print job.
// Loads/validates artwork, shows live preview, and lets the user tweak resolution, offsets, and edit the job image
Item {
    id: root
    required property StackView stackView
    required property int jobIndex
    required property var jobModel
    required property var appState
    required property Theme theme
    property var jobData: jobModel.getJob(jobIndex)
    property string imagePath: jobData.imagePath           // What the preview <Image> points at (may be a temp PNG for PDFs)
    property string tempPreviewPath: ""                    // Temp file created for PDF preview; cleaned on Back
    property var imageMeta: ({})                           // Metadata from ImageLoader
    property string selectedInputICC: ""                   // When using "Custom ICC" conversion
    property string selectedOutputICC: ""
    property string printedSizeDisplay: strings.trKey("jobDetails.printedSize.unavailable")
    property bool loadingInputICC: true
    property bool waitingForImageImport: false
    property bool syncingOffsetControls: false
    property var mediaSizeOptions: (strings.language, [
        { label: strings.trKey("media.a0"), widthMm: 841, heightMm: 1189, cupsName: "A0" },
        { label: strings.trKey("media.a1"), widthMm: 594, heightMm: 841, cupsName: "A1" },
        { label: strings.trKey("media.a2"), widthMm: 420, heightMm: 594, cupsName: "A2" },
        { label: strings.trKey("media.a3"), widthMm: 297, heightMm: 420, cupsName: "A3" },
        { label: strings.trKey("media.a4"), widthMm: 210, heightMm: 297, cupsName: "A4" },
        { label: strings.trKey("media.a5"), widthMm: 148, heightMm: 210, cupsName: "A5" },
        { label: strings.trKey("media.a6"), widthMm: 105, heightMm: 148, cupsName: "A6" },
        { label: strings.trKey("media.letter"), widthMm: 216, heightMm: 279, cupsName: "Letter" },
        { label: strings.trKey("media.legal"), widthMm: 216, heightMm: 356, cupsName: "Legal" },
        { label: strings.trKey("media.tabloid"), widthMm: 279, heightMm: 432, cupsName: "Tabloid" },
        { label: strings.trKey("media.sign12x18"), widthMm: 305, heightMm: 457, cupsName: "12x18" },
        { label: strings.trKey("media.sign18x24"), widthMm: 457, heightMm: 610, cupsName: "18x24" },
        { label: strings.trKey("media.coroplast24x36"), widthMm: 610, heightMm: 914, cupsName: "24x36" },
        { label: strings.trKey("media.sign24x48"), widthMm: 610, heightMm: 1219, cupsName: "24x48" },
        { label: strings.trKey("media.sign32x48"), widthMm: 813, heightMm: 1219, cupsName: "32x48" },
        { label: strings.trKey("media.custom"), widthMm: -1, heightMm: -1, cupsName: "Custom" }
    ])
    property var whiteModeOptions: (strings.language, [
        { label: strings.trKey("color.option.off"), value: "Off" },
        { label: strings.trKey("color.option.autoUnderbase"), value: "Auto Underbase" },
        { label: strings.trKey("color.option.flood"), value: "Flood" },
        { label: strings.trKey("color.option.plate"), value: "Plate" }
    ])
    property var varnishModeOptions: (strings.language, [
        { label: strings.trKey("color.option.off"), value: "Off" },
        { label: strings.trKey("color.option.overPrintedArea"), value: "Over Printed Area" },
        { label: strings.trKey("color.option.flood"), value: "Flood" },
        { label: strings.trKey("color.option.plate"), value: "Plate" }
    ])
    property var featheringOptions: (strings.language, [
        { label: strings.trKey("feathering.low"), value: 1 },
        { label: strings.trKey("feathering.medium"), value: 2 },
        { label: strings.trKey("feathering.high"), value: 3 }
    ])

    width: parent ? parent.width : 450
    height: parent ? parent.height : 600
	
    // DPI options depend on backend/printer type:
    // - Nocai: Y divisible by 720 (720, 1440, 2160)
    // - MultiInk: Y divisible by 600 (600, 1200, 1800)
    property bool usingMultiInk: appState && appState.usingMultiInkPrinter === true

    property var dpiOptionsNocai:    ["720x720", "720x1440", "720x2160"]
    property var dpiOptionsMultiInk: ["720x600", "720x1200", "720x1800"]
    
	property string whitePlatePath: ""
	property string varnishPlatePath: ""
    
    property var dpiOptions: usingMultiInk ? dpiOptionsMultiInk : dpiOptionsNocai
    
    Rectangle {
		anchors.fill: parent
		color: theme.bg
		z: -1
    }

    // On first show: derive preview path (PDF -> temp PNG), pull metadata, sync DPI widgets, and gate controls against printer caps.
    Component.onCompleted: {
        if (jobData.imagePath !== "") {
            updateMetadata(jobData.imagePath)
            
            if (jobData.imagePath.toLowerCase().endsWith(".pdf")) {
            	const previewPath = imageLoader.renderPdfToPreviewImage(jobData.imagePath)
            	
            	if (previewPath !== "") {
                    imagePath = "file://" + previewPath
                    tempPreviewPath = previewPath
            	} else {
                    console.warn("Failed to regenerate preview for PDF.")
                    imagePath = ""
            	}
            } else {
            	imagePath = jobData.imagePath
            	tempPreviewPath = ""
            }
        }
        
        // Sync DPI dropdown from saved jobData.resolution using correct backend list
        syncResolutionComboToJob()
        updatePrintedSize()
        
        // Sync White/Varnish Plates
        whitePlatePath = jobData.whitePlatePath || ""
		varnishPlatePath = jobData.varnishPlatePath || ""

        if (appState.selectedPrinter.length > 0) {
            safeSelectFirstSupported(profileBox, printJobOutput.supportedColorModes())
            safeSelectFirstSupported(paperSizeBox, printJobOutput.supportedMediaSizes())
		}
    }
    
    
    Connections {
        target: appState
        function onUsingMultiInkPrinterChanged() {
            // Rebind dpiOptions (it depends on usingMultiInk) and resync selection
            syncResolutionComboToJob()
            updatePrintedSize()
        }
    }


    // When navigating back to this screen, refresh preview and derived labels.
    onVisibleChanged: {
        if (visible) {
			jobData = jobModel.getJob(jobIndex)
	        syncOffsetControlsFromJob()
	        whitePlatePath = jobData.whitePlatePath || ""
	        varnishPlatePath = jobData.varnishPlatePath || ""
	        if (jobData.imagePath !== "") {
				updateMetadata(jobData.imagePath)
			}
	        refreshPreview()
	        updatePrintedSize()
        }
    }

	
    // Reload same-path imports without assigning previewImage.source directly;
    // direct assignment would detach its binding from imagePath.
    function refreshPreview() {
        const currentPath = imagePath
        imagePath = ""
        Qt.callLater(function() { imagePath = currentPath })
    }

    function applyJobImagePath(path) {
        jobData.imagePath = path

        // Replacing a PDF creates a new raster preview; discard the previous
        // temporary file before tracking the replacement.
        if (tempPreviewPath !== "") {
            imageLoader.deleteTemporaryFile(tempPreviewPath)
            tempPreviewPath = ""
        }

        if (path.toLowerCase().endsWith(".pdf")) {
            const previewPath = imageLoader.renderPdfToPreviewImage(path)
            if (previewPath !== "") {
                imagePath = "file://" + previewPath
                tempPreviewPath = previewPath
            } else {
                imagePath = ""
                tempPreviewPath = ""
            }
        } else {
            imagePath = path
            tempPreviewPath = ""
        }

        updateMetadata(path)
        updatePrintedSize()
        refreshPreview()
    }

    function importImageForCurrentJob(sourcePath) {
        if (jobModel.updateJobImage(jobIndex, sourcePath)) {
            jobData = jobModel.getJob(jobIndex)
            applyJobImagePath(jobData.imagePath)
            toast.show(strings.trKey("jobDetails.toast.imageImported"))
        } else {
            const message = jobModel.lastError()
            toast.show(message.length > 0 ? message : strings.trKey("jobDetails.toast.imageImportFailed"))
        }
    }

	
    // Capability guard: if no capability list is provided, assume supported.
    function isSupported(value, supportedList) {
        if (!supportedList || supportedList.length === 0)
            return true
        return supportedList.indexOf(value) !== -1
    }

	
    // Pick the first dropdown value that the current printer supports.
    function safeSelectFirstSupported(comboBox, supportedList) {
        if (!supportedList || supportedList.length === 0)
            return
        for (let i = 0; i < comboBox.count; i++) {
            const item = comboBox.model[i]
            const capabilityName = item && item.cupsName ? item.cupsName : item
            if (supportedList.indexOf(capabilityName) !== -1) {
                comboBox.currentIndex = i
                break
            }
        }
    }

    Connections {
        target: imageImportManager
        function onImageReady(localFilePath) {
            if (!root.waitingForImageImport)
                return
            root.waitingForImageImport = false
            root.importImageForCurrentJob(localFilePath)
        }
        function onCanceled() {
            if (!root.waitingForImageImport)
                return
            root.waitingForImageImport = false
        }
        function onFailed(message) {
            if (!root.waitingForImageImport)
                return
            root.waitingForImageImport = false
            toast.show(message)
        }
    }


    // paperSize remains the persisted field name for compatibility; the UI
    // consistently presents it as media size.
    function mediaSizeIndexFromSize(size) {
        for (let i = 0; i < mediaSizeOptions.length; ++i) {
            const option = mediaSizeOptions[i]
            if (option.widthMm === size.width && option.heightMm === size.height)
                return i
        }
        return mediaSizeOptions.length - 1
    }

    function optionIndexForValue(options, value) {
        for (let i = 0; i < options.length; ++i) {
            if (options[i].value === value)
                return i
        }
        return 0
    }

    // Apply the selected media dimensions back into the legacy job field.
    function updateMediaSize() {
        if (paperSizeBox.currentIndex < 0)
            return
        const option = mediaSizeOptions[paperSizeBox.currentIndex]
        if (option.widthMm < 0) {
            jobData.paperSize = Qt.size(customWidth.value, customHeight.value)
        } else {
            jobData.paperSize = Qt.size(option.widthMm, option.heightMm)
        }
    }

	
	// Parse "WxH" combo text and write to jobData.resolution.
	function updateResolution() {
		let dpiText = resolutionComboBox.currentText
		let parts = dpiText.split("x")
		if (parts.length === 2) {
		    jobData.resolution = Qt.size(parseInt(parts[0]), parseInt(parts[1]))
		}
	}
	
	
	function dpiStringFromSize(sz) {
        return (sz ? (sz.width + "x" + sz.height) : "")
    }


    // If a saved DPI doesn't exist in the active list, try to map it to an equivalent
    function mapDpiBetweenBackends(dpiStr) {
        // Common mappings between 720-based Y and 600-based Y.
        // You can extend this if you add more presets later.
        const mapToMulti = {
            "720x720":  "720x600",
            "720x1440": "720x1200",
            "720x2160": "720x1800"
        }
        const mapToNocai = {
            "720x600":  "720x720",
            "720x1200": "720x1440",
            "720x1800": "720x2160"
        }

        if (usingMultiInk && mapToMulti[dpiStr]) return mapToMulti[dpiStr]
        if (!usingMultiInk && mapToNocai[dpiStr]) return mapToNocai[dpiStr]
        return dpiStr
    }


    function syncResolutionComboToJob() {
        const saved = dpiStringFromSize(jobData.resolution)
        let idx = dpiOptions.indexOf(saved)

        if (idx < 0) {
            const mapped = mapDpiBetweenBackends(saved)
            idx = dpiOptions.indexOf(mapped)
            if (idx >= 0) {
                // Update the job to the mapped value so the UI + saved state stay aligned
                const parts = mapped.split("x")
                if (parts.length === 2) {
                    jobData.resolution = Qt.size(parseInt(parts[0]), parseInt(parts[1]))
                }
            }
        }

        // Fallback defaults per backend
        if (idx < 0) {
            const fallback = usingMultiInk ? "720x1200" : "720x1440"
            idx = dpiOptions.indexOf(fallback)
            const parts = fallback.split("x")
            jobData.resolution = Qt.size(parseInt(parts[0]), parseInt(parts[1]))
        }

        resolutionComboBox.currentIndex = idx
    }


	// Compute physical size from the artwork density. Files without trustworthy
	// density retain the legacy baseline for the selected printer family.
	function updatePrintedSize() {
		const wPx = imageMeta.width || 0
		const hPx = imageMeta.height || 0
		const fallbackDpi = usingMultiInk ? 600 : 720
		const inputXDpi = imageMeta.hasEmbeddedDpi ? imageMeta.xDpi : fallbackDpi
		const inputYDpi = imageMeta.hasEmbeddedDpi ? imageMeta.yDpi : fallbackDpi

		if (wPx > 0 && hPx > 0 && inputXDpi > 0 && inputYDpi > 0) {
		    const printedWmm = ((wPx * 25.4) / inputXDpi).toFixed(1)
		    const printedHmm = ((hPx * 25.4) / inputYDpi).toFixed(1)

		    printedSizeDisplay = strings.trKey("jobDetails.printedSize.prefix")
                                 + printedWmm + " mm × " + printedHmm + " mm"
		} else {
		    printedSizeDisplay = strings.trKey("jobDetails.printedSize.unavailable")
		}
	}

	
    // Refresh both controls as one logical operation. Assigning X triggers its
    // valueChanged signal, so callbacks must be suspended until Y is restored.
    function syncOffsetControlsFromJob() {
        const savedOffset = jobData.offset || Qt.point(0, 0)
        const savedX = savedOffset.x
        const savedY = savedOffset.y
        syncingOffsetControls = true
        offsetXSpin.value = savedX
        offsetYSpin.value = savedY
        syncingOffsetControls = false
        jobData.offset = Qt.point(savedX, savedY)
    }

    // Persist user-entered offset spinboxes to the local job data.
    function updateOffset() {
        if (syncingOffsetControls)
            return
        jobData.offset = Qt.point(offsetXSpin.value, offsetYSpin.value)
    }


    // Persist white/varnish/profile selections.
    function selectedOptionValue(comboBox, options) {
        return comboBox.currentIndex >= 0 ? options[comboBox.currentIndex].value : ""
    }
    function updateWhiteStrategy() { jobData.whiteStrategy = selectedOptionValue(whiteBox, whiteModeOptions) }
    function updateVarnishType() { jobData.varnishType = selectedOptionValue(varnishBox, varnishModeOptions) }
    function updateColorProfile() { jobData.colorProfile = profileBox.currentText }

	// Update White and Varnish Plate UI after loading in plate image
    function updateWhitePlatePath(path) {
		whitePlatePath = path
		jobData.whitePlatePath = path
	}

	function updateVarnishPlatePath(path) {
		varnishPlatePath = path
		jobData.varnishPlatePath = path
	}
    
    	
    // Fetch fresh metadata (dimensions, channels, color space guess, etc.).
    function updateMetadata(path) {
        imageMeta = imageLoader.extractMetadata(path)
    }

	
    // Main scroller for the form content; reduces flick velocity for desktop feel.
    ColumnLayout {
        width: parent.width
    	height: parent.height
    	
		Rectangle {
			id: headerBar
			Layout.fillWidth: true
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
					    onClicked: {
				        if (tempPreviewPath !== "") {
				            imageLoader.deleteTemporaryFile(tempPreviewPath)
				            tempPreviewPath = ""
				        }
				        stackView.pop()
				    }
				}

				Item { Layout.fillWidth: true }

				Label {
				    text: strings.trKey("jobDetails.title")
				    color: theme.text
				    font.pixelSize: root.theme.headerTitleSize(root.width)
				    font.weight: Font.Medium
				    horizontalAlignment: Text.AlignHCenter
				    verticalAlignment: Text.AlignVCenter
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
				    onClicked: {
			        jobModel.updateJob(jobIndex, {
			            name: jobNameField.text,
			            imagePath: jobData.imagePath,
			            paperSize: jobData.paperSize,
			            mediaHeightMm: mediaHeightEnabled.checked ? mediaHeightSpin.value / 10.0 : -1,
			            resolution: (function() {
			                let parts = resolutionComboBox.currentText.split("x")
			                return (parts.length === 2)
			                    ? Qt.size(parseInt(parts[0]), parseInt(parts[1]))
			                    : Qt.size(720, usingMultiInk ? 1200 : 1440)
			            })(),
			            offset: Qt.point(offsetXSpin.value, offsetYSpin.value),
			            feathering: selectedOptionValue(featheringBox, featheringOptions),
			            whiteStrategy: selectedOptionValue(whiteBox, whiteModeOptions),
			            varnishType: selectedOptionValue(varnishBox, varnishModeOptions),
			            colorProfile: profileBox.currentText,
			            whitePlatePath: whitePlatePath,
			            varnishPlatePath: varnishPlatePath
			        })
				        toast.show(strings.trKey("jobDetails.saved"))
				    }
				}
			}

			// optional: subtle divider line like a toolbar
			Rectangle {
				anchors.left: parent.left
				anchors.right: parent.right
				anchors.bottom: parent.bottom
				height: 1
				color: theme.divider
				opacity: 0.8
			}
		}

        ScrollView {
        
		id: scrollView
		Layout.alignment: Qt.AlignHCenter
		Layout.fillHeight: true
		Layout.fillWidth: true

		ScrollBar.vertical.interactive: true
		ScrollBar.vertical.policy: ScrollBar.AsNeeded
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        contentWidth: availableWidth

		// Wait until flickableItem is ready
		Connections {
			target: scrollView
			function onContentItemChanged() {
				if (scrollView.flickableItem) {
					scrollView.flickableItem.flickDeceleration = 500
					scrollView.flickableItem.maximumFlickVelocity = 8000
				}
			}
		}


	    // Card-like container for all job controls.
		Column {
			width: scrollView.availableWidth
			spacing: 0

			Pane {
			width: theme.boundedWidth(parent.width, 450)
			anchors.horizontalCenter: parent.horizontalCenter
			topPadding: theme.mobile ? theme.spaceMd : 20
			leftPadding: theme.panePadding
			rightPadding: theme.panePadding
			bottomPadding: theme.mobile ? theme.spaceMd : 20

			background: Rectangle {
				color: theme.surface
				radius: theme.cardRadius
				border.width: 1
				border.color: theme.divider
			}

                    ColumnLayout {
			id: columnContent
			width: parent.width
			spacing: theme.mobile ? theme.spaceMd : 16

			// Job name label
			Label {
				text: strings.trKey("jobDetails.jobName")
			}

			// Job name field
			TextField {
				id: jobNameField
				text: jobData.name
				placeholderText: strings.trKey("jobDetails.jobName.placeholder")
				Layout.fillWidth: true
			}

			// Artwork preview area; shows temp PNG for PDFs.
            Rectangle {
				id: imageContainer
			    Layout.fillWidth: true
			    height: theme.mobile ? 180 : 260
				color: theme.surface
			    border.color: theme.divider
			    radius: 10
                Layout.alignment: Qt.AlignHCenter
                clip: true

                Image {
                    id: previewImage
                    anchors.centerIn: parent
                    anchors.margins: 8
                    source: imagePath
                    fillMode: Image.PreserveAspectFit
                    width: parent.width
                    height: parent.height
                    smooth: true
                    visible: source !== ""
                    cache: false
                    clip: true

                    opacity: visible ? 1.0 : 0.0
                    Behavior on opacity { NumberAnimation { duration: 200 } }
                }

				Text {
					anchors.centerIn: parent
					text: jobData.imagePath === "" ? strings.trKey("jobDetails.noImage") : ""
					visible: imagePath == ""
					color: theme.subtext
				}
			}

			// Artwork actions: load, open editor, open imposition tool.
            GridLayout {
                columns: theme.actionColumns(width, 3, 118)
                rowSpacing: 10
                columnSpacing: 12
                Layout.fillWidth: true

                ThemedButton {
                    text: strings.trKey("jobDetails.uploadImage")
                    theme: root.theme
                    padding: 12
                    Layout.fillWidth: true
					font.pixelSize: 14	
                    onClicked: {
                        if (imageImportManager.supportsNativeImagePicker) {
                            root.waitingForImageImport = true
                            imageImportManager.openImageImportChooser()
                        } else {
                            imageDialog.open()
                        }
                    }
                }

                ThemedButton {
                    text: strings.trKey("jobDetails.editImage")
                    enabled: imagePath !== ""
                    theme: root.theme
					padding: 12
                    Layout.fillWidth: true
					font.pixelSize: 14
	                    onClicked: {
	                        stackView.push("qrc:/qml/ImageEditorView.qml", {
	                            "imagePath": imagePath,
	                            "stackView": stackView,
	                            "theme": root.theme
	                        })
	                    }
	                }

                ThemedButton {
                    text: strings.trKey("jobDetails.editImposition")
                    enabled: imagePath !== ""
                    theme: root.theme
                    padding: 12
                    Layout.fillWidth: true
                    font.pixelSize: 14
                    onClicked: {
							// Carry any current numeric offset into the same model field
							// that Imposition edits, even before the full details form is saved.
							jobModel.updateJob(jobIndex, {
								offset: Qt.point(offsetXSpin.value, offsetYSpin.value)
							})
							jobData = jobModel.getJob(jobIndex)
							stackView.push("qrc:/qml/ImpositionView.qml", {
								"jobIndex": jobIndex,
								"jobModel": jobModel,
									"stackView": stackView,
									"initialImagePath": imagePath,
									"fallbackInputDpi": usingMultiInk ? 600 : 720,
									"theme": root.theme
							})
	                    }
                }
            }

			// File picker for artwork; validates and builds a PDF preview if needed.
            FileDialog {
                id: imageDialog
                title: strings.trKey("jobDetails.selectImage.title")
                nameFilters: ["Images (*.png *.jpg *.jpeg *.bmp *.tif *.tiff *.svg *.pdf)"]
                
                onAccepted: {
                    importImageForCurrentJob(String(file))
				}
			}
						
			// Printer-related settings: media, DPI, offsets, white/varnish.
            Label {
                text: strings.trKey("jobDetails.printerSettings")
                font.pixelSize: theme.sectionTitleSize
                horizontalAlignment: Text.AlignHCenter
                Layout.alignment: Qt.AlignHCenter
            }
            
            Rectangle {
				Layout.fillWidth: true
				height: 1
				color: theme.divider
				opacity: 0.8
			}

            GroupBox {
                Layout.fillWidth: true

                ColumnLayout {
                    width: parent.width
                    spacing: 10

                    Label { text: strings.trKey("jobDetails.mediaSize") }
                    ComboBox {
                        id: paperSizeBox
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        Layout.maximumWidth: parent.width
                        model: root.mediaSizeOptions
                        textRole: "label"
                        currentIndex: mediaSizeIndexFromSize(jobData.paperSize)
                        onActivated: updateMediaSize()

                        enabled: appState.usingSimulatedPrinter
                                 || appState.selectedPrinter.length === 0
                                 || currentIndex < 0
                                 || isSupported(root.mediaSizeOptions[currentIndex].cupsName,
                                                printJobOutput.supportedMediaSizes())
                    }
								
				// Custom size widgets appear only when needed.
                ColumnLayout {
                    visible: paperSizeBox.currentIndex === root.mediaSizeOptions.length - 1
                    Layout.fillWidth: true
                    spacing: 8

                    Label { text: strings.trKey("jobDetails.customMediaSize") }

                    RowLayout {
                        spacing: 8
                        Layout.fillWidth: true

                        SpinBox {
                            id: customWidth
                            from: 10; to: 2000
                            value: jobData.paperSize.width
                            editable: true
                            Layout.fillWidth: true
                            onValueChanged: jobData.paperSize.width = value
                        }

                        Label { text: "×" }

                        SpinBox {
                            id: customHeight
                            from: 10; to: 2000
                            value: jobData.paperSize.height
                            editable: true
                            Layout.fillWidth: true
                            onValueChanged: jobData.paperSize.height = value
                        }
                    }
                }


				Label { text: strings.trKey("jobDetails.outputDpi") }
				ComboBox {
					id: resolutionComboBox
					Layout.fillWidth: true
					Layout.minimumWidth: 0
					Layout.maximumWidth: parent.width
					model: dpiOptions
					currentIndex: -1

					onCurrentIndexChanged: {
						updateResolution()
						updatePrintedSize()
					}
				}

                CheckBox {
                    id: mediaHeightEnabled
                    text: strings.trKey("jobDetails.mediaHeight.enable")
                    checked: jobData.mediaHeightMm !== undefined && jobData.mediaHeightMm >= 0
                    onToggled: jobData.mediaHeightMm = checked ? mediaHeightSpin.value / 10.0 : -1
                }

                RowLayout {
                    Layout.fillWidth: true
                    enabled: mediaHeightEnabled.checked

                    Label { text: strings.trKey("jobDetails.mediaHeight") }
                    SpinBox {
                        id: mediaHeightSpin
                        Layout.fillWidth: true
                        from: 0
                        to: 1520
                        stepSize: 1
                        editable: true
                        value: jobData.mediaHeightMm !== undefined && jobData.mediaHeightMm >= 0
                               ? Math.round(jobData.mediaHeightMm * 10) : 0
                        textFromValue: function(value, locale) {
                            return Number(value / 10.0).toLocaleString(locale, 'f', 1)
                        }
                        valueFromText: function(text, locale) {
                            return Math.round(Number.fromLocaleString(locale, text) * 10.0)
                        }
                        validator: DoubleValidator {
                            bottom: 0.0
                            top: 152.0
                            decimals: 1
                            notation: DoubleValidator.StandardNotation
                        }
                        contentItem: TextInput {
                            z: 2
                            text: mediaHeightSpin.displayText
                            color: theme.text
                            selectionColor: theme.accent
                            selectedTextColor: theme.bg
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            selectByMouse: true
                            validator: mediaHeightSpin.validator
                            inputMethodHints: Qt.ImhFormattedNumbersOnly
                        }
                        onValueModified: {
                            if (mediaHeightEnabled.checked)
                                jobData.mediaHeightMm = value / 10.0
                        }
                    }
                }

                Label {
                    text: strings.trKey("jobDetails.mediaHeight.help")
                    visible: mediaHeightEnabled.checked
                    font.italic: true
                    font.pointSize: 10
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }

				// Calculated printed size hint for the user.
				Label {
					id: printedSizeLabel
					text: printedSizeDisplay
					font.italic: true
					font.pointSize: 10
					wrapMode: Text.Wrap
				}

                Label { text: strings.trKey("jobDetails.offset") }
                RowLayout {
                    spacing: 8
                    Layout.fillWidth: true

                    SpinBox {
                        id: offsetXSpin
                        from: 0; to: 10000
                        value: jobData.offset.x
                        editable: true
                        validator: IntValidator { bottom: 0 }
                        Layout.fillWidth: true
                        onValueChanged: updateOffset()
                    }

                    Label { text: "×" }

                    SpinBox {
                        id: offsetYSpin
                        from: 0; to: 10000
                        value: jobData.offset.y
                        editable: true
                        validator: IntValidator { bottom: 0 }
                        Layout.fillWidth: true
                        onValueChanged: updateOffset()
                    }
                }

                Label { text: strings.trKey("jobDetails.feathering") }
                ComboBox {
                    id: featheringBox
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.maximumWidth: parent.width
                    model: root.featheringOptions
                    textRole: "label"
                    currentIndex: optionIndexForValue(
                        root.featheringOptions,
                        jobData.feathering !== undefined ? jobData.feathering : 2)
                    onActivated: jobData.feathering = selectedOptionValue(
                        featheringBox, root.featheringOptions)
                }

					Label { text: strings.trKey("jobDetails.whiteMode") }
				ComboBox {
					id: whiteBox
					Layout.fillWidth: true
					Layout.minimumWidth: 0
					Layout.maximumWidth: parent.width
					model: root.whiteModeOptions
                    textRole: "label"
					currentIndex: optionIndexForValue(root.whiteModeOptions, jobData.whiteStrategy)
					onActivated: updateWhiteStrategy()
				}
				
				ColumnLayout {
					visible: selectedOptionValue(whiteBox, root.whiteModeOptions) === "Plate"
					Layout.fillWidth: true
					spacing: 8

					Label {
						text: strings.trKey("jobDetails.whitePlateFile")
						color: theme.text
					}

					RowLayout {
						Layout.fillWidth: true
						spacing: 8

						TextField {
							Layout.fillWidth: true
                            Layout.minimumWidth: 0
							text: whitePlatePath
							readOnly: true
							placeholderText: strings.trKey("jobDetails.whitePlate.placeholder")
						}

						ThemedButton {
							text: strings.trKey("common.browse")
							theme: root.theme
							onClicked: whitePlateDialog.open()
						}
					}
				}
				
				Text {
					visible: selectedOptionValue(whiteBox, root.whiteModeOptions) === "Auto Underbase"
					text: strings.trKey("jobDetails.modeDefaults")
					color: theme.subtext
					wrapMode: Text.Wrap
					font.pixelSize: 12
				}
				
				FileDialog {
					id: whitePlateDialog
					title: strings.trKey("jobDetails.whitePlate.title")
					nameFilters: ["Images (*.png *.jpg *.jpeg *.bmp *.tif *.tiff)"]
					fileMode: FileDialog.OpenFile

					onAccepted: {
						updateWhitePlatePath(String(file))
					}
				}

				Label { text: strings.trKey("jobDetails.varnishMode") }
				ComboBox {
					id: varnishBox
					Layout.fillWidth: true
					Layout.minimumWidth: 0
					Layout.maximumWidth: parent.width
					model: root.varnishModeOptions
                    textRole: "label"
					currentIndex: optionIndexForValue(root.varnishModeOptions, jobData.varnishType)
					onActivated: updateVarnishType()
				}
				
				ColumnLayout {
					visible: selectedOptionValue(varnishBox, root.varnishModeOptions) === "Plate"
					Layout.fillWidth: true
					spacing: 8

					Label {
						text: strings.trKey("jobDetails.varnishPlateFile")
						color: theme.text
					}

					RowLayout {
						Layout.fillWidth: true
						spacing: 8

						TextField {
							Layout.fillWidth: true
                            Layout.minimumWidth: 0
							text: varnishPlatePath
							readOnly: true
							placeholderText: strings.trKey("jobDetails.varnishPlate.placeholder")
						}

						ThemedButton {
							text: strings.trKey("common.browse")
							theme: root.theme
							onClicked: varnishPlateDialog.open()
						}
					}
				}
				
				Text {
					visible: selectedOptionValue(varnishBox, root.varnishModeOptions) === "Over Printed Area"
					text: strings.trKey("jobDetails.modeDefaults")
					color: theme.subtext
					wrapMode: Text.Wrap
					font.pixelSize: 12
				}
				
				FileDialog {
					id: varnishPlateDialog
					title: strings.trKey("jobDetails.varnishPlate.title")
					nameFilters: ["Images (*.png *.jpg *.jpeg *.bmp *.tif *.tiff)"]
					fileMode: FileDialog.OpenFile

					onAccepted: {
						updateVarnishPlatePath(String(file))
					}
				}
                                
				// Color space selection and optional ICC-driven conversion.
                Label { text: strings.trKey("jobDetails.colorProfile") }
                ComboBox {
                    id: profileBox
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.maximumWidth: parent.width
                    model: ["sRGB", "AdobeRGB", "CMYK", "Lc+Lm+Ly+Lk", "Grayscale", "Indexed8", "Indexed16", "Custom ICC"]
                    currentIndex: model.indexOf(jobData.colorProfile)
                    enabled: appState.selectedPrinter.length === 0 || isSupported(currentText, printJobOutput.supportedColorModes())
                }

				// Action row for profile-driven conversions.
                RowLayout {
                    spacing: 8
                    Layout.fillWidth: true

                    ThemedButton {
                        Layout.fillWidth: true
						visible: profileBox.currentText === "Custom ICC"
						text: strings.trKey("jobDetails.loadInputIcc")
						theme: root.theme
						onClicked: { loadingInputICC = true; iccDialog.open() }
				    }

				    ThemedButton {
                        Layout.fillWidth: true
						visible: profileBox.currentText === "Custom ICC"
						text: strings.trKey("jobDetails.loadOutputIcc")
						theme: root.theme
						onClicked: { loadingInputICC = false; iccDialog.open() }
				    }

				    ThemedButton {
						id: convertButton
                        Layout.fillWidth: true
						text: strings.trKey("jobDetails.convertColorspace")
						theme: root.theme
						visible: profileBox.currentText !== jobData.colorProfile
						enabled: imagePath !== ""

                        onClicked: {
                            var result = false
                            if (profileBox.currentText === "Custom ICC") {
                                result = colorProfile.convertWithICCProfilesCMYK(imagePath, imagePath, selectedInputICC, selectedOutputICC)
                            } else {
                                result = colorProfile.convertToColorspace(imagePath, profileBox.currentText)
                            }

                            if (result) {
                                updateColorProfile()
                                refreshPreview()
                                toast.show(strings.trKey("jobDetails.toast.colorConverted"))
                            } else {
                                toast.show(strings.trKey("jobDetails.toast.colorFailed"))
                            }
                        }
                    }
                }

				// Show selected ICCs to show user what ICC will be applied.
                Text {
                    text: strings.trKey("jobDetails.inputPrefix") + selectedInputICC
                    visible: profileBox.currentText === "Custom ICC" && selectedInputICC !== ""
    				color: theme.subtext
                    wrapMode: Text.Wrap
                    font.pixelSize: 12
                }

                Text {
                    text: strings.trKey("jobDetails.outputPrefix") + selectedOutputICC
                    visible: profileBox.currentText === "Custom ICC" && selectedOutputICC !== ""
    				color: theme.subtext
                    wrapMode: Text.Wrap
                    font.pixelSize: 12
                }

					// Shared ICC picker; writes to input or output depending on the toggle.
		            FileDialog {
		                id: iccDialog
		                title: strings.trKey("jobDetails.selectIcc.title")
		                nameFilters: ["ICC Profiles (*.icc *.icm)"]
		                onAccepted: {
		                    if (loadingInputICC) {
		                        selectedInputICC = file
			                    } else {
			                        selectedOutputICC = file
			                    }
		                }
		            }
		        }
			}
                        
                        Rectangle {
							Layout.fillWidth: true
							height: 1
							color: theme.divider
							opacity: 0.8
						}

						// Raw metadata dump (only shows keys that exist).
                        GroupBox {
                            title: strings.trKey("jobDetails.metadata.title")
                            Layout.fillWidth: true
                            visible: Object.keys(imageMeta).length > 0

                            Column {
                                width: parent.width
                                spacing: 4

                                // Always shown if present
                                Text { text: strings.trKey("jobDetails.metadata.name") + imageMeta.name; color: theme.text; visible: imageMeta.name !== undefined }
                                Text { text: strings.trKey("jobDetails.metadata.size") + imageMeta.size + strings.trKey("jobDetails.metadata.bytesSuffix"); color: theme.text; visible: imageMeta.size !== undefined }
                                Text { text: strings.trKey("jobDetails.metadata.dimensions") + imageMeta.width + " x " + imageMeta.height; color: theme.text; visible: imageMeta.width !== undefined && imageMeta.height !== undefined }
                                Text { text: strings.trKey("jobDetails.metadata.channels") + imageMeta.channels; color: theme.text; visible: imageMeta.channels !== undefined }
                                Text { text: strings.trKey("jobDetails.metadata.format") + imageMeta.format; color: theme.text; visible: imageMeta.format !== undefined }
                                Text { text: strings.trKey("jobDetails.metadata.dpi") + imageMeta.dpi; color: theme.text; visible: imageMeta.dpi !== undefined }
                                Text { text: strings.trKey("jobDetails.metadata.colorProfile") + imageMeta.colorProfile; color: theme.text; visible: imageMeta.colorProfile !== undefined }

                                // SVG
                                Text { text: strings.trKey("jobDetails.metadata.svgSize") + imageMeta.svgWidth + " x " + imageMeta.svgHeight; color: theme.text; visible: imageMeta.svgWidth !== undefined && imageMeta.svgHeight !== undefined }
                                Text { text: strings.trKey("jobDetails.metadata.svgTitle") + imageMeta.svgTitle; color: theme.text; visible: imageMeta.svgTitle !== undefined }

                                // PDF
                                Text { text: strings.trKey("jobDetails.metadata.pdfVersion") + imageMeta.pdfVersion; color: theme.text; visible: imageMeta.pdfVersion !== undefined }
                                Text { text: strings.trKey("jobDetails.metadata.pdfTitle") + imageMeta.pdfTitle; color: theme.text; visible: imageMeta.pdfTitle !== undefined }
                                Text { text: strings.trKey("jobDetails.metadata.pageCount") + imageMeta.pageCount; color: theme.text; visible: imageMeta.pageCount !== undefined }
                            }
                        }
                    }
                }
            }
        }

        Toast {
            id: toast
            parent: Overlay.overlay
        }
    }
}
