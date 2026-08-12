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
    property string pendingMaintenanceLabel: ""
    property bool pendingMaintenancePollAfter: false
    property bool pendingMaintenanceShowOverlay: false
    property bool pendingMaintenanceShowToast: true
    property var pendingMaintenanceCompletion: null

    property int headMask: 3
    property int activeMotionAxis: 0
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
        enabled: root.controlsEnabled && !nocaiDirectPrint.maintenanceBusy
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
        if (!root.maintenanceSupported || !nocaiDirectPrint.available
                || nocaiDirectPrint.maintenanceBusy)
            return false;
        return root.runAsyncAction(
            "Refresh Printer Status", "GetPrinterStatus", {}, false,
            function (result, ok) {
                root.printerStatus = result || ({});
                if (ok) {
                    root.statusText = "Print: "
                        + root.printStatusText(result.printStatus)
                        + " | Clean: "
                        + root.cleanStatusText(result.cleanStatus);
                }
            }, !silent, !silent);
    }

    function runAsyncAction(label, action, arguments, pollAfter, completion,
                            showOverlay, showToast) {
        if (!root.maintenanceSupported) {
            root.statusText = "Maintenance is not supported for " + appState.selectedPrinter + ".";
            toast.show(root.statusText);
            return false;
        }
        if (nocaiDirectPrint.maintenanceBusy) {
            root.statusText = "Another printer maintenance operation is still running.";
            toast.show(root.statusText);
            return false;
        }
        root.pendingMaintenanceLabel = label;
        root.pendingMaintenancePollAfter = pollAfter;
        root.pendingMaintenanceShowOverlay = showOverlay === undefined
            ? true : Boolean(showOverlay);
        root.pendingMaintenanceShowToast = showToast === undefined
            ? true : Boolean(showToast);
        root.pendingMaintenanceCompletion = completion;
        if (!nocaiDirectPrint.startMaintenanceAction(action, arguments || ({}))) {
            root.statusText = label + " could not start: " + nocaiDirectPrint.lastError;
            root.pendingMaintenanceLabel = "";
            root.pendingMaintenancePollAfter = false;
            root.pendingMaintenanceShowOverlay = false;
            root.pendingMaintenanceShowToast = true;
            root.pendingMaintenanceCompletion = null;
            toast.show(root.statusText);
            return false;
        }
        root.statusText = label + " is running…";
        return true;
    }

    function movePrinterAxis(axis, direction, label) {
        root.activeMotionAxis = axis;
        return root.runAsyncAction(
            label, "MoveAxis", {"axis": axis, "direction": direction},
            false, null);
    }

    function stopPrinterAxis(axis, label) {
        root.activeMotionAxis = axis;
        return root.runAsyncAction(
            label, "StopAxis", {"axis": axis}, true,
            function (result, ok) {
                if (ok)
                    root.statusText = label + " at " + result.position + " mm.";
            });
    }

    function savePrinterAxis(axis, label) {
        root.activeMotionAxis = axis;
        return root.runAsyncAction(
            label, "SaveAxisPos", {"axis": axis}, true,
            function (result, ok) {
                if (ok)
                    root.statusText = label + ": " + result.position + " mm.";
            });
    }

    function maintenanceProgressText() {
        switch (root.pendingMaintenanceLabel) {
        case "Refresh Printer Status": return "Refreshing Printer Status…";
        case "ConnectPrinter": return "Connecting to Printer…";
        case "PrintNozzleCheck": return "Printing Nozzle Check…";
        case "StartCleanOperation": return "Cleaning Print Heads…";
        case "WipePrintHead": return "Wiping Print Heads…";
        case "StartPump": return "Starting Pump…";
        case "StopPumpOperation": return "Stopping Pump…";
        case "StartFlashSpray": return "Starting Flash Spray…";
        case "StopFlashSpray": return "Stopping Flash Spray…";
        case "CapPrintHead": return "Capping Print Head…";
        case "SetPrintHeight": return "Moving to Print Height…";
        case "GetPrintHeight": return "Reading Print Height…";
        }
        return root.pendingMaintenanceLabel.length > 0
            ? root.pendingMaintenanceLabel + "…"
            : "Working with Printer…";
    }

    function refreshJobSettings() {
        runAsyncAction("GetJobSettings", "GetJobSettings", {}, false,
            function (result, ok) {
                if (ok)
                    root.jobSettings = result;
            });
    }

    function refreshAlignment() {
        runAsyncAction("GetAlignmentValues", "GetAlignmentValues", {}, false,
            function (result, ok) {
                if (ok)
                    root.alignmentValues = result;
            });
    }

    function refreshUv() {
        runAsyncAction("GetUVParamValues", "GetUVParamValues", {}, false,
            function (result, ok) {
                if (ok)
                    root.uvValues = result;
            });
    }

    function refreshNewUv() {
        runAsyncAction("GetNewUVParamValues", "GetNewUVParamValues", {}, false,
            function (result, ok) {
                if (ok)
                    root.newUvValues = result;
            });
    }

    Connections {
        target: nocaiDirectPrint
        function onMaintenanceActionFinished(action, succeeded, result, errorMessage) {
            const label = root.pendingMaintenanceLabel.length > 0
                ? root.pendingMaintenanceLabel : action;
            root.statusText = succeeded
                ? label + " succeeded."
                : label + " failed: " + errorMessage;
            const completion = root.pendingMaintenanceCompletion;
            const pollAfter = root.pendingMaintenancePollAfter;
            const showToast = root.pendingMaintenanceShowToast;
            root.pendingMaintenanceLabel = "";
            root.pendingMaintenancePollAfter = false;
            root.pendingMaintenanceShowOverlay = false;
            root.pendingMaintenanceShowToast = true;
            root.pendingMaintenanceCompletion = null;
            if (completion)
                completion(result, succeeded);
            if (showToast)
                toast.show(root.statusText);
            if (pollAfter)
                Qt.callLater(function () {
                        if (root.statusPollingEnabled && !nocaiDirectPrint.maintenanceBusy)
                            root.updateStatus(true);
                    });
        }
    }

    Timer {
        id: statusPoller
        interval: 1500
        repeat: true
        running: root.visible && root.controlsEnabled && root.statusPollingEnabled
        onTriggered: root.updateStatus(true)
    }

    Component.onCompleted: {
        root.statusPollingEnabled = true;
        Qt.callLater(function () { root.updateStatus(true); });
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
                Layout.preferredWidth: root.theme.headerButtonWidth(root.width)
                Layout.preferredHeight: 40
                enabled: root.maintenanceSupported && !nocaiDirectPrint.maintenanceBusy
                onClicked: root.runAsyncAction(
                    "ConnectPrinter", "ConnectPrinter",
                    {"printerIndex": root.appState.sdkSelectedPrinterIndex}, true,
                    function (result, ok) {
                        root.statusPollingEnabled = ok;
                    })
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
                        id: maintenanceHeadMaskSpin
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
                        onClicked: root.runAsyncAction(
                            "PrintNozzleCheck", "PrintNozzleCheck", {}, true, null)
                    }
                    ActionButton {
                        text: "Automatic Head Cleaning"
                        theme: root.theme
                        onClicked: root.runAsyncAction(
                            "StartCleanOperation", "StartCleanOperation",
                            {"headMask": root.headMask}, true, null)
                    }
                    ActionButton {
                        text: "Wipe Heads"
                        theme: root.theme
                        onClicked: root.runAsyncAction(
                            "WipePrintHead", "WipePrintHead",
                            {"headMask": root.headMask}, true, null)
                    }
                    ActionButton {
                        text: "Cap Head"
                        theme: root.theme
                        onClicked: root.runAsyncAction(
                            "CapPrintHead", "CapPrintHead", {}, true, null)
                    }
                    ActionButton {
                        text: "Start Pump"
                        theme: root.theme
                        onClicked: root.runAsyncAction(
                            "StartPump", "StartPump",
                            {"headMask": root.headMask}, true, null)
                    }
                    ActionButton {
                        text: "Stop Pump"
                        theme: root.theme
                        onClicked: root.runAsyncAction(
                            "StopPumpOperation", "StopPumpOperation", {}, true, null)
                    }
                    ActionButton {
                        text: "Start Flash Spray"
                        theme: root.theme
                        onClicked: root.runAsyncAction(
                            "StartFlashSpray", "StartFlashSpray",
                            {"headMask": root.headMask}, true, null)
                    }
                    ActionButton {
                        text: "Stop Flash Spray"
                        theme: root.theme
                        onClicked: root.runAsyncAction(
                            "StopFlashSpray", "StopFlashSpray", {}, true, null)
                    }
                }
            }

            Section {
                title: "Motion And Height"
                theme: root.theme
                sectionEnabled: root.controlsEnabled
                help: "Use the arrows to move the print head left or right and the bed forward or back. Motion continues until Stop is pressed or the printer reaches its limit. Print height is in millimeters and accepts two decimal places."

                Label {
                    text: "Head and Bed Motion"
                    color: root.theme.text
                    font.weight: Font.Medium
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: 3
                    columnSpacing: 10
                    rowSpacing: 10

                    Item {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                    }
                    ActionButton {
                        text: "↑ Forward"
                        Accessible.name: "Move bed forward"
                        onClicked: root.movePrinterAxis(1, 0, "Move Bed Forward")
                    }
                    Item {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                    }

                    ActionButton {
                        text: "← Head"
                        Accessible.name: "Move print head left"
                        onClicked: root.movePrinterAxis(0, 0, "Move Head Left")
                    }
                    ActionButton {
                        text: "■ Stop"
                        Accessible.name: "Stop head or bed motion"
                        onClicked: root.stopPrinterAxis(
                            root.activeMotionAxis,
                            root.activeMotionAxis === 0
                                ? "Stop Head Motion"
                                : (root.activeMotionAxis === 1
                                   ? "Stop Bed Motion"
                                   : "Stop Height Motion"))
                    }
                    ActionButton {
                        text: "Head →"
                        Accessible.name: "Move print head right"
                        onClicked: root.movePrinterAxis(0, 1, "Move Head Right")
                    }

                    Item {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                    }
                    ActionButton {
                        text: "↓ Back"
                        Accessible.name: "Move bed back toward the user"
                        onClicked: root.movePrinterAxis(1, 1, "Move Bed Back")
                    }
                    Item {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                    }
                }

                ActionGrid {
                    ActionButton {
                        text: "Save Head Position"
                        onClicked: root.savePrinterAxis(0, "Saved Head Position")
                    }
                    ActionButton {
                        text: "Save Bed Position"
                        onClicked: root.savePrinterAxis(1, "Saved Bed Position")
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: root.theme.divider
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: root.theme.gridColumns(width, 2, 150)
                    columnSpacing: 12
                    rowSpacing: 8

                    FieldLabel {
                        text: "Print Height mm"
                        theme: root.theme
                    }
                    SpinBox {
                        id: maintenancePrintHeightSpin
                        Layout.fillWidth: true
                        from: 0
                        to: 15200
                        stepSize: 1
                        editable: true
                        value: Math.round(root.printHeight * 100)
                        textFromValue: function(value, locale) {
                            return Number(value / 100.0).toLocaleString(locale, 'f', 2)
                        }
                        valueFromText: function(text, locale) {
                            return Math.round(Number.fromLocaleString(locale, text) * 100.0)
                        }
                        validator: DoubleValidator {
                            bottom: 0.0
                            top: 152.0
                            decimals: 2
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
                        onValueModified: root.printHeight = value / 100.0
                    }
                }

                ActionGrid {
                    ActionButton {
                        text: "Set Height"
                        theme: root.theme
                        onClicked: {
                            root.activeMotionAxis = 2;
                            root.runAsyncAction(
                                "SetPrintHeight", "SetPrintHeight",
                                {"heightMm": root.printHeight}, true, null);
                        }
                    }
                    ActionButton {
                        text: "Get Height"
                        theme: root.theme
                        onClicked: root.runAsyncAction(
                            "GetPrintHeight", "GetPrintHeight", {}, false,
                            function (result, ok) {
                                if (ok) {
                                    root.printHeight = result.heightMm;
                                    root.statusText = "Print height: " + result.heightMm + " mm";
                                }
                            })
                    }
                    ActionButton {
                        text: "Stop Height"
                        onClicked: root.stopPrinterAxis(2, "Stop Height Motion")
                    }
                    ActionButton {
                        text: "Save Height Position"
                        onClicked: root.savePrinterAxis(2, "Saved Height Position")
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
                        onClicked: root.runAsyncAction(
                            "SetJobSettings", "SetJobSettings",
                            {"settings": root.jobSettings}, true, null)
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
                        onClicked: root.runAsyncAction(
                            "SetAlignmentValues", "SetAlignmentValues",
                            {"settings": root.alignmentValues, "type": root.alignmentType},
                            true, null)
                    }
                }

                ActionButton {
                    text: "Print Alignment Pattern"
                    theme: root.theme
                    onClicked: root.runAsyncAction(
                        "PrintAlignmentPattern", "PrintAlignmentPattern",
                        {"type": root.alignmentPatternType}, true, null)
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
                        onClicked: root.runAsyncAction(
                            "SetPrintXYValue", "SetPrintXYValue",
                            {"xMm": root.printX, "yMm": root.printY}, true, null)
                    }
                    ActionButton {
                        text: "Get XY"
                        theme: root.theme
                        onClicked: root.runAsyncAction(
                            "GetPrintXYValue", "GetPrintXYValue", {}, false,
                            function (result, ok) {
                                if (ok) {
                                    root.printX = result.xMm;
                                    root.printY = result.yMm;
                                    root.statusText = "XY: " + result.xMm + ", " + result.yMm + " mm";
                                }
                            })
                    }
                    ActionButton {
                        text: "Read UV"
                        theme: root.theme
                        onClicked: root.refreshUv()
                    }
                    ActionButton {
                        text: "Apply UV"
                        theme: root.theme
                        onClicked: root.runAsyncAction(
                            "SetUVParamValues", "SetUVParamValues",
                            {"settings": root.uvValues, "type": root.uvType}, true, null)
                    }
                    ActionButton {
                        text: "New UV Support"
                        theme: root.theme
                        onClicked: root.runAsyncAction(
                            "GetSupportNewUVParamFunction", "GetSupportNewUVParamFunction",
                            {}, false, function (result, ok) {
                                if (ok)
                                    root.statusText = "New UV support: " + result;
                            })
                    }
                    ActionButton {
                        text: "Run New UV Function"
                        theme: root.theme
                        onClicked: root.runAsyncAction(
                            "SetNewUVParamFunction", "SetNewUVParamFunction",
                            {"type": root.newUvFunctionType}, true, null)
                    }
                    ActionButton {
                        text: "Read New UV"
                        theme: root.theme
                        onClicked: root.refreshNewUv()
                    }
                    ActionButton {
                        text: "Apply New UV"
                        theme: root.theme
                        onClicked: root.runAsyncAction(
                            "SetNewUVParamValues", "SetNewUVParamValues",
                            {"settings": root.newUvValues, "type": root.newUvType},
                            true, null)
                    }
                }
            }

            Item {
                height: 8
            }
        }
    }

    Item {
        id: maintenanceBusyOverlay
        parent: Overlay.overlay
        anchors.fill: parent
        visible: root.visible
                 && nocaiDirectPrint.maintenanceBusy
                 && root.pendingMaintenanceShowOverlay
        z: 1000

        Rectangle {
            anchors.fill: parent
            color: "#00000088"
        }

        MouseArea {
            anchors.fill: parent
        }

        Rectangle {
            anchors.centerIn: parent
            width: Math.min(360, parent.width - 40)
            height: 150
            radius: 10
            color: root.theme.surface
            border.width: 1
            border.color: root.theme.divider

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 20
                spacing: 12

                BusyIndicator {
                    running: maintenanceBusyOverlay.visible
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: 56
                    Layout.preferredHeight: 56
                }

                Label {
                    text: root.maintenanceProgressText()
                    color: root.theme.text
                    font.pixelSize: 17
                    font.weight: Font.Medium
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }
        }
    }

    P.FileDialog {
        id: exportConfigDialog
        title: "Export Nocai Config"
        fileMode: P.FileDialog.SaveFile
        defaultSuffix: "pfg"
        nameFilters: ["Printer Config (*.pfg)", "All Files (*)"]
        onAccepted: root.runAsyncAction(
            "ExportConfigFile", "ExportConfigFile", {"path": file}, true, null)
    }

    P.FileDialog {
        id: importConfigDialog
        title: "Import Nocai Config"
        fileMode: P.FileDialog.OpenFile
        nameFilters: ["Printer Config (*.pfg)", "All Files (*)"]
        onAccepted: root.runAsyncAction(
            "ImportConfigFile", "ImportConfigFile", {"path": file}, true, null)
    }

    Toast {
        id: toast
        parent: Overlay.overlay
    }
}
