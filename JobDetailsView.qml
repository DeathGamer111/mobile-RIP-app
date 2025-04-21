import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt.labs.platform


ScrollView {
    required property StackView stackView
    required property int jobIndex
    property var jobData: jobModel.getJob(jobIndex)
    property string imagePath: jobData.imagePath
    property var imageMeta: ({})

    anchors.fill: parent
    ScrollBar.vertical.interactive: true
    ScrollBar.vertical.policy: ScrollBar.AsNeeded

    Component.onCompleted: {
        contentItem.flickableDirection = Flickable.VerticalFlick

        if (jobData.imagePath !== "") {
            updateMetadata(jobData.imagePath)
        }
    }

    function updateImagePath(path) {
        jobData.imagePath = path
    }

    function paperSizeIndexFromSize(size) {
        if (size.width === 210 && size.height === 297) return 0; // A4
        if (size.width === 216 && size.height === 279) return 1; // Letter
        if (size.width === 279 && size.height === 432) return 2; // Tabloid
        return 3; // Custom
    }

    function updatePaperSize() {
        if (paperSizeBox.currentText === "A4") {
            jobData.paperSize = Qt.size(210, 297)
        }
        else if (paperSizeBox.currentText === "Letter") {
            jobData.paperSize = Qt.size(216, 279)
        }
        else if (paperSizeBox.currentText === "Tabloid") {
            jobData.paperSize = Qt.size(279, 432)
        }
        else if (paperSizeBox.currentText === "Custom") {
            jobData.paperSize = Qt.size(customWidth.value, customHeight.value)
        }
    }

    function updateResolution() {
        jobData.resolution = Qt.size(resolutionWidthSpin.value, resolutionHeightSpin.value)
    }

    function updateOffset() {
        jobData.offset = Qt.point(offsetXSpin.value, offsetYSpin.value)
    }

    function updateWhiteStrategy() {
        jobData.whiteStrategy = whiteBox.currentText
    }

    function updateVarnishType() {
        jobData.varnishType = varnishBox.currentText
    }

    function updateColorProfile() {
        jobData.colorProfile = profileBox.currentText
    }

    function updateMetadata(path) {
        imageMeta = imageLoader.extractMetadata(path)
    }


    Column {
        width: parent.width
        spacing: 0
        anchors.horizontalCenter: parent.horizontalCenter

        Pane {
            width: Math.min(parent.width, 450)
            padding: 20

            ColumnLayout {
                id: columnContent
                spacing: 16
                anchors.fill: parent

                TextField {
                    id: jobNameField
                    text: jobData.name
                    placeholderText: "Enter Job Name"
                    Layout.fillWidth: true
                    ToolTip.text: "Enter a name to identify this print job"
                    ToolTip.visible: hovered
                }

                Rectangle {
                    id: imageContainer
                    width: 300
                    height: 200
                    color: "#eeeeee"
                    border.color: "#999"
                    Layout.alignment: Qt.AlignHCenter
                    clip: true

                    Image {
                        id: previewImage
                        anchors.centerIn: parent
                        source: imagePath
                        fillMode: Image.PreserveAspectFit
                        width: parent.width
                        height: parent.height
                        smooth: true
                        visible: source !== ""
                        clip: true
                    }

                    Text {
                        anchors.centerIn: parent
                        text: jobData.imagePath === "" ? "No image loaded" : ""
                        visible: imagePath == ""
                    }
                }

                RowLayout {
                    spacing: 12
                    Layout.alignment: Qt.AlignHCenter

                    Button {
                        text: "Upload Image"
                        onClicked: imageDialog.open()
                        ToolTip.text: "Select an image to associate with this print job"
                        ToolTip.visible: hovered
                    }

                    Button {
                        text: "Edit Image"
                        onClicked: console.log("TODO: Open image editor")
                        ToolTip.text: "Open image editor to make adjustments before printing"
                        ToolTip.visible: hovered
                    }
                }

                FileDialog {
                    id: imageDialog
                    title: "Select Image for Job"
                    nameFilters: ["Images (*.png *.jpg *.jpeg *.bmp *.tif *.tiff *.svg *.pdf)"]
                    onAccepted: {
                        if (imageLoader.isSupportedExtension(file)) {
                            if (imageLoader.validateFile(file)) {
                                updateMetadata(file)
                                updateImagePath(file)
                                imagePath = file
                            }
                            else {
                                console.warn("File validation failed.")
                            }
                        }
                        else {
                            console.warn("Unsupported file type.")
                        }
                    }
                }

                Label {
                    text: "Printer Settings"
                    font.pixelSize: 18
                    horizontalAlignment: Text.AlignHCenter
                    Layout.alignment: Qt.AlignHCenter
                }

                GroupBox {
                    title: ""
                    Layout.fillWidth: true

                    ColumnLayout {
                        spacing: 10
                        Layout.fillWidth: true
                        anchors.horizontalCenter: parent.horizontalCenter

                        Label { text: "Paper Size" }
                        ComboBox {
                            id: paperSizeBox
                            Layout.fillWidth: true
                            model: ["A4", "Letter", "Tabloid", "Custom"]
                            currentIndex: paperSizeIndexFromSize(jobData.paperSize)
                            onCurrentTextChanged: updatePaperSize()

                            ToolTip.text: "Select a predefined paper size or choose Custom to define your own dimensions"
                            ToolTip.visible: hovered
                        }

                        ColumnLayout {
                            visible: paperSizeBox.currentText === "Custom"
                            spacing: 8

                            Label { text: "Custom Paper Size (Width × Height, mm)" }

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
                                    ToolTip.text: "Custom paper width in millimeters"
                                    ToolTip.visible: hovered
                                }

                                Label { text: "×" }

                                SpinBox {
                                    id: customHeight
                                    from: 10; to: 2000
                                    value: jobData.paperSize.height
                                    editable: true
                                    Layout.fillWidth: true
                                    onValueChanged: jobData.paperSize.height = value
                                    ToolTip.text: "Custom paper height in millimeters"
                                    ToolTip.visible: hovered
                                }
                            }
                        }

                        Label { text: "Resolution (Width × Height)" }
                        RowLayout {
                            spacing: 8
                            Layout.fillWidth: true

                            SpinBox {
                                id: resolutionWidthSpin
                                from: 1; to: 10000
                                value: jobData.resolution.width
                                editable: true
                                validator: IntValidator { bottom: 1 }
                                Layout.fillWidth: true
                                onValueChanged: updateResolution()
                                ToolTip.text: "Horizontal resolution in pixels"
                                ToolTip.visible: hovered
                            }

                            Label { text: "×" }

                            SpinBox {
                                id: resolutionHeightSpin
                                from: 1; to: 10000
                                value: jobData.resolution.height
                                editable: true
                                validator: IntValidator { bottom: 1 }
                                Layout.fillWidth: true
                                onValueChanged: updateResolution()
                                ToolTip.text: "Vertical resolution in pixels"
                                ToolTip.visible: hovered
                            }
                        }

                        Label { text: "Offset (X × Y)" }
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
                                ToolTip.text: "Horizontal offset in pixels"
                                ToolTip.visible: hovered
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
                                ToolTip.text: "Vertical offset in pixels"
                                ToolTip.visible: hovered
                            }
                        }

                        Label { text: "White Strategy" }
                        ComboBox {
                            id: whiteBox
                            Layout.fillWidth: true
                            model: ["None", "Underprint", "Overprint", "Flood", "Spot", "Knockout"]
                            currentIndex: model.indexOf(jobData.whiteStrategy)
                            onCurrentTextChanged: updateWhiteStrategy()
                            ToolTip.text: "Select how white ink should be applied"
                            ToolTip.visible: hovered
                        }

                        Label { text: "Varnish Layer" }
                        ComboBox {
                            id: varnishBox
                            Layout.fillWidth: true
                            model: ["None", "Spot", "Full"]
                            currentIndex: model.indexOf(jobData.varnishType)
                            onCurrentTextChanged: updateVarnishType()
                            ToolTip.text: "Choose varnish coverage for this print job"
                            ToolTip.visible: hovered
                        }

                        Label { text: "Color Profile" }
                        ComboBox {
                            id: profileBox
                            Layout.fillWidth: true
                            model: ["sRGB", "AdobeRGB", "CMYK", "Gray", "Custom ICC"]
                            currentIndex: model.indexOf(jobData.colorProfile)
                            onCurrentTextChanged: updateColorProfile()
                            ToolTip.text: "Set the ICC or color profile used for rendering"
                            ToolTip.visible: hovered
                        }
                    }
                }

                GroupBox {
                    title: "Image Metadata"
                    Layout.fillWidth: true
                    visible: Object.keys(imageMeta).length > 0

                    Column {
                        spacing: 4

                        // Always shown if present
                        Text { text: "Name: " + imageMeta.name; color: "white"; visible: imageMeta.name !== undefined }
                        Text { text: "Size: " + imageMeta.size + " bytes"; color: "white"; visible: imageMeta.size !== undefined }
                        Text { text: "Dimensions: " + imageMeta.width + " x " + imageMeta.height; color: "white"; visible: imageMeta.width !== undefined && imageMeta.height !== undefined }
                        Text { text: "Channels: " + imageMeta.channels; color: "white"; visible: imageMeta.channels !== undefined }
                        Text { text: "Format: " + imageMeta.format; color: "white"; visible: imageMeta.format !== undefined }
                        Text { text: "DPI: " + imageMeta.dpi; color: "white"; visible: imageMeta.dpi !== undefined }
                        Text { text: "Color Profile: " + imageMeta.colorProfile; color: "white"; visible: imageMeta.colorProfile !== undefined }

                        // SVG
                        Text { text: "SVG Size: " + imageMeta.svgWidth + " x " + imageMeta.svgHeight; color: "white"; visible: imageMeta.svgWidth !== undefined && imageMeta.svgHeight !== undefined }
                        Text { text: "SVG Title: " + imageMeta.svgTitle; color: "white"; visible: imageMeta.svgTitle !== undefined }

                        // PDF
                        Text { text: "PDF Version: " + imageMeta.pdfVersion; color: "white"; visible: imageMeta.pdfVersion !== undefined }
                        Text { text: "PDF Title: " + imageMeta.pdfTitle; color: "white"; visible: imageMeta.pdfTitle !== undefined }
                        Text { text: "Page Count: " + imageMeta.pageCount; color: "white"; visible: imageMeta.pageCount !== undefined }
                    }
                }

                RowLayout {
                    spacing: 20
                    Layout.alignment: Qt.AlignHCenter

                    Button {
                        text: "Save Job"
                        onClicked: {
                            jobModel.updateJob(jobIndex, {
                                name: jobNameField.text,
                                imagePath: jobData.imagePath,
                                paperSize: jobData.paperSize,
                                resolution: Qt.size(resolutionWidthSpin.value, resolutionHeightSpin.value),
                                offset: Qt.point(offsetXSpin.value, offsetYSpin.value),
                                whiteStrategy: whiteBox.currentText,
                                varnishType: varnishBox.currentText,
                                colorProfile: profileBox.currentText
                            })
                            toast.show("Job Successfully Saved!")
                        }
                        ToolTip.text: "Apply and save changes to this print job."
                        ToolTip.visible: hovered
                    }

                    Button {
                        text: "Back"
                        onClicked: stackView.pop()
                        ToolTip.text: "Return to the job list while discarding changes."
                        ToolTip.visible: hovered
                    }


                    Toast {
                        id: toast
                        parent: Overlay.overlay
                    }
                }
            }
        }
    }
}
