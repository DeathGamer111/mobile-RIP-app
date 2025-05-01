import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt.labs.platform

Page {
    id: setupPage
    required property StackView stackView
    required property var appState

    Component.onCompleted: printJobOutput.refreshDetectedPrinters()

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
            TabButton { text: "Network Printer" }
            TabButton { text: "Simulated Printer" }
        }

        StackLayout {
            currentIndex: printerTabs.currentIndex
            Layout.fillWidth: true

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

            // === Simulated Printer Tab ===
            Item {
                ColumnLayout {
                    spacing: 10
                    anchors.horizontalCenter: parent.horizontalCenter
                    Layout.fillWidth: true

                    TextField {
                        id: simulatedNameField
                        placeholderText: "Simulated printer name"
                        text: appState.selectedPrinter
                        Layout.fillWidth: true
                    }

                    RowLayout {
                        spacing: 10
                        Layout.fillWidth: true

                        TextField {
                            id: ppdPathField
                            placeholderText: "No PPD file selected"
                            text: appState.selectedPPD
                            readOnly: true
                            Layout.fillWidth: true
                        }

                        Button {
                            text: "Browse..."
                            onClicked: ppdDialog.open()
                        }
                    }

                    Button {
                        text: "Register Simulated Printer"
                        enabled: simulatedNameField.text.length > 0 && ppdPathField.text.length > 0
                        onClicked: {
                            const name = simulatedNameField.text
                            const file = ppdPathField.text

                            const ppdOk = printJobOutput.loadPPDFile(file)
                            const regOk = printJobOutput.registerPrinterFromPPD(name, file)
                            const loadOk = printJobOutput.loadPrinter(name)

                            if (ppdOk && regOk && loadOk) {
                                appState.selectedPrinter = name
                                appState.selectedPPD = file
                                appState.usingSimulatedPrinter = true
                                toast.show("Simulated printer loaded: " + name)
                            } else {
                                toast.show("Failed to simulate printer: " + name)
                            }
                        }
                    }

                    FileDialog {
                        id: ppdDialog
                        title: "Select a PPD file"
                        nameFilters: ["PPD Files (*.ppd)"]
                        fileMode: FileDialog.OpenFile
                        onAccepted: {
                            ppdPathField.text = file
                            appState.selectedPPD = file
                        }
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
                Label { text: "Simulated: " + (appState.usingSimulatedPrinter ? "Yes" : "No") }

                // Optional Capabilities List
                Label { text: "Supported Resolutions: " + printJobOutput.supportedResolutions().join(", ") }
                Label { text: "Media Sizes: " + printJobOutput.supportedMediaSizes().join(", ") }
                Label { text: "Duplex Modes: " + printJobOutput.supportedDuplexModes().join(", ") }
                Label { text: "Color Modes: " + printJobOutput.supportedColorModes().join(", ") }
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
