import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "."

Page {
    id: root
    required property StackView stackView
    required property var appState
    required property Theme theme

    background: Rectangle { color: theme.bg }

    property string activeAction: ""
    property var pendingSettings: ({})

    property int draftPrintDirection: appState.sdkPrintDirection
    property int draftPrintSpeed: appState.sdkPrintSpeed
    property int draftWcSequence: appState.sdkWcSequence
    property int draftEclosionGrade: appState.sdkEclosionGrade
    property int draftHeadSelect: appState.sdkHeadSelect
    property int draftWhiteInkPercent: appState.sdkWhiteInkPercent
    property int draftWhiteInkPassCount: appState.sdkWhiteInkPassCount
    property int draftVarnishInkPercent: appState.sdkVarnishInkPercent
    property int draftVarnishInkPassCount: appState.sdkVarnishInkPassCount
    property int draftHeadVoltage: appState.sdkHeadVoltage
    property int draftCarReset: appState.sdkCarReset
    property int draftStripBlank: appState.sdkStripBlank
    property int draftBlankDistance: appState.sdkBlankDistance
    property int draftPass: appState.sdkPass
    property int draftVsdMode: appState.sdkVsdMode
    property int draftDisableUv0: appState.sdkDisableUv0
    property int draftDisableUv1: appState.sdkDisableUv1
    property int draftDisableUv2: appState.sdkDisableUv2
    property int draftDisableUv3: appState.sdkDisableUv3
    property int draftDisableUv4: appState.sdkDisableUv4
    property int draftDisableUv5: appState.sdkDisableUv5

    readonly property bool operationBusy: nocaiDirectPrint.maintenanceBusy
    readonly property bool canApply: nocaiDirectPrint.connected && !operationBusy

    function goBack() {
        if (root.stackView && root.stackView.depth > 1)
            root.stackView.pop()
        else if (StackView.view)
            StackView.view.pop()
    }

    function settingsMap() {
        return {
            "selectedPrinterIndex": appState.sdkSelectedPrinterIndex,
            "printDirection": draftPrintDirection,
            "printSpeed": draftPrintSpeed,
            "wcSequence": draftWcSequence,
            "eclosionGrade": draftEclosionGrade,
            "headSelect": draftHeadSelect,
            "whiteInkPercent": draftWhiteInkPercent,
            "whiteInkPassCount": draftWhiteInkPassCount,
            "varnishInkPercent": draftVarnishInkPercent,
            "varnishInkPassCount": draftVarnishInkPassCount,
            "headVoltage": draftHeadVoltage,
            "disableUv0": draftDisableUv0,
            "disableUv1": draftDisableUv1,
            "disableUv2": draftDisableUv2,
            "disableUv3": draftDisableUv3,
            "disableUv4": draftDisableUv4,
            "disableUv5": draftDisableUv5,
            "carReset": draftCarReset,
            "stripBlank": draftStripBlank,
            "blankDistance": draftBlankDistance,
            "pass": draftPass,
            "vsdMode": draftVsdMode
        }
    }

    function commitSettings(settings) {
        colorManager.setDirectPrintSettings(settings)
        appState.sdkPrintDirection = settings.printDirection
        appState.sdkPrintSpeed = settings.printSpeed
        appState.sdkWcSequence = settings.wcSequence
        appState.sdkEclosionGrade = settings.eclosionGrade
        appState.sdkHeadSelect = settings.headSelect
        appState.sdkWhiteInkPercent = settings.whiteInkPercent
        appState.sdkWhiteInkPassCount = settings.whiteInkPassCount
        appState.sdkVarnishInkPercent = settings.varnishInkPercent
        appState.sdkVarnishInkPassCount = settings.varnishInkPassCount
        appState.sdkHeadVoltage = settings.headVoltage
        appState.sdkDisableUv0 = settings.disableUv0
        appState.sdkDisableUv1 = settings.disableUv1
        appState.sdkDisableUv2 = settings.disableUv2
        appState.sdkDisableUv3 = settings.disableUv3
        appState.sdkDisableUv4 = settings.disableUv4
        appState.sdkDisableUv5 = settings.disableUv5
        appState.sdkCarReset = settings.carReset
        appState.sdkStripBlank = settings.stripBlank
        appState.sdkBlankDistance = settings.blankDistance
        appState.sdkPass = settings.pass
        appState.sdkVsdMode = settings.vsdMode
    }

    function applySettings() {
        if (!canApply) {
            toast.show(nocaiDirectPrint.connected
                       ? "Wait for the current printer operation to finish."
                       : "Connect the printer before applying SDK settings.")
            return
        }
        pendingSettings = settingsMap()
        activeAction = "SetJobSettings"
        if (!nocaiDirectPrint.startMaintenanceAction(
                "SetJobSettings", {"settings": pendingSettings})) {
            activeAction = ""
            pendingSettings = ({})
            toast.show("Could not apply printer settings: " + nocaiDirectPrint.lastError)
        }
    }

    function reconnectPrinter() {
        activeAction = "ReconnectPrinter"
        if (!nocaiDirectPrint.startReconnectPrinter(appState.sdkSelectedPrinterIndex)) {
            activeAction = ""
            toast.show("Could not start reconnect: " + nocaiDirectPrint.lastError)
        }
    }

    function restartPrinterService() {
        activeAction = "RestartPrinterService"
        if (!nocaiDirectPrint.startRestartService(appState.sdkSelectedPrinterIndex)) {
            activeAction = ""
            toast.show("Could not restart the printer service: " + nocaiDirectPrint.lastError)
        }
    }

    function copyDiagnosticLog() {
        const logText = nocaiDirectPrint.diagnosticLog()
        if (!logText || logText.length === 0) {
            toast.show("No printer service log is available yet.")
            return
        }
        clipboardBuffer.text = logText
        clipboardBuffer.selectAll()
        clipboardBuffer.copy()
        clipboardBuffer.deselect()
        toast.show("Printer diagnostic log copied to the clipboard.")
    }

    Connections {
        target: nocaiDirectPrint
        function onMaintenanceActionFinished(action, succeeded, result, errorMessage) {
            if (action === "SetJobSettings") {
                if (succeeded) {
                    root.commitSettings(root.pendingSettings)
                    toast.show("Printer SDK settings applied.")
                } else {
                    toast.show("Printer rejected the settings: " + errorMessage)
                }
                root.pendingSettings = ({})
            } else if (action === "ReconnectPrinter") {
                toast.show(succeeded
                           ? "Printer connection is ready."
                           : "Reconnect failed: " + errorMessage)
            } else if (action === "RestartPrinterService") {
                toast.show(succeeded
                           ? "Printer service restarted and connected."
                           : "Service restart failed: " + errorMessage)
            } else {
                return
            }
            root.activeAction = ""
        }
    }

    TextArea {
        id: clipboardBuffer
        width: 1
        height: 1
        opacity: 0
        readOnly: true
    }

    Rectangle {
        id: headerBar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 60
        color: theme.surface

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            spacing: 10

            ThemedButton {
                text: strings.trKey("common.back")
                theme: root.theme
                Layout.preferredWidth: root.theme.headerButtonWidth(root.width)
                padding: 12
                font.pixelSize: 15
                onClicked: root.goBack()
            }

            Item { Layout.fillWidth: true }

            Label {
                text: "Printer Settings"
                color: theme.text
                font.pixelSize: root.theme.headerTitleSize(root.width)
                font.weight: Font.Medium
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
                Layout.fillWidth: true
                Layout.minimumWidth: 0
            }

            Item { Layout.fillWidth: true }

            ThemedButton {
                text: root.activeAction === "SetJobSettings" ? "Applying…" : "Apply"
                theme: root.theme
                Layout.preferredWidth: root.theme.headerButtonWidth(root.width)
                padding: 12
                font.pixelSize: 15
                enabled: root.canApply
                onClicked: root.applySettings()
            }
        }
    }

    ScrollView {
        id: scroll
        anchors.top: headerBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: theme.pageMargin
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: scroll.availableWidth
            spacing: 14

            Pane {
                Layout.fillWidth: true
                padding: 12
                background: Rectangle {
                    color: theme.surface
                    radius: 12
                    border.width: 1
                    border.color: theme.divider
                }

                ColumnLayout {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: theme.boundedWidth(parent.width, 560)
                    spacing: 10

                    Label {
                        text: "Connection Recovery"
                        color: theme.text
                        font.pixelSize: 18
                        font.weight: Font.Medium
                        Layout.alignment: Qt.AlignHCenter
                    }
                    Label {
                        text: "Use these controls only when the printer connection is not responding. Recovery operations never run in a loop."
                        color: theme.subtext
                        wrapMode: Text.WordWrap
                        horizontalAlignment: Text.AlignHCenter
                        Layout.fillWidth: true
                    }
                    Rectangle { Layout.preferredHeight: 1; Layout.fillWidth: true; color: theme.divider }

                    ThemedButton {
                        text: root.activeAction === "ReconnectPrinter"
                              ? "Reconnecting…" : "Reconnect Printer"
                        theme: root.theme
                        Layout.fillWidth: true
                        Layout.preferredHeight: 42
                        enabled: !root.operationBusy
                        onClicked: root.reconnectPrinter()
                    }
                    ThemedButton {
                        text: root.activeAction === "RestartPrinterService"
                              ? "Restarting Service…" : "Restart Printer Service"
                        theme: root.theme
                        Layout.fillWidth: true
                        Layout.preferredHeight: 42
                        enabled: !root.operationBusy
                        onClicked: restartConfirmation.open()
                    }
                    ThemedButton {
                        text: "Copy Diagnostic Log"
                        theme: root.theme
                        Layout.fillWidth: true
                        Layout.preferredHeight: 42
                        enabled: !root.operationBusy
                        onClicked: root.copyDiagnosticLog()
                    }
                }
            }

            Pane {
                Layout.fillWidth: true
                padding: 12
                background: Rectangle {
                    color: theme.surface
                    radius: 12
                    border.width: 1
                    border.color: theme.divider
                }

                ColumnLayout {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: theme.boundedWidth(parent.width, 560)
                    spacing: 10

                    Label {
                        text: "SDK Print Settings"
                        color: theme.text
                        font.pixelSize: 18
                        font.weight: Font.Medium
                        Layout.alignment: Qt.AlignHCenter
                    }
                    Label {
                        text: root.canApply
                              ? "Apply sends these values to the connected printer SDK."
                              : "Connect the printer to apply settings to the SDK."
                        color: root.canApply ? theme.subtext : theme.warning
                        wrapMode: Text.WordWrap
                        horizontalAlignment: Text.AlignHCenter
                        Layout.fillWidth: true
                    }
                    Rectangle { Layout.preferredHeight: 1; Layout.fillWidth: true; color: theme.divider }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: theme.gridColumns(width, 2, 150)
                        columnSpacing: 12
                        rowSpacing: 8

                        Label { text: "Print Direction"; color: theme.text; Layout.fillWidth: true }
                        SpinBox { Layout.fillWidth: true; from: 0; to: 3; value: root.draftPrintDirection; onValueModified: root.draftPrintDirection = value }
                        Label { text: "Print Speed"; color: theme.text; Layout.fillWidth: true }
                        SpinBox { Layout.fillWidth: true; from: 0; to: 3; value: root.draftPrintSpeed; onValueModified: root.draftPrintSpeed = value }
                        Label { text: "WC Sequence"; color: theme.text; Layout.fillWidth: true }
                        SpinBox { Layout.fillWidth: true; from: 0; to: 1; value: root.draftWcSequence; onValueModified: root.draftWcSequence = value }
                        Label { text: "Eclosion Grade"; color: theme.text; Layout.fillWidth: true }
                        SpinBox { Layout.fillWidth: true; from: 0; to: 3; value: root.draftEclosionGrade; onValueModified: root.draftEclosionGrade = value }
                        Label { text: "Head Select"; color: theme.text; Layout.fillWidth: true }
                        SpinBox { Layout.fillWidth: true; from: 0; to: 2; value: root.draftHeadSelect; onValueModified: root.draftHeadSelect = value }
                        Label { text: "Head Voltage"; color: theme.text; Layout.fillWidth: true }
                        SpinBox { Layout.fillWidth: true; from: 400; to: 600; value: root.draftHeadVoltage; onValueModified: root.draftHeadVoltage = value }
                        Label { text: "Print Pass"; color: theme.text; Layout.fillWidth: true }
                        SpinBox { Layout.fillWidth: true; from: 0; to: 255; value: root.draftPass; onValueModified: root.draftPass = value }
                        Label { text: "VSD Mode"; color: theme.text; Layout.fillWidth: true }
                        SpinBox { Layout.fillWidth: true; from: 0; to: 65535; value: root.draftVsdMode; onValueModified: root.draftVsdMode = value }
                        Label { text: "Strip Blank"; color: theme.text; Layout.fillWidth: true }
                        SpinBox { Layout.fillWidth: true; from: 0; to: 2; value: root.draftStripBlank; onValueModified: root.draftStripBlank = value }
                        Label { text: "Blank Distance"; color: theme.text; Layout.fillWidth: true }
                        SpinBox { Layout.fillWidth: true; from: 0; to: 65535; value: root.draftBlankDistance; onValueModified: root.draftBlankDistance = value }
                        Label { text: "Reset Carriage"; color: theme.text; Layout.fillWidth: true }
                        CheckBox { checked: root.draftCarReset === 1; onToggled: root.draftCarReset = checked ? 1 : 0 }
                    }
                }
            }

            Pane {
                Layout.fillWidth: true
                padding: 12
                background: Rectangle {
                    color: theme.surface
                    radius: 12
                    border.width: 1
                    border.color: theme.divider
                }

                ColumnLayout {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: theme.boundedWidth(parent.width, 560)
                    spacing: 10

                    Label {
                        text: "Ink Settings"
                        color: theme.text
                        font.pixelSize: 18
                        font.weight: Font.Medium
                        Layout.alignment: Qt.AlignHCenter
                    }
                    Rectangle { Layout.preferredHeight: 1; Layout.fillWidth: true; color: theme.divider }
                    GridLayout {
                        Layout.fillWidth: true
                        columns: theme.gridColumns(width, 2, 150)
                        columnSpacing: 12
                        rowSpacing: 8

                        Label { text: "White Ink Percent"; color: theme.text; Layout.fillWidth: true }
                        SpinBox { Layout.fillWidth: true; from: 0; to: 9; value: root.draftWhiteInkPercent; onValueModified: root.draftWhiteInkPercent = value }
                        Label { text: "White Ink Pass"; color: theme.text; Layout.fillWidth: true }
                        SpinBox { Layout.fillWidth: true; from: 0; to: 255; value: root.draftWhiteInkPassCount; onValueModified: root.draftWhiteInkPassCount = value }
                        Label { text: "Varnish Ink Percent"; color: theme.text; Layout.fillWidth: true }
                        SpinBox { Layout.fillWidth: true; from: 0; to: 9; value: root.draftVarnishInkPercent; onValueModified: root.draftVarnishInkPercent = value }
                        Label { text: "Varnish Ink Pass"; color: theme.text; Layout.fillWidth: true }
                        SpinBox { Layout.fillWidth: true; from: 0; to: 255; value: root.draftVarnishInkPassCount; onValueModified: root.draftVarnishInkPassCount = value }
                    }
                }
            }

            Pane {
                Layout.fillWidth: true
                padding: 12
                background: Rectangle {
                    color: theme.surface
                    radius: 12
                    border.width: 1
                    border.color: theme.divider
                }

                ColumnLayout {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: theme.boundedWidth(parent.width, 560)
                    spacing: 10

                    Label {
                        text: "Disable UV Lights"
                        color: theme.text
                        font.pixelSize: 18
                        font.weight: Font.Medium
                        Layout.alignment: Qt.AlignHCenter
                    }
                    Label {
                        text: "Checked directions will not activate the corresponding UV lamp."
                        color: theme.warning
                        wrapMode: Text.WordWrap
                        horizontalAlignment: Text.AlignHCenter
                        Layout.fillWidth: true
                    }
                    Rectangle { Layout.preferredHeight: 1; Layout.fillWidth: true; color: theme.divider }
                    GridLayout {
                        Layout.fillWidth: true
                        columns: theme.gridColumns(width, 2, 180)
                        columnSpacing: 12
                        rowSpacing: 6

                        CheckBox { text: "Right lamp, R→L off"; checked: root.draftDisableUv0 === 1; onToggled: root.draftDisableUv0 = checked ? 1 : 0 }
                        CheckBox { text: "Right lamp, L→R off"; checked: root.draftDisableUv1 === 1; onToggled: root.draftDisableUv1 = checked ? 1 : 0 }
                        CheckBox { text: "Left lamp, R→L off"; checked: root.draftDisableUv2 === 1; onToggled: root.draftDisableUv2 = checked ? 1 : 0 }
                        CheckBox { text: "Left lamp, L→R off"; checked: root.draftDisableUv3 === 1; onToggled: root.draftDisableUv3 = checked ? 1 : 0 }
                        CheckBox { text: "UV lamp, R→L off"; checked: root.draftDisableUv4 === 1; onToggled: root.draftDisableUv4 = checked ? 1 : 0 }
                        CheckBox { text: "UV lamp, L→R off"; checked: root.draftDisableUv5 === 1; onToggled: root.draftDisableUv5 = checked ? 1 : 0 }
                    }
                }
            }

            Item { Layout.preferredHeight: 6 }
        }
    }

    Dialog {
        id: restartConfirmation
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: "Restart Printer Service?"
        standardButtons: Dialog.Ok | Dialog.Cancel
        width: Math.min(420, parent ? parent.width - 32 : 420)

        Label {
            width: parent.width
            text: "This closes the current SDK session and starts a new service. Use it only when Reconnect Printer does not recover the connection. The printer may still require a power cycle if it retained a stale hardware session."
            color: theme.text
            wrapMode: Text.WordWrap
        }

        onAccepted: root.restartPrinterService()
    }

    Toast {
        id: toast
        parent: Overlay.overlay
    }
}
