import QtQuick
import QtQuick.Layouts
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

        Component.onCompleted: {
            stackView.push("qrc:/qml/JobListView.qml", {
                stackView: stackView,
                appState: appState,
                jobModel: jobModel
            })
        }
    }
}
