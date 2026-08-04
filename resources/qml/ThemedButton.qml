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
    font.pixelSize: 13
    padding: 10

  	background: Rectangle {
        radius: 8
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
        width: btn.availableWidth
        clip: true
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
}
