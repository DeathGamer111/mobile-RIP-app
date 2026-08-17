import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "."

/* ImpositionView.qml
 * Canvas-style editor to position a job image on physical media (legacy paperSize field, in mm)
 * and optionally burn simple overlays (text/rectangle) into a new imposed image.
 * Coordinates persist as mm offsets; zoom only affects on-screen scale.
 */
Page {
    id: impositionView

    // Model hooks
    required property int jobIndex
    required property var jobModel
    required property StackView stackView
    required property Theme theme
    
    // Optional override so the image can display even if the job hasn’t been saved yet
	property string initialImagePath: ""

    background: Rectangle { color: theme.bg }

    // Centered card width helpers (same pattern as ColorManagementView)
    readonly property int cardWidthNarrow: 520
    readonly property int cardWidthMedium: 640
    readonly property int cardWidthWide: 760

    // Function to pull in current job model
    function jobData() { return jobModel.getJob(jobIndex) }

    // Source + media state (dimensions in mm)
	property string imagePath: (initialImagePath && initialImagePath.length > 0)
		                       ? initialImagePath
		                       : jobData().imagePath

    property size paperSize: jobData().paperSize

    // Overlay state
    property bool hasDrawnElements: false

    // Zoom (1.0 = 100%) auto-fit based on media size and viewport box
    property real zoomFactor: 1.0

    // Build an updated job payload; optionally swap imagePath when overlays are baked in
    function cloneJobWithOffset(newOffset, newPath = null) {
        return {
            name: jobData().name,
            imagePath: newPath || jobData().imagePath,
            paperSize: jobData().paperSize,
            resolution: jobData().resolution,
            offset: newOffset,
            whiteStrategy: jobData().whiteStrategy,
            varnishType: jobData().varnishType,
            colorProfile: jobData().colorProfile
        }
    }

    // Overlay command dispatcher for live preview (draws when saving via C++ backend)
    function apply(type, value) {
        const actions = {
            "text":     () => imageEditor.drawText(value.text, value.x, value.y),
            "drawRect": () => imageEditor.drawRectangle(value.x, value.y, value.w, value.h)
        }

        if (actions[type]) {
            actions[type]()
            hasDrawnElements = true
        } else {
            console.warn("Unknown draw action:", type)
        }
    }

    function updateZoomToFit() {
        if (paperSize.width > 0 && paperSize.height > 0 && impositionViewport.width > 0 && impositionViewport.height > 0) {
            zoomFactor = Math.min(
                impositionViewport.width / paperSize.width,
                impositionViewport.height / paperSize.height
            )
        }
    }

    onWidthChanged: updateZoomToFit()
    onHeightChanged: updateZoomToFit()

    function doBack() {
        stackView.pop()
    }

    function doSave() {
        let newOffset = Qt.point(
            Math.round(Math.max(0, imageWrapper.itemX)),
            Math.round(Math.max(0, imageWrapper.itemY))
        )

        if (hasDrawnElements) {
            imageEditor.applyImpositionEdits(
                jobData().imagePath,
                newOffset.x,
                newOffset.y,
                paperSize,
                {
                    text: textOverlayField.text,
                    textX: Math.round(textOverlayWrapper.itemX),
                    textY: Math.round(textOverlayWrapper.itemY),
                    drawRect: rectOverlayWrapper.visible,
                    rectX: Math.round(rectOverlayWrapper.itemX),
                    rectY: Math.round(rectOverlayWrapper.itemY),
                    rectW: Math.round(rectOverlayWrapper.width),
                    rectH: Math.round(rectOverlayWrapper.height)
                }
            )

            let imposedPath = jobData().imagePath.replace(/(\.\w+)$/, "_imposed$1")
            jobModel.updateJob(jobIndex, cloneJobWithOffset(newOffset, imposedPath))
            toast.show(strings.trKey("imposition.toast.imageUpdated"))
        } else {
            jobModel.updateJob(jobIndex, cloneJobWithOffset(newOffset))
            console.log("Image offset updated:", newOffset)
            toast.show(strings.trKey("imposition.toast.offsetUpdated"))
        }
    }

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
                theme: impositionView.theme
                Layout.preferredWidth: impositionView.theme.headerButtonWidth(impositionView.width)
                padding: 12
                font.pixelSize: 15
                onClicked: impositionView.doBack()
            }

            Item { Layout.fillWidth: true }

            Label {
                text: strings.trKey("imposition.title")
                color: theme.text
                font.pixelSize: impositionView.theme.headerTitleSize(impositionView.width)
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
                theme: impositionView.theme
                Layout.preferredWidth: impositionView.theme.headerButtonWidth(impositionView.width)
                padding: 12
                font.pixelSize: 15
                enabled: jobData().imagePath && jobData().imagePath.length > 0
                onClicked: impositionView.doSave()
            }
        }
    }

    // ---------- Body (scrollable like the other views) ----------
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
            // Viewer (locked in place)
            // =========================
            Pane {
                Layout.fillWidth: true
                padding: 12

                background: Rectangle {
                    color: theme.surface
                        radius: theme.cardRadius
                    border.width: 1
                    border.color: theme.divider
                }

                ColumnLayout {
					anchors.horizontalCenter: parent.horizontalCenter
					width: theme.boundedWidth(parent.width, impositionView.cardWidthWide)
					spacing: 10

                    // Fixed viewport box (like ImageEditor preview card)
                    Rectangle {
                        id: impositionViewport
                        Layout.fillWidth: true
                        Layout.preferredHeight: theme.mobile ? 240 : 400
                        Layout.minimumHeight: theme.mobile ? 200 : 260
                        Layout.maximumHeight: theme.mobile ? 240 : 400
                        color: theme.surface2
                        border.color: theme.divider
                        border.width: 1
                        radius: 10
                        clip: true

                        Component.onCompleted: Qt.callLater(impositionView.updateZoomToFit)
                        onWidthChanged: Qt.callLater(impositionView.updateZoomToFit)
                        onHeightChanged: Qt.callLater(impositionView.updateZoomToFit)

                        // Paper plane (mm) centered in the viewport
                        Item {
                            id: paperArea
                            width: paperSize.width
                            height: paperSize.height
                            scale: zoomFactor
                            anchors.centerIn: parent
                            clip: true

                            Rectangle {
                                anchors.fill: parent
                                color: "white"
                                border.color: "black"
                                border.width: 1
                            }

                            // Draggable job image proxy; offset stored in mm (x,y)
                            Item {
                                id: imageWrapper
                                property real itemX: (jobModel.getJob(jobIndex).offset?.x !== undefined) ? jobModel.getJob(jobIndex).offset.x : 0
                                property real itemY: (jobModel.getJob(jobIndex).offset?.y !== undefined) ? jobModel.getJob(jobIndex).offset.y : 0

								x: itemX
								y: itemY

                                width: imageItem.width
                                height: imageItem.height

                                // Image is sized from pixels → mm using assumed 720 DPI (25.4 mm/in)
                                Image {
                                    id: imageItem
									source: impositionView.imagePath
                                    fillMode: Image.PreserveAspectFit
                                    asynchronous: true
                                    visible: status === Image.Ready

                                    readonly property real dpi: 720
                                    readonly property real pxWidth: jobData().imageWidth || implicitWidth || 1000
                                    readonly property real pxHeight: jobData().imageHeight || implicitHeight || 1000

                                    width: (pxWidth / dpi) * 25.4
                                    height: (pxHeight / dpi) * 25.4

                                    onStatusChanged: {
                                        if (status === Image.Ready) {
                                            console.log("Image ready:", source)
                                            console.log("Size:", width, height)
                                        } else if (status === Image.Error) {
                                            console.error("Failed to load image:", source)
                                        }
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        drag.target: imageWrapper
                                        onReleased: {
											imageWrapper.itemX = imageWrapper.x
											imageWrapper.itemY = imageWrapper.y
										}
                                    }
                                }
                            }

                            // Draggable text overlay preview (baked at Save)
                            Item {
                                id: textOverlayWrapper
                                property real itemX: 100
                                property real itemY: 100

								x: itemX
								y: itemY

                                width: textItem.width
                                height: textItem.height

                                visible: textOverlayField.text.length > 0

                                Text {
                                    id: textItem
                                    text: textOverlayField.text
                                    font.pointSize: 14
                                    color: theme.accent
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    drag.target: textOverlayWrapper
                                    onReleased: {
										textOverlayWrapper.itemX = textOverlayWrapper.x
										textOverlayWrapper.itemY = textOverlayWrapper.y
                                    }
                                }
                            }

                            // Draggable rectangle overlay preview (baked at Save)
                            Item {
                                id: rectOverlayWrapper
                                property real itemX: 50
                                property real itemY: 50

								x: itemX
								y: itemY

                                width: 100
                                height: 100

                                visible: false

                                Rectangle {
                                    anchors.fill: parent
                                    color: "transparent"
                                    border.color: theme.danger
                                    border.width: 2
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    drag.target: rectOverlayWrapper
                                    onReleased: {
										rectOverlayWrapper.itemX = rectOverlayWrapper.x
										rectOverlayWrapper.itemY = rectOverlayWrapper.y
                                    }
                                }
                            }
                        }
                    }

                    // Zoom controls UNDER the viewer
                    GridLayout {
						id: zoomGrid
						Layout.fillWidth: true
						Layout.alignment: Qt.AlignHCenter
						columnSpacing: 10
						rowSpacing: 10
						columns: zoomGrid.width < 280 ? 1 : 3

						// Uniform sizing
						readonly property int btnH: 40
						readonly property int btnW: columns === 1 ? parent.width : Math.max(72, Math.floor((parent.width - 72 - (columns - 1) * columnSpacing) / 2))

						ThemedButton {
							text: strings.trKey("imposition.zoomOut")
							theme: impositionView.theme
							Layout.preferredWidth: zoomGrid.btnW
							Layout.preferredHeight: zoomGrid.btnH
							onClicked: zoomFactor = Math.max(zoomFactor - 0.1, 0.05)
						}

						Label {
							text: Math.round(zoomFactor * 100) + "%"
							color: theme.text
							font.weight: Font.Medium
							horizontalAlignment: Text.AlignHCenter
							verticalAlignment: Text.AlignVCenter
							Layout.alignment: Qt.AlignVCenter
							Layout.preferredWidth: zoomGrid.columns === 1 ? zoomGrid.parent.width : 72
						}

						ThemedButton {
							text: strings.trKey("imposition.zoomIn")
							theme: impositionView.theme
							Layout.preferredWidth: zoomGrid.btnW
							Layout.preferredHeight: zoomGrid.btnH
							onClicked: zoomFactor = Math.min(zoomFactor + 0.1, 5.0)
						}
					}
                }
            }

            // =========================
            // Overlay Tools
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
					width: theme.boundedWidth(parent.width, impositionView.cardWidthMedium)
					spacing: 12

                    Label {
                        text: strings.trKey("imposition.overlayTools")
                        color: theme.text
                        font.pixelSize: theme.sectionTitleSize
                        font.weight: Font.Medium
                        Layout.alignment: Qt.AlignHCenter
                    }

                    Rectangle { height: 1; Layout.fillWidth: true; color: theme.divider; opacity: 0.8 }

                    // --- Text tool: field row, then button row ---
					ColumnLayout {
						Layout.fillWidth: true
						spacing: 10

						// Row 1: label + textbox
						RowLayout {
							Layout.fillWidth: true
							spacing: 10

							Label {
								text: strings.trKey("imposition.text")
								color: theme.text
								Layout.preferredWidth: 60
							}

							TextField {
								id: textOverlayField
								placeholderText: strings.trKey("imposition.text.placeholder")
								Layout.fillWidth: true
                                Layout.minimumWidth: 0
							}
						}

						// Row 2: button (full row)
						RowLayout {
							Layout.fillWidth: true
							Layout.alignment: Qt.AlignHCenter

							ThemedButton {
								text: strings.trKey("imposition.drawText")
								theme: impositionView.theme
								Layout.preferredHeight: 40
								Layout.preferredWidth: Math.min(parent.width, 160)
								onClicked: {
									textOverlayWrapper.visible = true
									apply("text", {
										text: textOverlayField.text,
										x: textOverlayWrapper.itemX,
										y: textOverlayWrapper.itemY
									})
								}
							}
						}

						// Optional divider (looks nice and matches other views)
						Rectangle {
							height: 1
							Layout.fillWidth: true
							color: theme.divider
							opacity: 0.8
						}

						// --- Rect tool: W/H controls row, then button row ---
						GridLayout {
							Layout.fillWidth: true
							columns: theme.mobile ? 2 : theme.gridColumns(width, 4, 96)
							columnSpacing: 10
                            rowSpacing: 10

							Label {
								text: strings.trKey("imposition.width")
								color: theme.text
								Layout.preferredWidth: theme.formLabelWidth(parent.width, 60)
							}

							SpinBox {
								id: rectWidthField
								from: 10; to: 500; value: 100; stepSize: 5
								Layout.fillWidth: true
							}

							Label {
								text: strings.trKey("imposition.height")
								color: theme.text
								Layout.preferredWidth: theme.formLabelWidth(parent.width, 60)
							}

							SpinBox {
								id: rectHeightField
								from: 10; to: 500; value: 100; stepSize: 5
								Layout.fillWidth: true
							}
						}

						RowLayout {
							Layout.fillWidth: true
							Layout.alignment: Qt.AlignHCenter

							ThemedButton {
								text: strings.trKey("imposition.drawRect")
								theme: impositionView.theme
								Layout.preferredHeight: 40
								Layout.preferredWidth: Math.min(parent.width, 160)
								onClicked: {
									rectOverlayWrapper.visible = true
									rectOverlayWrapper.width = rectWidthField.value
									rectOverlayWrapper.height = rectHeightField.value
									apply("drawRect", {
										x: rectOverlayWrapper.itemX,
										y: rectOverlayWrapper.itemY,
										w: rectWidthField.value,
										h: rectHeightField.value
									})
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
