import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import Qt.labs.platform
import QtCore

Item {
    id: root
    required property StackView stackView
    required property var jobModel
    required property var appState

    property bool selectionMode: false
    property var selectedIndexes: []
    property string suggestedFilename: ""

    anchors.fill: parent

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

        function isSelected(index) {
            return selectedIndexes.includes(index)
        }

        function areAllJobsSelected() {
            if (selectedIndexes.length !== jobModel.count)
                return false

            for (let i = 0; i < jobModel.count; ++i) {
                if (selectedIndexes.indexOf(i) === -1)
                    return false
            }
            return true
        }

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

        function deselectAll() {
            selectedIndexes = []
        }

        function printSelectedJobDirectly() {
            const index = selectedIndexes[0]
            const job = jobModel.getJob(index)
            const inputFile = job.imagePath

            const outputPath = "" // Empty because printing directly to printer

            const success = printJobOutput.generatePRN(job, inputFile, outputPath)
            if (success) {
                console.log("Print job sent to printer:", appState.selectedPrinter)
                toast.show("Print job sent successfully.")
            } else {
                console.warn("Failed to print job:", job.name)
                toast.show("Failed to print job.")
            }
        }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // === Top Header ===
        Rectangle {
            height: 60
            width: parent.width
            color: "#2c3e50"
            Layout.fillWidth: true

            RowLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 15

                Image {
                    source: "qrc:/assets/logo.png"
                    Layout.preferredWidth: 40
                    Layout.preferredHeight: 40
                    Layout.margins: 4
                    fillMode: Image.PreserveAspectFit
                    smooth: true
                }

                Label {
                    text: "Print Job Manager"
                    font.pixelSize: 22
                    color: "white"
                    verticalAlignment: Text.AlignVCenter
                }

                Item { Layout.fillWidth: true }

                Button {
                    text: "Printer Setup"
                    onClicked: stackView.push("qrc:/qml/PrinterSetupView.qml", {
                        stackView: stackView,
                        appState: appState
                    })
                }
            }
        }

        // === Top Toolbar ===
        Frame {
            Layout.fillWidth: true
            padding: 10
            background: Rectangle { color: "#34495e" }

            RowLayout {
                spacing: 10
                Layout.alignment: Qt.AlignHCenter

                Button {
                    text: selectionMode ? "Cancel Selection" : "Select Jobs"
                    onClicked: {
                        selectedIndexes = []
                        selectionMode = !selectionMode
                    }
                }

                Button {
                    text: areAllJobsSelected() ? "Deselect All" : "Select All"
                    visible: selectionMode
                    onClicked: areAllJobsSelected() ? deselectAll() : selectAll()
                }

                Button {
                    text: "Remove Jobs"
                    visible: selectionMode
                    enabled: selectedIndexes.length > 0
                    onClicked: {
                        const sorted = selectedIndexes.slice().sort((a, b) => b - a)
                        for (let i = 0; i < sorted.length; ++i)
                            jobModel.removeJob(sorted[i])
                        selectedIndexes = []
                    }
                }

                Button {
                    text: "Save Jobs"
                    visible: selectionMode
                    enabled: selectedIndexes.length > 0
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

        // === Selection Info ===
        Label {
            visible: selectionMode
            text: selectedIndexes.length + " job(s) selected"
            font.pixelSize: 14
            color: "#7f8c8d"
            Layout.topMargin: 10
            Layout.bottomMargin: 10
            horizontalAlignment: Text.AlignHCenter
            Layout.fillWidth: true
        }

        // === Scrollable Job List ===
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 12
            clip: true

            ListView {
                id: jobListView
                width: Math.min(parent.width, 500)
                model: jobModel
                spacing: 6
                anchors.horizontalCenter: parent.horizontalCenter

                delegate: ItemDelegate {
                    width: jobListView.width
                    highlighted: selectionMode && isSelected(index)

                    onClicked: {
                        if (selectionMode) {
                            toggleSelection(index)
                        } else {
                            stackView.push("qrc:/qml/JobDetailsView.qml", {
                                jobIndex: index,
                                stackView: stackView,
                                appState: appState,
                                jobModel: jobModel
                            })
                        }
                    }

                    contentItem: RowLayout {
                        anchors.fill: parent
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
                        }
                    }
                }
            }
        }

        // === Bottom Toolbar ===
        // === Bottom Toolbar ===
        Rectangle {
            Layout.fillWidth: true
            height: 50
            color: "#34495e"

            RowLayout {
                anchors.centerIn: parent
                spacing: 20

                Button {
                    text: "Add New Job"
                    onClicked: jobModel.addJob("New Print Job")
                }

                Button {
                    text: "Load Jobs"
                    onClicked: openFileDialog.open()
                }

                Button {
                    text: "Print Job"
                    enabled: selectedIndexes.length > 0
                    onClicked: {
                        appState.usingSimulatedPrinter
                            ? outputFileDialog.open()
                            : printSelectedJobDirectly()
                    }
                    ToolTip.text: "Generate PRN file or print directly to the selected printer."
                    ToolTip.visible: hovered
                }
            }
        }


        // === File Dialogs ===
        FileDialog {
            id: openFileDialog
            title: "Load Jobs from JSON"
            nameFilters: ["JSON Files (*.json)"]
            fileMode: FileDialog.OpenFile
            onAccepted: jobModel.loadFromJson(file)
        }

        FileDialog {
            id: saveFileDialog
            title: "Save Jobs to JSON"
            nameFilters: ["JSON Files (*.json)"]
            fileMode: FileDialog.SaveFile
            defaultSuffix: "json"
            onAccepted: jobModel.saveToJson(file, selectedIndexes)
        }

        FileDialog {
            id: outputFileDialog
            title: "Select PRN File Destination"
            nameFilters: ["PRN Files (*.prn)", "All Files (*)"]
            fileMode: FileDialog.SaveFile
            onAccepted: {
                const outputPath = file
                const job = jobModel.getJob(selectedIndexes[0])

                const loaded = printJobNocai.loadInputImage(job.imagePath)
                const profiled = printJobNocai.applyICCConversion(
                    "file:///home/mccalla/Documents/sRGBProfile.icm",
                    "file:///home/mccalla/Documents/X-33_1440H_280.icc"
                )
                const halftoned = printJobNocai.generateFinalPRN(
                    outputPath,
                    720,
                    720
                )

                if (loaded && profiled && halftoned) {
                    console.log("PRN generated successfully:", outputPath)
                    toast.show("PRN generated successfully.")
                } else {
                    console.warn("Failed to generate PRN for:", job.name)
                    toast.show("Failed to generate PRN file.")
                }
            }
        }

        Toast {
            id: toast
            parent: Overlay.overlay
        }
    }
}
