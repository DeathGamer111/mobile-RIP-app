// Toast.qml
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: toast
    modal: false
    focus: false
    x: (parent.width - width) / 2
    y: (parent.height - height) / 2
    padding: 12
    background: Rectangle {
        color: "#333"
        radius: 10
        opacity: 0.9
    }

    Label {
        id: toastLabel
        text: ""
        color: "white"
        font.pixelSize: 16
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

    property int duration: 2000

    Timer {
        id: hideTimer
        interval: duration
        onTriggered: toast.close()
    }

    function show(message) {
        toastLabel.text = message
        open()
        hideTimer.restart()
    }
}
