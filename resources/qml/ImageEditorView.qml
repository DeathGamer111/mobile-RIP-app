import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt.labs.platform
import "."


/* ImageEditorView.qml
 * Lightweight, inline editor for basic raster edits using ImageEditor C++ backend.
 * Workflow:
 *   1) Load original → save a temp working copy
 *   2) Apply edits to temp; preview updates live
 *   3) Save commits temp → original path; Back cleans temp on exit
 */
Page {
    id: editorPage
    required property string imagePath
    required property StackView stackView
    required property Theme theme

    background: Rectangle { color: theme.bg }
    
    readonly property int cardWidthNarrow: 520
	readonly property int cardWidthMedium: 640
	readonly property int cardWidthWide: 760

    // Temp working copy path (same extension as source)
    property string tempPath: {
        let parts = imagePath.split(".")
        let ext = parts.length > 1 ? parts[parts.length - 1] : "png"
        return imagePath + ".edit_tmp." + ext
    }

    // Edit/session state
    property bool isDirty: false
    property bool editorReady: false
    property bool editorStarting: true
    property string currentTool: "none"

    // Crop tool state (editor units / preview overlay)
    property real cropX: 50
    property real cropY: 50
    property real cropW: 100
    property real cropH: 100

    // Enhancement sliders (UI-side)
    property real brightness: 0
    property real contrast: 0
    property real hue: 0
    property real saturation: 0
    property real sharpness: 0
    property real gamma: 0

    // Sync resize spin boxes with actual image size
    function refreshImageSize() {
        resizeWidthSpin.value = imageEditor.getImageWidth()
        resizeHeightSpin.value = imageEditor.getImageHeight()
    }

    function doSave() {
        if (imageEditor.saveImage(imagePath)) {
            isDirty = false
            toast.show(strings.trKey("imageEditor.toast.saved"))
        } else {
            toast.show(strings.trKey("imageEditor.toast.saveFailed"))
        }
    }

    function doBack() {
        if (isDirty) {
            toast.show(strings.trKey("imageEditor.toast.unsavedBack"))
            isDirty = false
        } else {
            cleanupAndExit()
        }
    }

    // Stage the working copy after the page has completed its first layout.
    // Keeping the preview source empty until the file exists prevents the
    // Android scene graph from racing the ImageMagick write during navigation.
    function beginEditorSession() {
        if (imageEditor.loadImage(imagePath)) {
            if (imageEditor.saveImage(tempPath) && imageEditor.loadImage(tempPath)) {
                editorReady = true
                editorStarting = false
                refreshImage()
                refreshImageSize()
            } else {
                editorStarting = false
                console.warn("Failed to load temp image for editing")
                toast.show(strings.trKey("imageEditor.toast.saveFailed"))
            }
        } else {
            editorStarting = false
            console.warn("Failed to load image for editing")
            toast.show(strings.trKey("imageEditor.toast.saveFailed"))
        }
    }

    Component.onCompleted: Qt.callLater(beginEditorSession)

    // ---------- Header (Back / Title / Save) ----------
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
                theme: editorPage.theme
                Layout.preferredWidth: editorPage.theme.headerButtonWidth(editorPage.width)
                padding: 12
                font.pixelSize: 15
                onClicked: editorPage.doBack()
            }

            Item { Layout.fillWidth: true }

            Label {
                text: strings.trKey("imageEditor.title")
                color: theme.text
                font.pixelSize: editorPage.theme.headerTitleSize(editorPage.width)
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
                theme: editorPage.theme
                Layout.preferredWidth: editorPage.theme.headerButtonWidth(editorPage.width)
                padding: 12
                font.pixelSize: 15
                onClicked: editorPage.doSave()
            }
        }
    }

    // ---------- Main content ----------
    ColumnLayout {
        anchors.top: headerBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: bottomBar.top
        anchors.margins: theme.pageMargin
        spacing: 12

        // Preview pane with optional crop overlay
        Pane {
            Layout.fillWidth: true
            Layout.preferredHeight: theme.mobile ? 190 : 260
            padding: theme.panePadding

            background: Rectangle {
                color: theme.surface
                radius: 12
                border.width: 1
                border.color: theme.divider
            }

            Rectangle {
                id: imageContainer
                anchors.fill: parent
                color: theme.surface2
                border.color: theme.divider
                radius: 10
                clip: true

                Image {
                    id: imagePreview
                    source: ""
                    fillMode: Image.PreserveAspectFit
                    smooth: true
                    cache: false
                    anchors.fill: parent
                }

                Rectangle {
                    x: cropX
                    y: cropY
                    width: cropW
                    height: cropH
                    color: "transparent"
                    border.color: theme.danger
                    border.width: 2
                    visible: currentTool === "crop"
                }
            }
        }

        // Tools panel (scrollable): View, Transform, Enhance, Effects
        ScrollView {
            id: scrollView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            ScrollBar.vertical.interactive: true
            ScrollBar.vertical.policy: ScrollBar.AsNeeded
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            contentWidth: availableWidth

            Component.onCompleted: contentItem.flickableDirection = Flickable.VerticalFlick

            ColumnLayout {
                width: scrollView.availableWidth
                spacing: 14
                Layout.alignment: Qt.AlignHCenter

                // =========================
                // View
                // =========================
                Pane {
                    Layout.fillWidth: true
                    padding: theme.panePadding

                    background: Rectangle {
                        color: theme.surface
                        radius: 12
                        border.width: 1
                        border.color: theme.divider
                    }

                    ColumnLayout {
						anchors.horizontalCenter: parent.horizontalCenter
						width: theme.boundedWidth(parent.width, editorPage.cardWidthNarrow)
						spacing: 12

						Label {
							text: strings.trKey("imageEditor.view")
							color: theme.text
							font.pixelSize: theme.sectionTitleSize
							font.weight: Font.Medium
							Layout.alignment: Qt.AlignHCenter
						}

						Rectangle { height: 1; Layout.fillWidth: true; color: theme.divider; opacity: 0.8 }

						GridLayout {
							Layout.fillWidth: true
                            columns: theme.actionColumns(width, 3, 118)
							columnSpacing: 10
                            rowSpacing: 10

							ThemedButton { text: strings.trKey("imageEditor.originalSize"); theme: editorPage.theme; Layout.fillWidth: true; onClicked: { apply("resizeOriginal"); refreshImageSize() } }
							ThemedButton { text: strings.trKey("imageEditor.halfSize");     theme: editorPage.theme; Layout.fillWidth: true; onClicked: { apply("resizeHalf"); refreshImageSize() } }
							ThemedButton { text: strings.trKey("imageEditor.doubleSize");   theme: editorPage.theme; Layout.fillWidth: true; onClicked: { apply("resizeDouble"); refreshImageSize() } }
						}

						Label {
							text: strings.trKey("imageEditor.resizeTitle")
							color: theme.subtext
							Layout.alignment: Qt.AlignHCenter
						}

						ColumnLayout {
							Layout.fillWidth: true
							spacing: 8

							RowLayout {
								Layout.fillWidth: true
								spacing: 8

								SpinBox {
									id: resizeWidthSpin
									Layout.fillWidth: true
									Layout.minimumWidth: 0
									from: 1; to: 10000
									value: 100
									editable: true
									validator: IntValidator { bottom: 1 }
								}

								Label {
									text: "×"
									color: theme.text
									horizontalAlignment: Text.AlignHCenter
									Layout.alignment: Qt.AlignVCenter
								}

								SpinBox {
									id: resizeHeightSpin
									Layout.fillWidth: true
									Layout.minimumWidth: 0
									from: 1; to: 10000
									value: 100
									editable: true
									validator: IntValidator { bottom: 1 }
								}
							}

							ThemedButton {
								text: strings.trKey("imageEditor.resize")
								theme: editorPage.theme
								Layout.fillWidth: true
								onClicked: apply("resize", { x: resizeWidthSpin.value, y: resizeHeightSpin.value })
							}
						}
					}
                }

                // =========================
                // Transform
                // =========================
                Pane {
                    Layout.fillWidth: true
                    padding: theme.panePadding

                    background: Rectangle {
                        color: theme.surface
                        radius: 12
                        border.width: 1
                        border.color: theme.divider
                    }

					ColumnLayout {
						anchors.horizontalCenter: parent.horizontalCenter
						width: theme.boundedWidth(parent.width, editorPage.cardWidthNarrow)
						spacing: 12

						Label {
							text: strings.trKey("imageEditor.transform")
							color: theme.text
							font.pixelSize: theme.sectionTitleSize
							font.weight: Font.Medium
							Layout.alignment: Qt.AlignHCenter
						}

						Rectangle { height: 1; Layout.fillWidth: true; color: theme.divider; opacity: 0.8 }

						GridLayout {
							id: transformGrid
							Layout.fillWidth: true
							Layout.alignment: Qt.AlignHCenter
							rowSpacing: 10
							columnSpacing: 10

							// 4 buttons total
							// 1 row if wide enough, otherwise 2×2
							columns: theme.mobile ? 2 : theme.gridColumns(parent.width, 4, 104)

							// Match Effects sizing
							readonly property int btnW: Math.floor(
								(parent.width - (columns - 1) * columnSpacing) / columns
							)
							readonly property int btnH: 40

							ThemedButton {
								text: strings.trKey("imageEditor.flipH")
								theme: editorPage.theme
								Layout.preferredWidth: transformGrid.btnW
								Layout.preferredHeight: transformGrid.btnH
								onClicked: apply("flip", "horizontal")
							}

							ThemedButton {
								text: strings.trKey("imageEditor.flipV")
								theme: editorPage.theme
								Layout.preferredWidth: transformGrid.btnW
								Layout.preferredHeight: transformGrid.btnH
								onClicked: apply("flip", "vertical")
							}

							ThemedButton {
								text: strings.trKey("imageEditor.rotate")
								theme: editorPage.theme
								Layout.preferredWidth: transformGrid.btnW
								Layout.preferredHeight: transformGrid.btnH
								onClicked: apply("rotate", 90)
							}
							
							ThemedButton {
								text: strings.trKey("imageEditor.crop")
								theme: editorPage.theme
								Layout.preferredWidth: transformGrid.btnW
								Layout.preferredHeight: transformGrid.btnH
								onClicked: apply("crop", { x: cropX, y: cropY, w: cropW, h: cropH })
							}
						}
					}
                }

                // =========================
                // Enhance
                // =========================
                Pane {
                    Layout.fillWidth: true
                    padding: theme.panePadding

                    background: Rectangle {
                        color: theme.surface
                        radius: 12
                        border.width: 1
                        border.color: theme.divider
                    }

                    ColumnLayout {
						anchors.horizontalCenter: parent.horizontalCenter
						width: theme.boundedWidth(parent.width, editorPage.cardWidthMedium)
						spacing: 12

                        Label {
                            text: strings.trKey("imageEditor.enhance")
                            color: theme.text
                            font.pixelSize: theme.sectionTitleSize
                            font.weight: Font.Medium
                            Layout.alignment: Qt.AlignHCenter
                        }

                        Rectangle { height: 1; Layout.fillWidth: true; color: theme.divider; opacity: 0.8 }

                        // Brightness
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            Label { text: strings.trKey("imageEditor.brightness"); color: theme.text; Layout.preferredWidth: theme.formLabelWidth(parent.width, 110) }
                            Slider {
                                id: brightnessSlider
                                from: -100; to: 100
                                value: brightness
                                Layout.fillWidth: true
                                onMoved: {
                                    brightness = value
                                    apply("brightness", { brightness })
                                }
                            }
                            Label { text: brightness.toFixed(0); color: theme.subtext; Layout.preferredWidth: 42; horizontalAlignment: Text.AlignRight }
                        }

                        // Contrast
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            Label { text: strings.trKey("imageEditor.contrast"); color: theme.text; Layout.preferredWidth: theme.formLabelWidth(parent.width, 110) }
                            Slider {
                                id: contrastSlider
                                from: 0
                                to: 100
                                value: 50
                                stepSize: 1
                                Layout.fillWidth: true
                                onMoved: {
                                    let contrastAmount = Math.abs(value - 50) / 2.0
                                    let increase = value >= 50
                                    contrast = contrastAmount
                                    if (contrastAmount > 0.1) {
                                        apply("contrast", { increase, contrast: contrastAmount })
                                    }
                                }
                            }
                            Label { text: contrast.toFixed(1); color: theme.subtext; Layout.preferredWidth: 42; horizontalAlignment: Text.AlignRight }
                        }

                        // Hue
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            Label { text: strings.trKey("imageEditor.hue"); color: theme.text; Layout.preferredWidth: theme.formLabelWidth(parent.width, 110) }
                            Slider {
                                id: hueSlider
                                value: hue
                                from: -180; to: 180
                                Layout.fillWidth: true
                                onMoved: {
                                    hue = value
                                    apply("hue", hue)
                                }
                            }
                            Label { text: hue.toFixed(0); color: theme.subtext; Layout.preferredWidth: 42; horizontalAlignment: Text.AlignRight }
                        }

                        // Saturation
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            Label { text: strings.trKey("imageEditor.saturation"); color: theme.text; Layout.preferredWidth: theme.formLabelWidth(parent.width, 110) }
                            Slider {
                                id: saturationSlider
                                value: saturation
                                from: 0; to: 200
                                Layout.fillWidth: true
                                onMoved: {
                                    saturation = value
                                    apply("saturation", saturation)
                                }
                            }
                            Label { text: saturation.toFixed(0); color: theme.subtext; Layout.preferredWidth: 42; horizontalAlignment: Text.AlignRight }
                        }

                        // Sharpen
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            Label { text: strings.trKey("imageEditor.sharpen"); color: theme.text; Layout.preferredWidth: theme.formLabelWidth(parent.width, 110) }
                            Slider {
                                id: sharpenSlider
                                value: sharpness
                                from: 0; to: 10
                                Layout.fillWidth: true
                                onMoved: {
                                    sharpness = value
                                    apply("sharpen", sharpness)
                                }
                            }
                            Label { text: sharpness.toFixed(0); color: theme.subtext; Layout.preferredWidth: 42; horizontalAlignment: Text.AlignRight }
                        }

                        // Gamma
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            Label { text: strings.trKey("imageEditor.gamma"); color: theme.text; Layout.preferredWidth: theme.formLabelWidth(parent.width, 110) }
                            Slider {
                                id: gammaSlider
                                value: gamma
                                from: 0; to: 5.0
                                stepSize: 0.1
                                Layout.fillWidth: true
                                onMoved: {
                                    gamma = value
                                    apply("gamma", gamma)
                                }
                            }
                            Label { text: gamma.toFixed(1); color: theme.subtext; Layout.preferredWidth: 42; horizontalAlignment: Text.AlignRight }
                        }
                    }
                }

				// =========================
				// Effects
				// =========================
				Pane {
					Layout.fillWidth: true
					padding: theme.panePadding

					background: Rectangle {
						color: theme.surface
						radius: 12
						border.width: 1
						border.color: theme.divider
					}

					ColumnLayout {
						anchors.horizontalCenter: parent.horizontalCenter
						width: theme.boundedWidth(parent.width, editorPage.cardWidthNarrow)
						spacing: 12

						Label {
							text: strings.trKey("imageEditor.effects")
							color: theme.text
							font.pixelSize: theme.sectionTitleSize
							font.weight: Font.Medium
							Layout.alignment: Qt.AlignHCenter
						}

						Rectangle { height: 1; Layout.fillWidth: true; color: theme.divider; opacity: 0.8 }

						GridLayout {
							id: effectsGrid
							Layout.fillWidth: true
							Layout.alignment: Qt.AlignHCenter
							rowSpacing: 10
							columnSpacing: 10

							// Decide layout from the *known* container width (the ColumnLayout width)
							// 5 columns = 1 row, else 3 columns = 2 rows (5 items -> 2 rows max)
							columns: theme.mobile ? 2 : theme.gridColumns(parent.width, 5, 96)

							// Uniform button sizing
							readonly property int btnW: Math.floor((parent.width - (columns - 1) * columnSpacing) / columns)
							readonly property int btnH: 40

							ThemedButton {
								text: strings.trKey("imageEditor.blur")
								theme: editorPage.theme
								Layout.preferredWidth: effectsGrid.btnW
								Layout.preferredHeight: effectsGrid.btnH
								onClicked: apply("blur", 1.0)
							}
							ThemedButton {
								text: strings.trKey("imageEditor.sepia")
								theme: editorPage.theme
								Layout.preferredWidth: effectsGrid.btnW
								Layout.preferredHeight: effectsGrid.btnH
								onClicked: apply("sepia")
							}
							ThemedButton {
								text: strings.trKey("imageEditor.vignette")
								theme: editorPage.theme
								Layout.preferredWidth: effectsGrid.btnW
								Layout.preferredHeight: effectsGrid.btnH
								onClicked: apply("vignette")
							}
							ThemedButton {
								text: strings.trKey("imageEditor.swirl")
								theme: editorPage.theme
								Layout.preferredWidth: effectsGrid.btnW
								Layout.preferredHeight: effectsGrid.btnH
								onClicked: apply("swirl", 90)
							}
							ThemedButton {
								text: strings.trKey("imageEditor.implode")
								theme: editorPage.theme
								Layout.preferredWidth: effectsGrid.btnW
								Layout.preferredHeight: effectsGrid.btnH
								onClicked: apply("implode", 0.5)
							}
						}
					}
				}
                Item { height: 6 }
            }
        }
    }

    // ---------- Bottom bar (Undo / Redo centered) ----------
    Rectangle {
        id: bottomBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: theme.mobile ? 56 : 64
        color: theme.surface

        RowLayout {
            anchors.centerIn: parent
            spacing: 16

            ThemedButton {
                text: strings.trKey("imageEditor.undo")
                theme: editorPage.theme
                padding: 12
                font.pixelSize: 15
                onClicked: {
                    if (imageEditor.undo()) {
                        isDirty = true
                        imageEditor.saveImage(tempPath)
                        refreshImage()
                        refreshImageSize()
                        resetSliders()
                    }
                }
            }

            ThemedButton {
                text: strings.trKey("imageEditor.redo")
                theme: editorPage.theme
                padding: 12
                font.pixelSize: 15
                onClicked: {
                    if (imageEditor.redo()) {
                        isDirty = true
                        imageEditor.saveImage(tempPath)
                        refreshImage()
                        refreshImageSize()
                        resetSliders()
                    }
                }
            }
        }
    }

    // Toast for lightweight feedback
    Toast {
        id: toast
        parent: Overlay.overlay
    }

    // Force preview reload with a cache-buster
    function refreshImage() {
        if (editorReady)
            imagePreview.source = tempPath + "?" + Date.now()
    }

    // Clean temp artifacts and leave editor
    function cleanupAndExit() {
        imageEditor.deleteFile(tempPath)
        imageEditor.clearUndoRedoStacks()
        stackView.pop()
    }

    // Return sliders to current backing values (after undo/redo)
    function resetSliders() {
        brightnessSlider.value = brightness
        contrastSlider.value = 50
        hueSlider.value = hue
        saturationSlider.value = saturation
        sharpenSlider.value = sharpness
        gammaSlider.value = gamma
    }

    // Command router: calls through to C++ ImageEditor and updates preview/temp
    function apply(type, value) {
        const actions = {
            "flip":                 () => imageEditor.flip(value),
            "rotate":               () => imageEditor.rotate(value),
            "brightnessContrast":   () => imageEditor.adjustBrightnessContrast(value.brightness, value.contrast),
            "brightness":           () => imageEditor.adjustBrightness(value.brightness),
            "contrast":             () => imageEditor.adjustContrast(value.increase, value.contrast, 128.0),
            "hue":                  () => imageEditor.adjustHue(value),
            "saturation":           () => imageEditor.adjustSaturation(value),
            "gamma":                () => imageEditor.adjustGamma(value),
            "sharpen":              () => imageEditor.sharpenImage(value),
            "blur":                 () => imageEditor.applyBlur(value),
            "sepia":                () => imageEditor.applySepia(),
            "vignette":             () => imageEditor.applyVignette(),
            "swirl":                () => imageEditor.applySwirl(value),
            "implode":              () => imageEditor.applyImplode(value),
            "resizeOriginal":       () => imageEditor.resizeToOriginal(),
            "resizeHalf":           () => imageEditor.resizeToHalf(),
            "resizeDouble":         () => imageEditor.resizeToDouble(),
            "resize":               () => imageEditor.resizeImage(value.x, value.y),
            "crop":                 () => imageEditor.crop(value.x, value.y, value.w, value.h),
        }

        let ok = false

        if (actions[type]) {
            try {
                ok = actions[type]()
                if (ok !== false) {
                    isDirty = true
                    if (imageEditor.saveImage(tempPath))
                        refreshImage()
                    else
                        toast.show(strings.trKey("imageEditor.toast.saveFailed"))
                }
            } catch (err) {
                console.warn("Error executing action:", type, err)
            }
        } else {
            console.warn("Unknown operation:", type)
        }
    }
}
