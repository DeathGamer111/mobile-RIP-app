// Toast.qml
import QtQuick
import QtQuick.Controls

Popup {
    id: toast
    modal: false
    focus: false
    width: Math.max(180, Math.min((parent ? parent.width : 360) - 32, Math.max(220, toastLabel.implicitWidth + padding * 2)))
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0
    padding: 12
    background: Rectangle {
        color: "#333"
        radius: 10
        opacity: 0.9
    }

    Label {
        id: toastLabel
        width: parent.width
        text: ""
        color: "white"
        font.pixelSize: 16
        wrapMode: Text.WordWrap
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

    property int duration: 2000

    Timer {
        id: hideTimer
        interval: toast.duration
        onTriggered: toast.close()
    }

    function show(message) {
        toastLabel.text = message
        open()
        hideTimer.restart()
    }
}
