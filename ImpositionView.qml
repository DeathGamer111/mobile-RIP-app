import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Page {
    id: impositionView
    title: "Imposition Editor"

    property var jobData
    property string imagePath: jobData.imagePath
    property size paperSize: jobData.paperSize
    property real scaleFactor: Math.min(impositionBox.width / paperSize.width, impositionBox.height / paperSize.height)


    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        Rectangle {
            id: impositionBox
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#f0f0f0"
            border.color: "black"
            border.width: 1

            Item {
                id: paperArea
                width: paperSize.width * scaleFactor
                height: paperSize.height * scaleFactor
                anchors.centerIn: parent
                clip: true

                DraggableItem {
                    id: imageContainer
                    itemX: (jobData.imagePosition.width || 0) * scaleFactor
                    itemY: (jobData.imagePosition.height || 0) * scaleFactor

                    sourceComponent: Image {
                        id: jobImage
                        source: jobData.imagePath
                        width: jobData.imageWidth || 300 * scaleFactor
                        height: jobData.imageHeight || 200 * scaleFactor
                        fillMode: Image.PreserveAspectFit
                    }
                }


                /*
                DraggableItem {
                    id: textContainer
                    itemX: jobData.textX || 50
                    itemY: jobData.textY || 50

                    sourceComponent: Text {
                        id: exampleText
                        text: "Sample Text"
                        font.pointSize: 14
                        color: "blue"
                    }
                }
                */
            }
        }

        RowLayout {
            Layout.alignment: Qt.AlignCenter
            spacing: 20

            Button {
                text: "Back"
                onClicked: stackView.pop()
            }

            Button {
                text: "Save"
                onClicked: {
                    jobModel.updateJob(jobData.index, {

                        imagePosition: Qt.size(
                            Math.round(imageContainer.x / scaleFactor),
                            Math.round(imageContainer.y / scaleFactor)
                        ),

                        imageWidth: jobImage.width / scaleFactor,
                        imageHeight: jobImage.height / scaleFactor,
                        textX: textContainer.x / scaleFactor,
                        textY: textContainer.y / scaleFactor
                    })
                    toast.show("Job Successfully Saved!")
                }
            }

            Toast {
                id: toast
                parent: Overlay.overlay
            }
        }
    }
}
