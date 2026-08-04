import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt.labs.platform as P
import "."

Page {
    id: root

    required property var stackView
    required property var appState
    required property Theme theme

    readonly property bool maintenanceSupported: nocaiDirectPrint.supportsMaintenance(appState.selectedPrinter)
    readonly property bool controlsEnabled: maintenanceSupported && nocaiDirectPrint.available
    property bool statusPollingEnabled: false

    property int headMask: 3
    property int axis: 0
    property int axisDirection: 0
    property real printHeight: 0.0
    property int printX: 0
    property int printY: 0
    property int alignmentType: 0
    property int alignmentPatternType: 0
    property int uvType: 0
    property int newUvType: 0
    property int newUvFunctionType: 0
    property string statusText: nocaiDirectPrint.statusText()
    property var printerStatus: ({})
    property var jobSettings: ({})
    property var alignmentValues: ({})
    property var uvValues: ({})
    property var newUvValues: ({})

    background: Rectangle {
        color: root.theme.bg
    }

    component Section: Pane {
        id: section
        property Theme theme: root.theme
        property string title: ""
        property string help: ""
        property bool sectionEnabled: true
        default property alias content: body.data

        Layout.fillWidth: true
        padding: 14
        opacity: sectionEnabled ? 1.0 : 0.46

        background: Rectangle {
            color: section.theme.surface
            radius: 8
            border.width: 1
            border.color: section.theme.divider
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 10

            Label {
                text: section.title
                color: section.theme.text
                font.pixelSize: 17
                font.weight: Font.Medium
                Layout.fillWidth: true
            }

            Label {
                visible: section.help.length > 0
                text: section.help
                color: section.theme.subtext
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            Rectangle {
                height: 1
                color: section.theme.divider
                opacity: 0.75
                Layout.fillWidth: true
            }

            ColumnLayout {
                id: body
                Layout.fillWidth: true
                spacing: 10
                enabled: section.sectionEnabled
            }
        }
    }

    component ActionButton: ThemedButton {
        theme: root.theme
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        Layout.preferredWidth: 1
        Layout.preferredHeight: 44
        Layout.minimumHeight: 44
        Layout.maximumHeight: 44
        padding: 10
        font.pixelSize: 13
    }

    component ActionGrid: GridLayout {
        Layout.fillWidth: true
        columns: root.theme.gridColumns(width, 2, 150)
        columnSpacing: 10
        rowSpacing: 10
    }

    component FieldLabel: Label {
        property Theme theme: root.theme
        color: theme.text
        verticalAlignment: Text.AlignVCenter
        Layout.fillWidth: true
    }

    function goBack() {
        if (root.stackView && root.stackView.depth > 1) {
            root.stackView.pop()
            return
        }

        if (StackView.view)
            StackView.view.pop()
    }

    function printStatusText(code) {
        switch (Number(code)) {
        case 0:
            return "Standby";
        case 1:
            return "Printing";
        case 2:
            return "Paused";
        case 3:
            return "Resume";
        case 4:
            return "Canceled";
        case 5:
            return "Error";
        default:
            return "Unknown";
        }
    }

    function cleanStatusText(code) {
        switch (Number(code)) {
        case 0:
            return "Standby";
        case 1:
            return "Auto-cleaning";
        default:
            return "Unknown";
        }
    }

    function readMapValue(map, key, fallback) {
        if (!map || map[key] === undefined)
            return fallback;
        return map[key];
    }

    function updateStatus(silent) {
        if (!root.controlsEnabled)
            return false;
        const result = nocaiDirectPrint.getPrinterStatus();
        root.printerStatus = result;
        if (result.ok) {
            root.statusText = "Print: " + root.printStatusText(result.printStatus) + " | Clean: " + root.cleanStatusText(result.cleanStatus);
            return true;
        }
        root.statusText = nocaiDirectPrint.lastError;
        if (!silent)
            toast.show(root.statusText);
        return false;
    }

    function runAction(label, callback, pollAfter) {
        if (!root.maintenanceSupported) {
            root.statusText = "Maintenance is not supported for " + appState.selectedPrinter + ".";
            toast.show(root.statusText);
            return false;
        }
        const ok = callback();
        root.statusText = ok ? label + " succeeded." : label + " failed: " + nocaiDirectPrint.lastError;
        toast.show(root.statusText);
        if (pollAfter)
            Qt.callLater(function () {
                    if (root.statusPollingEnabled)
                        root.updateStatus(true);
                });
        return ok;
    }

    function refreshJobSettings() {
        const result = nocaiDirectPrint.getJobSettings();
        jobSettings = result;
        runAction("GetJobSettings", function () {
                return result.ok;
            }, false);
    }

    function refreshAlignment() {
        const result = nocaiDirectPrint.getAlignmentValues();
        alignmentValues = result;
        runAction("GetAlignmentValues", function () {
                return result.ok;
            }, false);
    }

    function refreshUv() {
        const result = nocaiDirectPrint.getUVParamValues();
        uvValues = result;
        runAction("GetUVParamValues", function () {
                return result.ok;
            }, false);
    }

    function refreshNewUv() {
        const result = nocaiDirectPrint.getNewUVParamValues();
        newUvValues = result;
        runAction("GetNewUVParamValues", function () {
                return result.ok;
            }, false);
    }

    Timer {
        id: statusPoller
        interval: 1500
        repeat: true
        running: root.visible && root.controlsEnabled && root.statusPollingEnabled
        onTriggered: root.updateStatus(true)
    }

    header: Rectangle {
        height: 60
        color: root.theme.surface

        RowLayout {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 10

            ThemedButton {
                text: "Back"
                theme: root.theme
                Layout.preferredWidth: root.theme.headerButtonWidth(root.width)
                Layout.preferredHeight: 40
                onClicked: root.goBack()
            }

            Label {
                text: "Printer Maintenance"
                color: root.theme.text
                font.pixelSize: root.theme.headerTitleSize(root.width)
                font.weight: Font.Medium
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
            }

            ThemedButton {
                text: "Connect"
                theme: root.theme
                enabled: root.maintenanceSupported
                Layout.preferredWidth: root.theme.headerButtonWidth(root.width)
                Layout.preferredHeight: 40
                onClicked: root.runAction("ConnectPrinter", function () {
                        if (!nocaiDirectPrint.refreshPrinters())
                            return false;
                        if (root.appState.sdkSelectedPrinterIndex >= 0)
                            nocaiDirectPrint.choosePrinter(root.appState.sdkSelectedPrinterIndex);
                        const ok = nocaiDirectPrint.connectPrinter();
                        root.statusPollingEnabled = ok;
                        if (ok)
                            Qt.callLater(root.updateStatus, true);
                        return ok;
                    }, false)
            }
        }
    }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth

        ColumnLayout {
            width: root.theme.boundedWidth(parent.width, 520)
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 12
            anchors.margins: root.theme.pageMargin

            Section {
                title: "Status"
                theme: root.theme
                sectionEnabled: root.maintenanceSupported
                help: "The SDK recommends polling printer status slower than once per second. This page refreshes print and cleaning status every 1.5 seconds while maintenance is available."

                Label {
                    Layout.fillWidth: true
                    text: root.maintenanceSupported ? root.statusText : "Maintenance is unavailable for the selected printer. Select a supported printer type in Printer Setup first."
                    color: root.maintenanceSupported ? root.theme.subtext : root.theme.warning
                    wrapMode: Text.WordWrap
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    Label {
                        Layout.fillWidth: true
                        color: root.theme.text
                        text: "Print: " + root.printStatusText(root.readMapValue(root.printerStatus, "printStatus", -1))
                    }

                    Label {
                        Layout.fillWidth: true
                        color: root.theme.text
                        text: "Clean: " + root.cleanStatusText(root.readMapValue(root.printerStatus, "cleanStatus", -1))
                    }
                }

                ActionButton {
                    text: "Refresh Status"
                    onClicked: {
                        root.statusPollingEnabled = true;
                        root.updateStatus(false);
                    }
                }
            }

            Section {
                title: "Head Maintenance"
                theme: root.theme
                sectionEnabled: root.controlsEnabled
                help: "Head mask is a bitmask: bit 0 selects head 1 and bit 1 selects head 2, so 3 selects both X-33 heads. Nozzle Check prints the SDK's diagnostic pattern. Automatic Head Cleaning runs the printer's cleaning cycle. Flash Spray rapidly fires the selected nozzles at the maintenance station to keep them wet or clear light drying; stop it when the refresh is complete."

                GridLayout {
                    Layout.fillWidth: true
                    columns: root.theme.gridColumns(width, 2, 150)
                    columnSpacing: 12
                    rowSpacing: 8

                    FieldLabel {
                        text: "Head Mask"
                        theme: root.theme
                    }
                    SpinBox {
                        id: maintenancePrintHeightSpin
                        Layout.fillWidth: true
                        from: 0
                        to: 65535
                        value: root.headMask
                        onValueModified: root.headMask = value
                    }
                }

                ActionGrid {
                    ActionButton {
                        text: "Print Nozzle Check"
                        theme: root.theme
                        onClicked: root.runAction("PrintNozzleCheck", function () {
                                return nocaiDirectPrint.printNozzleCheck();
                            }, true)
                    }
                    ActionButton {
                        text: "Automatic Head Cleaning"
                        theme: root.theme
                        onClicked: root.runAction("StartCleanOperation", function () {
                                return nocaiDirectPrint.startCleanOperation(root.headMask);
                            }, true)
                    }
                    ActionButton {
                        text: "Wipe Heads"
                        theme: root.theme
                        onClicked: root.runAction("WipePrintHead", function () {
                                return nocaiDirectPrint.wipePrintHead(root.headMask);
                            }, true)
                    }
                    ActionButton {
                        text: "Start Pump"
                        theme: root.theme
                        onClicked: root.runAction("StartPump", function () {
                                return nocaiDirectPrint.startPump(root.headMask);
                            }, true)
                    }
                    ActionButton {
                        text: "Stop Pump"
                        theme: root.theme
                        onClicked: root.runAction("StopPumpOperation", function () {
                                return nocaiDirectPrint.stopPumpOperation();
                            }, true)
                    }
                    ActionButton {
                        text: "Start Flash Spray"
                        theme: root.theme
                        onClicked: root.runAction("StartFlashSpray", function () {
                                return nocaiDirectPrint.spitPrintHead(root.headMask);
                            }, true)
                    }
                    ActionButton {
                        text: "Stop Flash Spray"
                        theme: root.theme
                        onClicked: root.runAction("StopFlashSpray", function () {
                                return nocaiDirectPrint.stopSpitOperation();
                            }, true)
                    }
                    ActionButton {
                        text: "Cap Head"
                        theme: root.theme
                        onClicked: root.runAction("CapPrintHead", function () {
                                return nocaiDirectPrint.capPrintHead();
                            }, true)
                    }
                }
            }

            Section {
                title: "Motion And Height"
                theme: root.theme
                sectionEnabled: root.controlsEnabled
                help: "Axis 0 is X, 1 is Y, and 2 is Z. Direction 0 moves positive; direction 1 moves negative. The SDK reports saved/stop positions in millimeters where supported."

                GridLayout {
                    Layout.fillWidth: true
                    columns: root.theme.gridColumns(width, 2, 150)
                    columnSpacing: 12
                    rowSpacing: 8

                    FieldLabel {
                        text: "Axis"
                        theme: root.theme
                    }
                    SpinBox {
                        Layout.fillWidth: true
                        from: 0
                        to: 2
                        value: root.axis
                        onValueModified: root.axis = value
                    }
                    FieldLabel {
                        text: "Direction"
                        theme: root.theme
                    }
                    SpinBox {
                        Layout.fillWidth: true
                        from: 0
                        to: 1
                        value: root.axisDirection
                        onValueModified: root.axisDirection = value
                    }
                    FieldLabel {
                        text: "Print Height mm"
                        theme: root.theme
                    }
                    SpinBox {
                        Layout.fillWidth: true
                        from: 0
                        to: 1520
                        stepSize: 1
                        editable: true
                        value: Math.round(root.printHeight * 10)
                        textFromValue: function(value, locale) {
                            return Number(value / 10.0).toLocaleString(locale, 'f', 1)
                        }
                        valueFromText: function(text, locale) {
                            return Math.round(Number.fromLocaleString(locale, text) * 10.0)
                        }
                        validator: DoubleValidator {
                            bottom: 0.0
                            top: 152.0
                            decimals: 1
                            notation: DoubleValidator.StandardNotation
                        }
                        contentItem: TextInput {
                            z: 2
                            text: maintenancePrintHeightSpin.displayText
                            color: root.theme.text
                            selectionColor: root.theme.accent
                            selectedTextColor: root.theme.bg
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            selectByMouse: true
                            validator: maintenancePrintHeightSpin.validator
                            inputMethodHints: Qt.ImhFormattedNumbersOnly
                        }
                        onValueModified: root.printHeight = value / 10.0
                    }
                }

                ActionGrid {
                    ActionButton {
                        text: "Move Axis"
                        theme: root.theme
                        onClicked: root.runAction("MoveAxis", function () {
                                return nocaiDirectPrint.moveAxis(root.axis, root.axisDirection);
                            }, true)
                    }
                    ActionButton {
                        text: "Stop Axis"
                        theme: root.theme
                        onClicked: root.runAction("StopAxis", function () {
                                const r = nocaiDirectPrint.stopAxis(root.axis);
                                if (r.ok)
                                    root.statusText = "StopAxis position: " + r.position + " mm";
                                return r.ok;
                            }, true)
                    }
                    ActionButton {
                        text: "Save Axis Position"
                        theme: root.theme
                        onClicked: root.runAction("SaveAxisPos", function () {
                                const r = nocaiDirectPrint.saveAxisPos(root.axis);
                                if (r.ok)
                                    root.statusText = "Saved position: " + r.position + " mm";
                                return r.ok;
                            }, true)
                    }
                    ActionButton {
                        text: "Set Height"
                        theme: root.theme
                        onClicked: root.runAction("SetPrintHeight", function () {
                                return nocaiDirectPrint.setPrintHeight(root.printHeight);
                            }, true)
                    }
                    ActionButton {
                        text: "Get Height"
                        theme: root.theme
                        onClicked: root.runAction("GetPrintHeight", function () {
                                const r = nocaiDirectPrint.getPrintHeight();
                                root.printHeight = r.heightMm;
                                if (r.ok)
                                    root.statusText = "Print height: " + r.heightMm + " mm";
                                return r.ok;
                            }, false)
                    }
                }
            }

            Section {
                title: "Settings And Config"
                theme: root.theme
                sectionEnabled: root.controlsEnabled
                help: "Read the engine's current job settings before applying changes. Import/export uses the vendor PFG configuration file format."

                ActionGrid {
                    ActionButton {
                        text: "Read Job Settings"
                        theme: root.theme
                        onClicked: root.refreshJobSettings()
                    }
                    ActionButton {
                        text: "Apply Read Settings"
                        theme: root.theme
                        onClicked: root.runAction("SetJobSettings", function () {
                                return nocaiDirectPrint.setJobSettingsFromMap(root.jobSettings);
                            }, true)
                    }
                }

                Label {
                    Layout.fillWidth: true
                    text: "Direction " + root.readMapValue(root.jobSettings, "printDirection", "-") + " | Speed " + root.readMapValue(root.jobSettings, "printSpeed", "-") + " | Head Voltage " + root.readMapValue(root.jobSettings, "headVoltage", "-")
                    color: root.theme.subtext
                    wrapMode: Text.WordWrap
                }

                ActionGrid {
                    ActionButton {
                        text: "Export Config"
                        theme: root.theme
                        onClicked: exportConfigDialog.open()
                    }
                    ActionButton {
                        text: "Import Config"
                        theme: root.theme
                        onClicked: importConfigDialog.open()
                    }
                }
            }

            Section {
                title: "Alignment"
                theme: root.theme
                sectionEnabled: root.controlsEnabled
                help: "Value type selects which alignment field to write. Pattern type selects a printer-generated calibration chart. Use the dedicated Head Maintenance button for the common nozzle check; this section exposes all pattern types for advanced alignment work."

                GridLayout {
                    Layout.fillWidth: true
                    columns: root.theme.gridColumns(width, 2, 150)
                    columnSpacing: 12
                    rowSpacing: 8

                    FieldLabel {
                        text: "Value Type"
                        theme: root.theme
                    }
                    SpinBox {
                        Layout.fillWidth: true
                        from: 0
                        to: 5
                        value: root.alignmentType
                        onValueModified: root.alignmentType = value
                    }
                    FieldLabel {
                        text: "Pattern Type"
                        theme: root.theme
                    }
                    SpinBox {
                        Layout.fillWidth: true
                        from: 0
                        to: 22
                        value: root.alignmentPatternType
                        onValueModified: root.alignmentPatternType = value
                    }
                }

                ActionGrid {
                    ActionButton {
                        text: "Read Alignment"
                        theme: root.theme
                        onClicked: root.refreshAlignment()
                    }
                    ActionButton {
                        text: "Apply Alignment"
                        theme: root.theme
                        onClicked: root.runAction("SetAlignmentValues", function () {
                                return nocaiDirectPrint.setAlignmentValues(root.alignmentValues, root.alignmentType);
                            }, true)
                    }
                }

                ActionButton {
                    text: "Print Alignment Pattern"
                    theme: root.theme
                    onClicked: root.runAction("PrintAlignmentPattern", function () {
                            return nocaiDirectPrint.printAlignmentPattern(root.alignmentPatternType);
                        }, true)
                }

                Label {
                    Layout.fillWidth: true
                    text: "Step " + root.readMapValue(root.alignmentValues, "stepValue", "-") + " | Bidi " + root.readMapValue(root.alignmentValues, "bidiValue", "-")
                    color: root.theme.subtext
                    wrapMode: Text.WordWrap
                }
            }

            Section {
                title: "XY And UV"
                theme: root.theme
                sectionEnabled: root.controlsEnabled
                help: "XY offsets are in millimeters. UV value types map to the right/left lamp directional offsets documented by the SDK. New UV controls are only active on firmware that reports support."

                GridLayout {
                    Layout.fillWidth: true
                    columns: root.theme.gridColumns(width, 2, 150)
                    columnSpacing: 12
                    rowSpacing: 8

                    FieldLabel {
                        text: "X Offset mm"
                        theme: root.theme
                    }
                    SpinBox {
                        Layout.fillWidth: true
                        from: 0
                        to: 65535
                        value: root.printX
                        onValueModified: root.printX = value
                    }
                    FieldLabel {
                        text: "Y Offset mm"
                        theme: root.theme
                    }
                    SpinBox {
                        Layout.fillWidth: true
                        from: 0
                        to: 65535
                        value: root.printY
                        onValueModified: root.printY = value
                    }
                    FieldLabel {
                        text: "UV Value Type"
                        theme: root.theme
                    }
                    SpinBox {
                        Layout.fillWidth: true
                        from: 0
                        to: 4
                        value: root.uvType
                        onValueModified: root.uvType = value
                    }
                    FieldLabel {
                        text: "New UV Value Type"
                        theme: root.theme
                    }
                    SpinBox {
                        Layout.fillWidth: true
                        from: 0
                        to: 6
                        value: root.newUvType
                        onValueModified: root.newUvType = value
                    }
                    FieldLabel {
                        text: "New UV Function"
                        theme: root.theme
                    }
                    SpinBox {
                        Layout.fillWidth: true
                        from: 0
                        to: 8
                        value: root.newUvFunctionType
                        onValueModified: root.newUvFunctionType = value
                    }
                }

                ActionGrid {
                    ActionButton {
                        text: "Set XY"
                        theme: root.theme
                        onClicked: root.runAction("SetPrintXYValue", function () {
                                return nocaiDirectPrint.setPrintXYValue(root.printX, root.printY);
                            }, true)
                    }
                    ActionButton {
                        text: "Get XY"
                        theme: root.theme
                        onClicked: root.runAction("GetPrintXYValue", function () {
                                const r = nocaiDirectPrint.getPrintXYValue();
                                root.printX = r.xMm;
                                root.printY = r.yMm;
                                if (r.ok)
                                    root.statusText = "XY: " + r.xMm + ", " + r.yMm + " mm";
                                return r.ok;
                            }, false)
                    }
                    ActionButton {
                        text: "Read UV"
                        theme: root.theme
                        onClicked: root.refreshUv()
                    }
                    ActionButton {
                        text: "Apply UV"
                        theme: root.theme
                        onClicked: root.runAction("SetUVParamValues", function () {
                                return nocaiDirectPrint.setUVParamValues(root.uvValues, root.uvType);
                            }, true)
                    }
                    ActionButton {
                        text: "New UV Support"
                        theme: root.theme
                        onClicked: root.runAction("GetSupportNewUVParamFunction", function () {
                                const support = nocaiDirectPrint.getSupportNewUVParamFunction();
                                root.statusText = "New UV support: " + support;
                                return support >= 0;
                            }, false)
                    }
                    ActionButton {
                        text: "Run New UV Function"
                        theme: root.theme
                        onClicked: root.runAction("SetNewUVParamFunction", function () {
                                return nocaiDirectPrint.setNewUVParamFunction(root.newUvFunctionType);
                            }, true)
                    }
                    ActionButton {
                        text: "Read New UV"
                        theme: root.theme
                        onClicked: root.refreshNewUv()
                    }
                    ActionButton {
                        text: "Apply New UV"
                        theme: root.theme
                        onClicked: root.runAction("SetNewUVParamValues", function () {
                                return nocaiDirectPrint.setNewUVParamValues(root.newUvValues, root.newUvType);
                            }, true)
                    }
                }
            }

            Item {
                height: 8
            }
        }
    }

    P.FileDialog {
        id: exportConfigDialog
        title: "Export Nocai Config"
        fileMode: P.FileDialog.SaveFile
        defaultSuffix: "pfg"
        nameFilters: ["Printer Config (*.pfg)", "All Files (*)"]
        onAccepted: root.runAction("ExportConfigFile", function () {
                return nocaiDirectPrint.exportConfigFile(file);
            }, true)
    }

    P.FileDialog {
        id: importConfigDialog
        title: "Import Nocai Config"
        fileMode: P.FileDialog.OpenFile
        nameFilters: ["Printer Config (*.pfg)", "All Files (*)"]
        onAccepted: root.runAction("ImportConfigFile", function () {
                return nocaiDirectPrint.importConfigFile(file);
            }, true)
    }

    Toast {
        id: toast
        parent: Overlay.overlay
    }
}
