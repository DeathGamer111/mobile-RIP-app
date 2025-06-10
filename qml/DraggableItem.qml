import QtQuick

Item {
    id: root

    property Component sourceComponent
    property real itemX: 0
    property real itemY: 0
    property alias contentItem: contentLoader.item

    x: itemX
    y: itemY

    // Use an implicit size fallback
    width: loaderWrapper.implicitWidth
    height: loaderWrapper.implicitHeight

    Item {
        id: loaderWrapper
        anchors.fill: parent

        Loader {
            id: contentLoader
            anchors.fill: parent
            sourceComponent: root.sourceComponent
            onLoaded: {
                // Defensive: assign size to parent if not auto-sized
                if (item && item.implicitWidth > 0)
                    loaderWrapper.implicitWidth = item.implicitWidth
                if (item && item.implicitHeight > 0)
                    loaderWrapper.implicitHeight = item.implicitHeight
            }
        }

        MouseArea {
            anchors.fill: parent
            drag.target: root
        }

        MultiPointTouchArea {
            anchors.fill: parent
            touchPoints: [ TouchPoint { id: touch } ]
            onTouchUpdated: {
                root.x += touch.x - touch.previousX
                root.y += touch.y - touch.previousY
            }
        }
    }
}
