import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Button {
    id: btn
    
    // Must be supplied by the caller
    required property Theme theme

	hoverEnabled: true
    clip: true
    Layout.minimumWidth: 0
	font.pixelSize: theme.buttonTextSize
    leftPadding: theme.mobile ? 14 : 10
    rightPadding: theme.mobile ? 14 : 10
    topPadding: theme.mobile ? 8 : 10
    bottomPadding: theme.mobile ? 8 : 10
    implicitHeight: theme.controlHeight

  	background: Rectangle {
        radius: btn.theme.controlRadius
        border.width: 1
		border.color: btn.hovered && btn.enabled ? btn.theme.accent2 : btn.theme.divider

        color: !btn.enabled
               ? Qt.rgba(btn.theme.surface2.r,
                          btn.theme.surface2.g,
                          btn.theme.surface2.b, 0.4)
               : btn.pressed
                   ? Qt.rgba(btn.theme.accent2.r,
                              btn.theme.accent2.g,
                              btn.theme.accent2.b, 0.25)
					: btn.hovered
						? Qt.rgba(btn.theme.accent2.r, btn.theme.accent2.g, btn.theme.accent2.b, 0.10)
	                       : btn.theme.surface2

        Behavior on color { ColorAnimation { duration: 120 } }
		Behavior on border.color { ColorAnimation { duration: 120 } }
    }

	contentItem: Label {
        text: btn.text
		color: btn.enabled ? btn.theme.text : btn.theme.subtext
        font.pixelSize: btn.font.pixelSize
        font.weight: btn.font.weight
        width: btn.availableWidth
        clip: true
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
}
