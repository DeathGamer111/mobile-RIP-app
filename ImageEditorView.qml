import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt.labs.platform

Page {
    id: editorPage
    required property string imagePath
    property string tempPath: imagePath + ".edit_tmp"

    property bool isDirty: true
    property string currentTool: "none"

    property real cropX: 50
    property real cropY: 50
    property real cropW: 100
    property real cropH: 100

    property real brightness: 0
    property real contrast: 0
    property real hue: 0
    property real saturation: 0
    property real sharpness: 0
    property real gamma: 0
    property string overlayText: "Sample Text"

    Component.onCompleted: {
        if (imageEditor.loadImage(imagePath)) {
            imageEditor.saveImage(tempPath)
            if (imageEditor.loadImage(tempPath)) {
                refreshImage()
            } else {
                console.warn("Failed to load temp image for editing")
            }
        } else {
            console.warn("Failed to load image for editing")
        }
    }

    // === Main Layout ===
    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        // === Top: Image Preview ===
        Pane {
            Layout.fillWidth: true
            Layout.preferredHeight: 260

            Rectangle {
                id: imageContainer
                anchors.fill: parent
                color: "#eeeeee"
                border.color: "#999"
                clip: true

                Image {
                    id: imagePreview
                    source: tempPath
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
                    border.color: "red"
                    border.width: 2
                    visible: currentTool === "crop"
                }
            }
        }

        // === Middle: Scrollable Tool Panel ===
        ScrollView {
            id: scrollView
            Layout.fillWidth: true
            Layout.fillHeight: true
            ScrollBar.vertical.interactive: true
            ScrollBar.vertical.policy: ScrollBar.AsNeeded

            Component.onCompleted: {
                contentItem.flickableDirection = Flickable.VerticalFlick
            }

            Column {
                width: parent.width
                spacing: 0
                anchors.horizontalCenter: parent.horizontalCenter

                Pane {
                    width: Math.min(parent.width, 450)
                    padding: 20

                    ColumnLayout {
                        id: toolColumn
                        width: parent.width
                        spacing: 20

                        // === View Tools ===
                        GroupBox {
                            title: "View"
                            Layout.fillWidth: true

                            GridLayout {
                                columns: 4
                                anchors.horizontalCenter: parent.horizontalCenter

                                Button { text: "Original Size"; onClicked: apply("resizeOriginal") }
                                Button { text: "Half Size"; onClicked: apply("resizeHalf") }
                                Button { text: "Double Size"; onClicked: apply("resizeDouble") }
                                Button { text: "Resize" }
                            }
                        }

                        // === Transform Tools ===
                        GroupBox {
                            title: "Transform"
                            Layout.fillWidth: true
                            GridLayout {
                                columns: 4
                                anchors.horizontalCenter: parent.horizontalCenter

                                Button { text: "Crop"; onClicked: apply("crop", { x: cropX, y: cropY, w: cropW, h: cropH }) }
                                Button { text: "Flip H"; onClicked: apply("flip", "horizontal") }
                                Button { text: "Flip V"; onClicked: apply("flip", "vertical") }
                                Button { text: "Rotate"; onClicked: apply("rotate", 90) }
                            }
                        }

                        // === Enhance Tools ===
                        GroupBox {
                            title: "Enhance"
                            Layout.fillWidth: true

                            ColumnLayout {
                                anchors.horizontalCenter: parent.horizontalCenter

                                RowLayout {
                                    spacing: 10
                                    Label { text: "Brightness" }
                                    Slider {
                                        id: brightnessSlider
                                        from: -100; to: 100
                                        value: brightness
                                        Layout.fillWidth: true
                                        onValueChanged: {
                                            brightness = value
                                            apply("brightnessContrast", { brightness, contrast })
                                        }
                                    }
                                    Label { text: brightness.toFixed(0) }
                                }

                                RowLayout {
                                    spacing: 10
                                    Label { text: "Contrast" }
                                    Slider {
                                        id: contrastSlider
                                        from: -100; to: 100
                                        value: contrast
                                        Layout.fillWidth: true
                                        onValueChanged: {
                                            contrast = value
                                            apply("brightnessContrast", { brightness, contrast })
                                        }
                                    }
                                    Label { text: contrast.toFixed(0) }
                                }

                                RowLayout {
                                    spacing: 10
                                    Label { text: "Hue" }
                                    Slider {
                                        id: hueSlider
                                        value: hue
                                        from: -180; to: 180
                                        Layout.fillWidth: true
                                        onValueChanged: {
                                            hue = value
                                            apply("hue", hue)
                                        }
                                    }
                                    Label { text: hue.toFixed(0) }
                                }

                                RowLayout {
                                    spacing: 10
                                    Label { text: "Saturation" }
                                    Slider {
                                        id: saturationSlider
                                        value: saturation
                                        from: 0; to: 200
                                        Layout.fillWidth: true
                                        onValueChanged: {
                                            saturation = value
                                            apply("saturation", saturation)
                                        }
                                    }
                                    Label { text: saturation.toFixed(0) }
                                }

                                RowLayout {
                                    spacing: 10
                                    Label { text: "Sharpen" }
                                    Slider {
                                        id: sharpenSlider
                                        value: sharpness
                                        from: 0; to: 10
                                        Layout.fillWidth: true
                                        onValueChanged: {
                                            sharpness = value
                                            apply("sharpen", sharpness)
                                        }
                                    }
                                    Label { text: sharpness.toFixed(0) }
                                }

                                RowLayout {
                                    spacing: 10
                                    Label { text: "Gamma" }
                                    Slider {
                                        id: gammaSlider
                                        value: gamma
                                        from: 0.1; to: 5.0
                                        stepSize: 0.1
                                        Layout.fillWidth: true
                                        onValueChanged: {
                                            gamma = value
                                            apply("gamma", gamma)
                                        }
                                    }
                                    Label { text: gamma.toFixed(0) }
                                }
                            }
                        }

                        // === Effects Tools ===
                        GroupBox {
                            title: "Effects"
                            Layout.fillWidth: true

                            GridLayout {
                                columns: 4
                                anchors.horizontalCenter: parent.horizontalCenter

                                Button { text: "Blur"; onClicked: apply("blur", 1.0) }
                                Button { text: "Sepia"; onClicked: apply("sepia") }
                                Button { text: "Vignette"; onClicked: apply("vignette") }
                                Button { text: "Swirl"; onClicked: apply("swirl", 90) }
                                Button { text: "Implode"; onClicked: apply("implode", 0.5) }
                            }
                        }

                        // === Color Space Tools ===
                        GroupBox {
                            title: "Color Space"
                            Layout.fillWidth: true

                            GridLayout {
                                columns: 4
                                anchors.horizontalCenter: parent.horizontalCenter

                                Button { text: "Grayscale"; onClicked: apply("colorspace", "Gray") }
                                Button { text: "RGB"; onClicked: apply("colorspace", "RGB") }
                                Button { text: "CMYK"; onClicked: apply("colorspace", "CMYK") }
                                Button { text: "ICC Profile" }
                            }
                        }

                        // === Drawing Tools ===
                        GroupBox {
                            title: "Text Overlay"
                            Layout.fillWidth: true

                            ColumnLayout {
                                anchors.horizontalCenter: parent.horizontalCenter

                                RowLayout {
                                    spacing: 10

                                    TextField {
                                        placeholderText: "Enter text to draw"
                                        text: overlayText
                                        onTextChanged: overlayText = text
                                        Layout.fillWidth: true
                                    }

                                    Button { text: "Draw Text"; onClicked: apply("text", { text: overlayText, x: 100, y: 100 }) }
                                }

                                RowLayout {
                                    spacing: 10

                                    TextField {
                                        placeholderText: "Enter deminsions for rectangle"
                                        Layout.fillWidth: true
                                    }

                                    Button { text: "Draw Rect"; onClicked: apply("drawRect", { x: 50, y: 50, w: 100, h: 100 }) }
                                }
                            }
                        }
                    }
                }
            }
        }

        // === Bottom: Save / Back Toolbar ===
        Rectangle {
            Layout.fillWidth: true
            height: 60
            color: "transparent"

            RowLayout {
                anchors.centerIn: parent
                spacing: 20

                Button {
                    text: "Save"
                    onClicked: {
                        if (imageEditor.saveImage(imagePath)) {
                            isDirty = false
                            toast.show("Image saved.")
                        }
                    }
                }

                Button {
                    text: "Undo"
                    onClicked: {
                        if (imageEditor.undo()) {
                            isDirty = true
                            refreshImage()
                        }
                    }
                }

                Button {
                    text: "Redo"
                    onClicked: {
                        if (imageEditor.redo()) {
                            isDirty = true
                            refreshImage()
                        }
                    }
                }

                Button {
                    text: "Back"
                    ToolTip.text: "Return to the job list while discarding changes."
                    ToolTip.visible: hovered
                    onClicked: {
                        if (isDirty) {
                            toast.show("Unsaved changes. Press back again to discard?")
                            isDirty = false
                        } else {
                            cleanupAndExit()
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

    function refreshImage() {
        imagePreview.source = tempPath + "?" + Date.now()
    }

    function cleanupAndExit() {
        imageEditor.deleteFile(tempPath)
        stackView.pop()
    }

    function apply(type, value) {
        const actions = {
            "flip":                 () => imageEditor.flip(value),
            "rotate":               () => imageEditor.rotate(value),
            "colorspace":           () => imageEditor.convertColorSpace(value),
            "brightnessContrast":   () => imageEditor.adjustBrightnessContrast(value.brightness, value.contrast),
            "hue":                  () => imageEditor.adjustHue(value),
            "saturation":           () => imageEditor.adjustSaturation(value),
            "gamma":                () => imageEditor.adjustGamma(value),
            "sharpen":              () => imageEditor.applySharpen(value),
            "blur":                 () => imageEditor.applyBlur(value),
            "sepia":                () => imageEditor.applySepia(),
            "vignette":             () => imageEditor.applyVignette(),
            "swirl":                () => imageEditor.applySwirl(value),
            "implode":              () => imageEditor.applyImplode(value),
            "resizeOriginal":       () => imageEditor.resizeToOriginal(),
            "resizeHalf":           () => imageEditor.resizeToHalf(),
            "resizeDouble":         () => imageEditor.resizeToDouble(),
            "crop":                 () => imageEditor.crop(value.x, value.y, value.w, value.h),
            "text":                 () => imageEditor.drawText(value.text, value.x, value.y),
            "drawRect":             () => imageEditor.drawRectangle(value.x, value.y, value.w, value.h)
        }

        let ok = false

        if (actions[type]) {
            try {
                ok = actions[type]()
                isDirty = true
                imageEditor.saveImage(tempPath)
                refreshImage()
            }
            catch (err) {
                console.warn("Error executing action:", type, err)
            }
        }
        else {
            console.warn("Unknown operation:", type)
        }
    }
}
