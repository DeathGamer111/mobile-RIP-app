import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import Qt.labs.platform

Item {
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
        selectedIndexes = selectedIndexes.slice() // trigger UI update
    }

    function isSelected(index) {
        return selectedIndexes.indexOf(index) !== -1
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 20
        width: Math.min(parent.width, 500)
        anchors.horizontalCenter: parent.horizontalCenter

        // Selection Controls
        ColumnLayout {
            spacing: 8
            Layout.alignment: Qt.AlignHCenter

            RowLayout {
                spacing: 8
                Layout.alignment: Qt.AlignHCenter

                Button {
                    text: selectionMode ? "Cancel Selection" : "Select Jobs"
                    onClicked: {
                        selectionMode = !selectionMode
                        if (!selectionMode) selectedIndexes = []
                    }
                }

                Button {
                    text: "Remove Jobs"
                    visible: selectionMode
                    enabled: selectedIndexes.length > 0
                    onClicked: {
                        for (let i = selectedIndexes.length - 1; i >= 0; i--) {
                            jobModel.removeJob(selectedIndexes[i])
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
                        jobName = jobName.replace(/[^a-zA-Z0-9_-]/g, "_") // safe filename
                        suggestedFilename = jobName + ".json"
                        saveFileDialog.open()
                    }
                }
            }
        }

        // Scrollable job list
        ScrollView {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: Math.min(parent.width, 450)
            Layout.preferredHeight: 400
            ScrollBar.vertical.policy: ScrollBar.AsNeeded
            ScrollBar.vertical.interactive: true

            Component.onCompleted: {
                contentItem.flickableDirection = Flickable.VerticalFlick
            }

            ListView {
                id: jobListView
                width: parent.width
                model: jobModel
                spacing: 6
                clip: true

                delegate: ItemDelegate {
                    id: itemDelegate
                    width: parent.width
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
                            id: checkBox
                            visible: selectionMode
                            checked: isSelected(index)
                            onClicked: toggleSelection(index)
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

        // File dialogs
        FileDialog {
            id: openFileDialog
            title: "Load Jobs from JSON"
            nameFilters: ["JSON Files (*.json)"]
            onAccepted: jobModel.loadFromJson(file)
        }

        FileDialog {
            id: saveFileDialog
            title: "Save Jobs to JSON"
            nameFilters: ["JSON Files (*.json)"]
            fileMode: FileDialog.SaveFile
            currentFile: suggestedFilename
            onAccepted: jobModel.saveToJson(file)
        }

        // Bottom row for core actions
        RowLayout {
            spacing: 10
            Layout.alignment: Qt.AlignHCenter

            Button {
                text: "Add New Job"
                onClicked: jobModel.addJob("New Print Job")
            }

            Button {
                text: "Load Jobs"
                onClicked: openFileDialog.open()
            }

            Button {
                text: "Load PPD"
                onClicked: ppdDialog.open()
                ToolTip.text: "Select a PPD file to simulate printer for PRN output"
                ToolTip.visible: hovered
            }

            FileDialog {
                id: ppdDialog
                title: "Select Printer PPD File"
                nameFilters: ["PPD Files (*.ppd)"]
                fileMode: FileDialog.OpenFile
                onAccepted: {

                    const PPDsuccess = printJobOutput.loadPPDFile(QUrl(file).toLocalFile())
                    PPDsuccess
                        ? console.log("Loaded PPD File:", file)
                        : console.warn("Failed to Load PPD File:", file)

                    const RegisterSuccess = printJobOutput.registerPrinterFromPPD("TestEpson", QUrl(file).toLocalFile());
                    RegisterSuccess
                        ? console.log("Registered PPD as printer:", file)
                        : console.warn("Failed to Register PPD File as printer:", file)

                    const RegisterPrinterSucces = printJobOutput.loadPrinter("TestEpson");
                    RegisterPrinterSucces
                        ? console.log("Loaded Printer from PPD File", file)
                        : console.warn("Failed to Load Printer from PPD file:", file)
                }
            }

            Button {
                text: "Print Job"
                enabled: selectedIndexes.length > 0
                onClicked: outputFileDialog.open()
                ToolTip.text: "Generate PRN output file(s) for the selected job(s)."
                ToolTip.visible: hovered
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

                    const PRNsuccess = printJobOutput.generatePRN(job, inputFile, outputPath)
                    PRNsuccess
                        ? console.log("PRN generated at:", outputPath)
                        : console.warn("Failed to generate PRN for:", job.name)
                }
            }
        }
    }
}
