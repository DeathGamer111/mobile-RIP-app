import QtQuick
import QtQuick.Controls

ApplicationWindow {
    id: root
    visible: true
    width: 450
    height: 600
    title: qsTr("RIP Printer App")

    StackView {
        id: stackView
        anchors.fill: parent

        // Start on Job List View
        initialItem: JobListView {
            onOpenJobDetails: (jobIndex) => {
                stackView.push(Qt.createComponent("JobDetailsView.qml"), { jobIndex: jobIndex, stackView: stackView })
            }
        }
    }
}
