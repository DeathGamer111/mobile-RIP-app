import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import Qt.labs.platform
import QtCore

Item {
    id: root
    required property StackView stackView
    required property var appState

    signal openJobDetails(int jobIndex)

    property bool selectionMode: false
    property var selectedIndexes: []
    property string suggestedFilename: ""

    anchors.fill: parent

    function toggleSelection(index) {
        const i = selectedIndexes.indexOf(index)
        if (i >= 0) {
            selectedIndexes.splice(i, 1)
        } else {
            selectedIndexes.push(index)
        }
        selectedIndexes = selectedIndexes.slice()
    }

    function isSelected(index) {
        return selectedIndexes.indexOf(index) !== -1
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
        let all = []
        for (let i = 0; i < jobModel.count; ++i) {
            all.push(i)
        }
        selectedIndexes = all.slice() // <- Important: reassign it!
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
        anchors.margins: 20
        spacing: 12

        // Top toolbar
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 10

            Button {
                text: selectionMode ? "Cancel Selection" : "Select Jobs"
                onClicked: {
                    selectionMode = !selectionMode
                    if (!selectionMode) selectedIndexes = []
                }
            }

            Button {
                visible: selectionMode
                text: areAllJobsSelected() ? "Deselect All" : "Select All"
                onClicked: {
                    if (areAllJobsSelected()) {
                        selectAll()
                        //deselectAll()
                    } else {
                        selectAll()
                    }
                }
            }

            Button {
                text: "Remove Jobs"
                visible: selectionMode
                enabled: selectedIndexes.length > 0
                onClicked: {
                    const sorted = selectedIndexes.slice().sort((a, b) => b - a)
                    for (let i = 0; i < sorted.length; ++i) {
                        jobModel.removeJob(sorted[i])
                    }
                    selectedIndexes = []
                }
            }

            Button {
                text: "Save Jobs"
                visible: selectionMode
                enabled: selectedIndexes.length > 0
                onClicked: {
                    let jobName = jobModel.getJob(selectedIndexes[0]).name || "UntitledJob"
                    jobName = jobName.replace(/[^a-zA-Z0-9_-]/g, "_")
                    suggestedFilename = jobName + ".json"
                    saveFileDialog.open()
                }
            }
        }

        Label {
            visible: selectionMode
            text: selectedIndexes.length + " job(s) selected"
            font.pixelSize: 14
            color: "gray"
            horizontalAlignment: Text.AlignHCenter
            Layout.alignment: Qt.AlignHCenter
        }

        // Scrollable middle section (the job list)
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 12
            clip: true
            ScrollBar.vertical.policy: ScrollBar.AsNeeded
            ScrollBar.vertical.interactive: true

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
                            openJobDetails(index)
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

        // Bottom toolbar
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 10

            Button {
                text: "Add New Job"
                onClicked: jobModel.addJob("New Print Job")
            }

            Button {
                text: "Load Jobs"
                onClicked: openFileDialog.open()
            }

            Button {
                text: "Printer Setup"
                ToolTip.text: "Configure a simulated or real printer"
                ToolTip.visible: hovered
                onClicked: {
                    if (appState !== undefined) {
                        stackView.push("qrc:/PrinterSetupView.qml", {
                            stackView: stackView,
                            appState: appState
                        })
                    } else {
                        console.warn("appState is undefined!")
                    }
                }
            }

            Button {
                text: "Print Job"
                enabled: selectedIndexes.length > 0
                onClicked: {
                    if (appState.usingSimulatedPrinter) {
                        outputFileDialog.open()
                    } else {
                        printSelectedJobDirectly()
                    }
                }
                ToolTip.text: "Generate PRN file or print directly to the selected printer."
                ToolTip.visible: hovered
            }
        }

        // File Dialogs
        FileDialog {
            id: openFileDialog
            title: "Load Jobs from JSON"
            nameFilters: ["JSON Files (*.json)"]
            fileMode: FileDialog.OpenFile
            onAccepted: {
                console.log("Opening file:", file)
                jobModel.loadFromJson(file)
            }
        }

        FileDialog {
            id: saveFileDialog
            title: "Save Jobs to JSON"
            nameFilters: ["JSON Files (*.json)"]
            fileMode: FileDialog.SaveFile
            currentFile: suggestedFilename
            defaultSuffix: "json"
            onAccepted: {
                console.log("Saving file:", file)
                jobModel.saveToJson(file, selectedIndexes)
            }
        }

        FileDialog {
            id: outputFileDialog
            title: "Select PRN File Destination"
            fileMode: FileDialog.SaveFile
            nameFilters: ["PRN Files (*.prn)", "All Files (*)"]

            onAccepted: {
                const outputPath = file
                const index = selectedIndexes[0]
                const job = jobModel.getJob(index)
                const inputFile = job.imagePath

                const PRNsuccess = printJobOutput.generatePRNviaFilter(job, inputFile, outputPath)
                PRNsuccess
                    ? console.log("PRN generated at:", outputPath)
                    : console.warn("Failed to generate PRN for:", job.name)
            }
        }
    }
}
