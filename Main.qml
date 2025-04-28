import QtQuick
import QtQuick.Controls

ApplicationWindow {
    id: root
    visible: true
    width: 450
    height: 600
    title: qsTr("RIP Printer App")

    QtObject {
        id: appState
        property string selectedPrinter: ""
        property string selectedPPD: ""
        property bool usingSimulatedPrinter: false
    }

    StackView {
        id: stackView
        anchors.fill: parent

        // Start on Job List View
        initialItem: JobListView {
            stackView: stackView
            appState: appState
            onOpenJobDetails: (jobIndex) => {
                stackView.push(Qt.createComponent("JobDetailsView.qml"), {
                    jobIndex: jobIndex,
                    stackView: stackView,
                    appState: appState
                })
            }
        }
    }
}
