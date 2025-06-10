import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt.labs.platform

Page {
    id: setupPage
    required property StackView stackView
    required property var appState

    Component.onCompleted: printJobOutput.refreshDetectedPrinters()

    property var nocaiPrinterCapabilities: {
        "X-33": {
            resolutions: ["720x720", "1440x1440"],
            mediaSizes: ["A1", "A2", "A3", "A4", "A5", "A6", "Tabloid"],
            duplexModes: ["None"],
            colorModes: ["CMYK", "CMYKWW", "CMYKWV"]
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 20
        Layout.alignment: Qt.AlignHCenter
        width: Math.min(parent.width, 500)

        Label {
            text: "Printer Setup"
            font.pixelSize: 22
            Layout.alignment: Qt.AlignHCenter
        }

        TabBar {
            id: printerTabs
            Layout.fillWidth: true
            TabButton { text: "Nocai Printer" }
            TabButton { text: "Network Printer" }
        }

        StackLayout {
            currentIndex: printerTabs.currentIndex
            Layout.fillWidth: true

            // === Nocai Printer Tab (Default) ===
            Item {
                ColumnLayout {
                    spacing: 10
                    Layout.fillWidth: true
                    anchors.horizontalCenter: parent.horizontalCenter

                    ComboBox {
                        id: nocaiPrinterComboBox
                        Layout.fillWidth: true
                        model: ["X-33"]

                        onActivated: {
                            const selected = nocaiPrinterComboBox.currentText
                            if (selected.length > 0) {
                                appState.selectedPrinter = selected
                                appState.usingSimulatedPrinter = true

                                // Copy all necessary assets now
                                printJobNocai.prepareNocaiAssets()

                                toast.show("Nocai printer selected: " + selected)
                            }
                        }
                    }
                    ColumnLayout {
                        spacing: 10
                        Layout.fillWidth: true
                        anchors.horizontalCenter: parent.horizontalCenter

                        Label {
                            text: "The Nocai printer engine generates PRN files instead of printing directly to the Printer."
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                            Layout.maximumWidth: 400
                        }

                        Label {
                            text: "This is intended for direct USB or offline transfer to a supported Nocai printer or software such as Atools."
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                            Layout.maximumWidth: 400
                        }
                    }
                }
            }

            // === Network Printer Tab ===
            Item {
                ColumnLayout {
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: 10
                    Layout.fillWidth: true

                    ComboBox {
                        id: printerComboBox
                        Layout.fillWidth: true
                        model: printJobOutput.detectedPrinters

                        onActivated: {
                            const name = printerComboBox.currentText
                            if (printJobOutput.loadPrinter(name)) {
                                appState.selectedPrinter = name
                                appState.usingSimulatedPrinter = false

                                printJobOutput.supportedResolutions()
                                printJobOutput.supportedMediaSizes()
                                printJobOutput.supportedDuplexModes()
                                printJobOutput.supportedColorModes()

                                toast.show("Network printer loaded: " + name)
                            } else {
                                toast.show("Failed to load printer: " + name)
                            }
                        }
                    }

                    Button {
                        text: "Refresh List"
                        Layout.alignment: Qt.AlignHCenter
                        onClicked: printJobOutput.refreshDetectedPrinters()
                    }
                }
            }
        }

        // === Printer Info (after setup) ===
        GroupBox {
            title: "Selected Printer Details"
            visible: appState.selectedPrinter.length > 0
            Layout.fillWidth: true

            ColumnLayout {
                spacing: 6
                Layout.fillWidth: true

                Label { text: "Name: " + appState.selectedPrinter }
                Label { text: "Nocai Printer: " + (appState.usingSimulatedPrinter ? "Yes" : "No") }

                // Capabilities based on printer type
                Label {
                    Layout.preferredWidth: parent.width
                    Layout.maximumWidth: 480
                    text: "Supported Resolutions: " +
                        (appState.usingSimulatedPrinter
                         ? nocaiPrinterCapabilities[appState.selectedPrinter]?.resolutions?.join(", ")
                         : printJobOutput.supportedResolutions().join(", "))
                }
                Label {
                    Layout.preferredWidth: parent.width
                    Layout.maximumWidth: 480
                    text: "Media Sizes: " +
                        (appState.usingSimulatedPrinter
                         ? nocaiPrinterCapabilities[appState.selectedPrinter]?.mediaSizes?.join(", ")
                         : printJobOutput.supportedMediaSizes().join(", "))
                }
                Label {
                    Layout.preferredWidth: parent.width
                    Layout.maximumWidth: 480
                    text: "Duplex Modes: " +
                        (appState.usingSimulatedPrinter
                         ? nocaiPrinterCapabilities[appState.selectedPrinter]?.duplexModes?.join(", ")
                         : printJobOutput.supportedDuplexModes().join(", "))
                }
                Label {
                    Layout.preferredWidth: parent.width
                    Layout.maximumWidth: 480
                    text: "Color Modes: " +
                        (appState.usingSimulatedPrinter
                         ? nocaiPrinterCapabilities[appState.selectedPrinter]?.colorModes?.join(", ")
                         : printJobOutput.supportedColorModes().join(", "))
                }
            }
        }

        // === Confirm / Cancel ===
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 20

            Button {
                text: "Confirm Setup"
                enabled: appState.selectedPrinter.length > 0
                onClicked: {
                    toast.show("Printer setup complete: " + appState.selectedPrinter)
                    stackView.pop()
                }
            }

            Button {
                text: "Cancel"
                onClicked: {
                    stackView.pop()
                }
            }
        }
    }

    Toast {
        id: toast
        parent: Overlay.overlay
    }
}
