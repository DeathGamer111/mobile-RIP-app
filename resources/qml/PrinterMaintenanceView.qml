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
    property int activeMotionDirection: 0
    property bool axisMotionActive: false
    property bool axisMotionHeld: false
    property bool axisStopRequested: false
    property bool axisHomeRequested: false
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
    property string lastPrinterStatusSignature: ""
    readonly property bool userScrolling: maintenanceScroll.contentItem
                                          && (maintenanceScroll.contentItem.dragging
                                              || maintenanceScroll.contentItem.flicking
                                              || maintenanceScroll.contentItem.moving)

    background: Rectangle {
        color: root.theme.bg
    }

    component Section: Pane {
        id: section
        property Theme theme: root.theme
        property string title: ""
        property string help: ""
        property bool sectionEnabled: true
        property bool expanded: true
        property bool helpExpanded: false
        default property alias content: body.data

        Layout.fillWidth: true
        padding: section.theme.panePadding
        opacity: sectionEnabled ? 1.0 : 0.46
        implicitHeight: sectionLayout.implicitHeight + topPadding + bottomPadding

        background: Rectangle {
            color: section.theme.surface
            radius: section.theme.cardRadius
            border.width: 1
            border.color: section.theme.divider
        }

        ColumnLayout {
            id: sectionLayout
            anchors.fill: parent
            spacing: section.theme.spaceSm

            RowLayout {
                Layout.fillWidth: true

                Label {
                    text: section.title
                    color: section.theme.text
                    font.pixelSize: section.theme.sectionTitleSize
                    font.weight: Font.DemiBold
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }

                ToolButton {
                    visible: section.help.length > 0
                    text: "ⓘ"
                    flat: true
                    font.pixelSize: 16
                    Accessible.name: section.title
                    onClicked: section.helpExpanded = !section.helpExpanded
                }

                ToolButton {
                    text: section.expanded ? "−" : "+"
                    flat: true
                    font.pixelSize: 18
                    Accessible.name: section.title
                    onClicked: section.expanded = !section.expanded
                }
            }

            Label {
                visible: section.expanded && section.help.length > 0
                         && (!section.theme.mobile || section.helpExpanded)
                text: section.help
                color: section.theme.subtext
                font.pixelSize: section.theme.bodyTextSize
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            Rectangle {
                visible: section.expanded
                height: 1
                color: section.theme.divider
                opacity: 0.75
                Layout.fillWidth: true
            }

            ColumnLayout {
                id: body
                visible: section.expanded
                Layout.fillWidth: true
                spacing: section.theme.spaceSm
                enabled: section.sectionEnabled
            }
        }
    }

    component ActionButton: ThemedButton {
        theme: root.theme
        enabled: root.controlsEnabled && !root.axisMotionActive
                 && !nocaiDirectPrint.maintenanceBusy
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        Layout.preferredWidth: 1
        Layout.preferredHeight: 44
        Layout.minimumHeight: 44
        Layout.maximumHeight: 44
        padding: 10
        font.pixelSize: 13
    }

    component MotionButton: ThemedButton {
        property int motionAxis: 0
        property int motionDirection: 0
        property string motionLabel: ""

        theme: root.theme
        enabled: root.controlsEnabled
                 && (!root.axisMotionActive
                     || (root.activeMotionAxis === motionAxis
                         && root.activeMotionDirection === motionDirection))
                 && (!nocaiDirectPrint.maintenanceBusy
                     || (root.axisMotionActive
                         && root.activeMotionAxis === motionAxis
                         && root.activeMotionDirection === motionDirection))
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        Layout.preferredWidth: 1
        Layout.preferredHeight: 44
        Layout.minimumHeight: 44
        Layout.maximumHeight: 44
        padding: 10
        font.pixelSize: 13
        autoRepeat: false

        onPressed: root.beginAxisMotion(motionAxis, motionDirection, motionLabel)
        onReleased: root.releaseAxisMotion(motionAxis, motionDirection)
        onCanceled: root.releaseAxisMotion(motionAxis, motionDirection)
    }

    component ActionGrid: GridLayout {
        Layout.fillWidth: true
        columns: root.theme.actionColumns(width, 2, 150)
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
            return strings.trKey("printerMaintenance.status.standby");
        case 1:
            return strings.trKey("printerMaintenance.status.printing");
        case 2:
            return strings.trKey("printerMaintenance.status.paused");
        case 3:
            return strings.trKey("printerMaintenance.status.resume");
        case 4:
            return strings.trKey("printerMaintenance.status.canceled");
        case 5:
            return strings.trKey("printerMaintenance.status.error");
        default:
            return strings.trKey("printerMaintenance.status.unknown");
        }
    }

    function cleanStatusText(code) {
        switch (Number(code)) {
        case 0:
            return strings.trKey("printerMaintenance.status.standby");
        case 1:
            return strings.trKey("printerMaintenance.status.autoCleaning");
        default:
            return strings.trKey("printerMaintenance.status.unknown");
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
            strings.trKey("printerMaintenance.action.refreshStatus"), "GetPrinterStatus", {}, false,
            function (result, ok) {
                if (ok) {
                    const signature = String(result.printStatus) + ":" + String(result.cleanStatus);
                    if (signature !== root.lastPrinterStatusSignature) {
                        root.lastPrinterStatusSignature = signature;
                        root.printerStatus = result || ({});
                    }
                    root.statusText = strings.trKey("printerMaintenance.status.printPrefix")
                        + root.printStatusText(result.printStatus)
                        + strings.trKey("printerMaintenance.status.cleanInfix")
                        + root.cleanStatusText(result.cleanStatus);
                }
            }, !silent, !silent);
    }

    function runAsyncAction(label, action, arguments, pollAfter, completion,
                            showOverlay, showToast) {
        if (!root.maintenanceSupported) {
            root.statusText = strings.trKey("printerMaintenance.toast.unsupportedPrefix")
                              + appState.selectedPrinter + ".";
            toast.show(root.statusText);
            return false;
        }
        if (nocaiDirectPrint.maintenanceBusy) {
            root.statusText = strings.trKey("printerMaintenance.toast.busy");
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
            root.statusText = label + strings.trKey("printerMaintenance.toast.couldNotStart")
                              + nocaiDirectPrint.lastError;
            root.pendingMaintenanceLabel = "";
            root.pendingMaintenancePollAfter = false;
            root.pendingMaintenanceShowOverlay = false;
            root.pendingMaintenanceShowToast = true;
            root.pendingMaintenanceCompletion = null;
            toast.show(root.statusText);
            return false;
        }
        root.statusText = label + strings.trKey("printerMaintenance.toast.runningSuffix");
        return true;
    }

    function clearAxisMotionState() {
        root.axisMotionActive = false;
        root.axisMotionHeld = false;
        root.axisStopRequested = false;
        root.axisHomeRequested = false;
        motionSafetyTimer.stop();
    }

    function beginAxisMotion(axis, direction, label) {
        if (root.axisMotionActive || nocaiDirectPrint.maintenanceBusy)
            return false;

        root.activeMotionAxis = axis;
        root.activeMotionDirection = direction;
        root.axisMotionActive = true;
        root.axisMotionHeld = true;
        root.axisStopRequested = false;
        root.axisHomeRequested = false;
        motionSafetyTimer.restart();

        const started = root.runAsyncAction(
            label, "MoveAxis", {"axis": axis, "direction": direction},
            false, function (result, ok) {
                if (!ok) {
                    root.clearAxisMotionState();
                    return;
                }
                if (root.axisHomeRequested) {
                    Qt.callLater(root.stopAndHomeHead);
                } else if (root.axisStopRequested || !root.axisMotionHeld) {
                    Qt.callLater(root.stopActiveAxis);
                }
            }, false, false);
        if (!started)
            root.clearAxisMotionState();
        return started;
    }

    function releaseAxisMotion(axis, direction) {
        if (!root.axisMotionActive || root.activeMotionAxis !== axis
                || root.activeMotionDirection !== direction)
            return;

        root.axisMotionHeld = false;
        root.axisStopRequested = true;
        if (!nocaiDirectPrint.maintenanceBusy)
            root.stopActiveAxis();
    }

    function stopActiveAxis() {
        if (!root.axisMotionActive)
            return false;
        if (nocaiDirectPrint.maintenanceBusy) {
            root.axisStopRequested = true;
            return false;
        }

        const axis = root.activeMotionAxis;
        const label = axis === 0 ? strings.trKey("printerMaintenance.action.stopHeadMotion")
                    : (axis === 1 ? strings.trKey("printerMaintenance.action.stopBedMotion")
                                  : strings.trKey("printerMaintenance.action.stopHeightMotion"));
        root.axisStopRequested = false;
        return root.runAsyncAction(
            label, "StopAxis", {"axis": axis}, true,
            function (result, ok) {
                root.clearAxisMotionState();
                if (ok && axis === 2)
                    root.statusText = label + strings.trKey("printerMaintenance.status.atPosition")
                                      + result.position + " mm.";
            }, false, false);
    }

    function stopPrinterAxis(axis, label) {
        root.activeMotionAxis = axis;
        return root.runAsyncAction(
            label, "StopAxis", {"axis": axis}, true,
            function (result, ok) {
                if (ok)
                    root.statusText = label + strings.trKey("printerMaintenance.status.atPosition")
                                      + result.position + " mm.";
            });
    }

    function stopAndHomeHead() {
        root.axisMotionHeld = false;
        root.axisStopRequested = false;
        root.axisHomeRequested = true;
        if (nocaiDirectPrint.maintenanceBusy)
            return false;

        const axis = root.axisMotionActive ? root.activeMotionAxis : 0;
        return root.runAsyncAction(
            strings.trKey("printerMaintenance.action.stopAndHomeHead"),
            "StopAndHomeHead", {"axis": axis}, true,
            function (result, ok) {
                root.clearAxisMotionState();
                if (ok)
                    root.statusText = strings.trKey("printerMaintenance.status.motionStoppedHome");
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
        case strings.trKey("printerMaintenance.action.refreshStatus"): return strings.trKey("printerMaintenance.progress.refreshingStatus");
        case strings.trKey("printerMaintenance.action.connect"): return strings.trKey("printerMaintenance.progress.connecting");
        case strings.trKey("printerMaintenance.action.nozzleCheck"): return strings.trKey("printerMaintenance.progress.nozzleCheck");
        case strings.trKey("printerMaintenance.action.autoClean"): return strings.trKey("printerMaintenance.progress.cleaningHeads");
        case strings.trKey("printerMaintenance.action.wipeHeads"): return strings.trKey("printerMaintenance.progress.wipingHeads");
        case strings.trKey("printerMaintenance.action.startManualClean"): return strings.trKey("printerMaintenance.progress.startManualClean");
        case strings.trKey("printerMaintenance.action.stopManualClean"): return strings.trKey("printerMaintenance.progress.stopManualClean");
        case strings.trKey("printerMaintenance.action.startFlushing"): return strings.trKey("printerMaintenance.progress.startFlushing");
        case strings.trKey("printerMaintenance.action.stopFlushing"): return strings.trKey("printerMaintenance.progress.stopFlushing");
        case strings.trKey("printerMaintenance.action.capHead"): return strings.trKey("printerMaintenance.progress.cappingHead");
        case strings.trKey("printerMaintenance.action.setHeight"): return strings.trKey("printerMaintenance.progress.movingHeight");
        case strings.trKey("printerMaintenance.action.getHeight"): return strings.trKey("printerMaintenance.progress.readingHeight");
        }
        return root.pendingMaintenanceLabel.length > 0
            ? root.pendingMaintenanceLabel + "…"
            : strings.trKey("printerMaintenance.progress.working");
    }

    function refreshJobSettings() {
        runAsyncAction(strings.trKey("printerMaintenance.action.readJobSettings"),
            "GetJobSettings", {}, false,
            function (result, ok) {
                if (ok)
                    root.jobSettings = result;
            });
    }

    function refreshAlignment() {
        runAsyncAction(strings.trKey("printerMaintenance.action.readAlignment"),
            "GetAlignmentValues", {}, false,
            function (result, ok) {
                if (ok)
                    root.alignmentValues = result;
            });
    }

    function refreshUv() {
        runAsyncAction(strings.trKey("printerMaintenance.action.readUv"),
            "GetUVParamValues", {}, false,
            function (result, ok) {
                if (ok)
                    root.uvValues = result;
            });
    }

    function refreshNewUv() {
        runAsyncAction(strings.trKey("printerMaintenance.action.readNewUv"),
            "GetNewUVParamValues", {}, false,
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
                ? label + strings.trKey("printerMaintenance.toast.succeededSuffix")
                : label + strings.trKey("printerMaintenance.toast.failedSuffix") + errorMessage;
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
        interval: 5000
        repeat: true
        running: root.visible && root.controlsEnabled && root.statusPollingEnabled
                 && !root.axisMotionActive && !root.userScrolling
        onTriggered: root.updateStatus(true)
    }

    Timer {
        id: motionSafetyTimer
        interval: 30000
        repeat: false
        onTriggered: {
            if (!root.axisMotionActive)
                return;
            root.axisMotionHeld = false;
            root.axisHomeRequested = true;
            toast.show(strings.trKey("printerMaintenance.toast.motionTimeout"));
            root.stopAndHomeHead();
        }
    }

    Component.onCompleted: {
        root.statusPollingEnabled = true;
        Qt.callLater(function () { root.updateStatus(true); });
    }

    header: Rectangle {
        id: maintenanceHeader
        height: root.theme.appBarHeight
        color: root.theme.surface

        RowLayout {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 10

            ThemedButton {
                text: strings.trKey("common.back")
                theme: root.theme
                Layout.preferredWidth: root.theme.headerButtonWidth(root.width)
                Layout.preferredHeight: root.theme.compactControlHeight
                enabled: !root.axisMotionActive
                         && !nocaiDirectPrint.maintenanceBusy
                onClicked: root.goBack()
            }

            Label {
                text: strings.trKey("printerMaintenance.title")
                color: root.theme.text
                font.pixelSize: root.theme.headerTitleSize(root.width)
                font.weight: Font.Medium
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
            }

            ThemedButton {
                text: strings.trKey("common.connect")
                theme: root.theme
                Layout.preferredWidth: root.theme.headerButtonWidth(root.width)
                Layout.preferredHeight: root.theme.compactControlHeight
                enabled: root.maintenanceSupported && !nocaiDirectPrint.maintenanceBusy
                onClicked: root.runAsyncAction(
                    strings.trKey("printerMaintenance.action.connect"), "ConnectPrinter",
                    {"printerIndex": root.appState.sdkSelectedPrinterIndex}, true,
                    function (result, ok) {
                        root.statusPollingEnabled = ok;
                    })
            }
        }
    }

    ScrollView {
        id: maintenanceScroll
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: root.theme.boundedWidth(parent.width, 520)
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 12
            anchors.margins: root.theme.pageMargin

            Section {
                title: strings.trKey("printerMaintenance.section.status")
                theme: root.theme
                sectionEnabled: root.maintenanceSupported
                help: strings.trKey("printerMaintenance.section.status.help")
                expanded: true

                Label {
                    Layout.fillWidth: true
                    text: root.maintenanceSupported ? root.statusText
                                                    : strings.trKey("printerMaintenance.unavailable")
                    color: root.maintenanceSupported ? root.theme.subtext : root.theme.warning
                    wrapMode: Text.WordWrap
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    Label {
                        Layout.fillWidth: true
                        color: root.theme.text
                        text: strings.trKey("printerMaintenance.status.printPrefix")
                              + root.printStatusText(root.readMapValue(root.printerStatus, "printStatus", -1))
                    }

                    Label {
                        Layout.fillWidth: true
                        color: root.theme.text
                        text: strings.trKey("printerMaintenance.status.cleanPrefix")
                              + root.cleanStatusText(root.readMapValue(root.printerStatus, "cleanStatus", -1))
                    }
                }

                ActionButton {
                    text: strings.trKey("printerMaintenance.action.refreshStatus")
                    onClicked: {
                        root.statusPollingEnabled = true;
                        root.updateStatus(false);
                    }
                }
            }

            Section {
                title: strings.trKey("printerMaintenance.section.headMaintenance")
                theme: root.theme
                sectionEnabled: root.controlsEnabled
                help: strings.trKey("printerMaintenance.section.headMaintenance.help")
                expanded: true

                GridLayout {
                    Layout.fillWidth: true
                    columns: root.theme.gridColumns(width, 2, 150)
                    columnSpacing: 12
                    rowSpacing: 8

                    FieldLabel {
                        text: strings.trKey("printerMaintenance.headMask")
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
                        text: strings.trKey("printerMaintenance.action.nozzleCheck")
                        theme: root.theme
                        onClicked: root.runAsyncAction(
                            strings.trKey("printerMaintenance.action.nozzleCheck"), "PrintNozzleCheck", {}, true, null)
                    }
                    ActionButton {
                        text: strings.trKey("printerMaintenance.action.autoClean")
                        theme: root.theme
                        onClicked: root.runAsyncAction(
                            strings.trKey("printerMaintenance.action.autoClean"), "StartCleanOperation",
                            {"headMask": root.headMask}, true, null)
                    }
                    ActionButton {
                        text: strings.trKey("printerMaintenance.action.wipeHeads")
                        theme: root.theme
                        onClicked: root.runAsyncAction(
                            strings.trKey("printerMaintenance.action.wipeHeads"), "WipePrintHead",
                            {"headMask": root.headMask}, true, null)
                    }
                    ActionButton {
                        text: strings.trKey("printerMaintenance.action.capHead")
                        theme: root.theme
                        onClicked: root.runAsyncAction(
                            strings.trKey("printerMaintenance.action.capHead"), "CapPrintHead", {}, true, null)
                    }
                    ActionButton {
                        text: strings.trKey("printerMaintenance.action.startManualClean")
                        theme: root.theme
                        onClicked: root.runAsyncAction(
                            strings.trKey("printerMaintenance.action.startManualClean"), "StartPump",
                            {"headMask": root.headMask}, true, null)
                    }
                    ActionButton {
                        text: strings.trKey("printerMaintenance.action.stopManualClean")
                        theme: root.theme
                        onClicked: root.runAsyncAction(
                            strings.trKey("printerMaintenance.action.stopManualClean"), "StopPumpOperation", {}, true, null)
                    }
                    ActionButton {
                        text: strings.trKey("printerMaintenance.action.startFlushing")
                        theme: root.theme
                        onClicked: root.runAsyncAction(
                            strings.trKey("printerMaintenance.action.startFlushing"), "StartFlashSpray",
                            {"headMask": root.headMask}, true, null)
                    }
                    ActionButton {
                        text: strings.trKey("printerMaintenance.action.stopFlushing")
                        theme: root.theme
                        onClicked: root.runAsyncAction(
                            strings.trKey("printerMaintenance.action.stopFlushing"), "StopFlashSpray", {}, true, null)
                    }
                }
            }

            Section {
                title: strings.trKey("printerMaintenance.section.motionHeight")
                theme: root.theme
                sectionEnabled: root.controlsEnabled
                help: strings.trKey("printerMaintenance.section.motionHeight.help")
                expanded: !root.theme.mobile

                Label {
                    text: strings.trKey("printerMaintenance.headBedMotion")
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
                    MotionButton {
                        text: strings.trKey("printerMaintenance.motion.back")
                        Accessible.name: strings.trKey("printerMaintenance.motion.moveBedBack")
                        motionAxis: 1
                        motionDirection: 1
                        motionLabel: strings.trKey("printerMaintenance.motion.moveBedBack")
                    }
                    Item {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                    }

                    MotionButton {
                        text: strings.trKey("printerMaintenance.motion.headLeft")
                        Accessible.name: strings.trKey("printerMaintenance.motion.moveHeadLeft")
                        motionAxis: 0
                        motionDirection: 0
                        motionLabel: strings.trKey("printerMaintenance.motion.moveHeadLeft")
                    }
                    ThemedButton {
                        text: strings.trKey("printerMaintenance.motion.stopHome")
                        theme: root.theme
                        Accessible.name: strings.trKey("printerMaintenance.motion.stopHomeAccessible")
                        enabled: root.controlsEnabled
                                 && (!nocaiDirectPrint.maintenanceBusy
                                     || root.axisMotionActive)
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        Layout.preferredWidth: 1
                        Layout.preferredHeight: 44
                        padding: 8
                        font.pixelSize: 12
                        onClicked: root.stopAndHomeHead()
                    }
                    MotionButton {
                        text: strings.trKey("printerMaintenance.motion.headRight")
                        Accessible.name: strings.trKey("printerMaintenance.motion.moveHeadRight")
                        motionAxis: 0
                        motionDirection: 1
                        motionLabel: strings.trKey("printerMaintenance.motion.moveHeadRight")
                    }

                    Item {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                    }
                    MotionButton {
                        text: strings.trKey("printerMaintenance.motion.forward")
                        Accessible.name: strings.trKey("printerMaintenance.motion.moveBedForward")
                        motionAxis: 1
                        motionDirection: 0
                        motionLabel: strings.trKey("printerMaintenance.motion.moveBedForward")
                    }
                    Item {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                    }
                }

                ActionGrid {
                    ActionButton {
                        text: strings.trKey("printerMaintenance.action.saveHeadPosition")
                        onClicked: root.savePrinterAxis(0, strings.trKey("printerMaintenance.status.savedHeadPosition"))
                    }
                    ActionButton {
                        text: strings.trKey("printerMaintenance.action.saveBedPosition")
                        onClicked: root.savePrinterAxis(1, strings.trKey("printerMaintenance.status.savedBedPosition"))
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
                        text: strings.trKey("printerMaintenance.printHeightMm")
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
                        text: strings.trKey("printerMaintenance.action.setHeight")
                        theme: root.theme
                        onClicked: {
                            root.activeMotionAxis = 2;
                            root.runAsyncAction(
                                strings.trKey("printerMaintenance.action.setHeight"), "SetPrintHeight",
                                {"heightMm": root.printHeight}, true, null);
                        }
                    }
                    ActionButton {
                        text: strings.trKey("printerMaintenance.action.getHeight")
                        theme: root.theme
                        onClicked: root.runAsyncAction(
                            strings.trKey("printerMaintenance.action.getHeight"), "GetPrintHeight", {}, false,
                            function (result, ok) {
                                if (ok) {
                                    root.printHeight = result.heightMm;
                                    root.statusText = strings.trKey("printerMaintenance.status.printHeight")
                                                      + result.heightMm + " mm";
                                }
                            })
                    }
                    ActionButton {
                        text: strings.trKey("printerMaintenance.action.stopHeight")
                        onClicked: root.stopPrinterAxis(2, strings.trKey("printerMaintenance.action.stopHeightMotion"))
                    }
                    ActionButton {
                        text: strings.trKey("printerMaintenance.action.saveHeightPosition")
                        onClicked: root.savePrinterAxis(2, strings.trKey("printerMaintenance.status.savedHeightPosition"))
                    }
                }
            }

            Section {
                title: strings.trKey("printerMaintenance.section.settingsConfig")
                theme: root.theme
                sectionEnabled: root.controlsEnabled
                help: strings.trKey("printerMaintenance.section.settingsConfig.help")
                expanded: !root.theme.mobile

                ActionGrid {
                    ActionButton {
                        text: strings.trKey("printerMaintenance.action.readJobSettings")
                        theme: root.theme
                        onClicked: root.refreshJobSettings()
                    }
                    ActionButton {
                        text: strings.trKey("printerMaintenance.action.applyReadSettings")
                        theme: root.theme
                        onClicked: root.runAsyncAction(
                            strings.trKey("printerMaintenance.action.applyReadSettings"), "SetJobSettings",
                            {"settings": root.jobSettings}, true, null)
                    }
                }

                Label {
                    Layout.fillWidth: true
                    text: strings.trKey("printerMaintenance.settings.direction")
                          + root.readMapValue(root.jobSettings, "printDirection", "-")
                          + strings.trKey("printerMaintenance.settings.speed")
                          + root.readMapValue(root.jobSettings, "printSpeed", "-")
                          + strings.trKey("printerMaintenance.settings.headVoltage")
                          + root.readMapValue(root.jobSettings, "headVoltage", "-")
                    color: root.theme.subtext
                    wrapMode: Text.WordWrap
                }

                ActionGrid {
                    ActionButton {
                        text: strings.trKey("printerMaintenance.action.exportConfig")
                        theme: root.theme
                        onClicked: exportConfigDialog.open()
                    }
                    ActionButton {
                        text: strings.trKey("printerMaintenance.action.importConfig")
                        theme: root.theme
                        onClicked: importConfigDialog.open()
                    }
                }
            }

            Section {
                title: strings.trKey("printerMaintenance.section.alignment")
                theme: root.theme
                sectionEnabled: root.controlsEnabled
                help: strings.trKey("printerMaintenance.section.alignment.help")
                expanded: !root.theme.mobile

                GridLayout {
                    Layout.fillWidth: true
                    columns: root.theme.gridColumns(width, 2, 150)
                    columnSpacing: 12
                    rowSpacing: 8

                    FieldLabel {
                        text: strings.trKey("printerMaintenance.valueType")
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
                        text: strings.trKey("printerMaintenance.patternType")
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
                        text: strings.trKey("printerMaintenance.action.readAlignment")
                        theme: root.theme
                        onClicked: root.refreshAlignment()
                    }
                    ActionButton {
                        text: strings.trKey("printerMaintenance.action.applyAlignment")
                        theme: root.theme
                        onClicked: root.runAsyncAction(
                            strings.trKey("printerMaintenance.action.applyAlignment"), "SetAlignmentValues",
                            {"settings": root.alignmentValues, "type": root.alignmentType},
                            true, null)
                    }
                }

                ActionButton {
                    text: strings.trKey("printerMaintenance.action.printAlignmentPattern")
                    theme: root.theme
                    onClicked: root.runAsyncAction(
                        strings.trKey("printerMaintenance.action.printAlignmentPattern"), "PrintAlignmentPattern",
                        {"type": root.alignmentPatternType}, true, null)
                }

                Label {
                    Layout.fillWidth: true
                    text: strings.trKey("printerMaintenance.alignment.step")
                          + root.readMapValue(root.alignmentValues, "stepValue", "-")
                          + strings.trKey("printerMaintenance.alignment.bidi")
                          + root.readMapValue(root.alignmentValues, "bidiValue", "-")
                    color: root.theme.subtext
                    wrapMode: Text.WordWrap
                }
            }

            Section {
                title: strings.trKey("printerMaintenance.section.xyUv")
                theme: root.theme
                sectionEnabled: root.controlsEnabled
                help: strings.trKey("printerMaintenance.section.xyUv.help")
                expanded: !root.theme.mobile

                GridLayout {
                    Layout.fillWidth: true
                    columns: root.theme.gridColumns(width, 2, 150)
                    columnSpacing: 12
                    rowSpacing: 8

                    FieldLabel {
                        text: strings.trKey("printerMaintenance.xOffsetMm")
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
                        text: strings.trKey("printerMaintenance.yOffsetMm")
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
                        text: strings.trKey("printerMaintenance.uvValueType")
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
                        text: strings.trKey("printerMaintenance.newUvValueType")
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
                        text: strings.trKey("printerMaintenance.newUvFunction")
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
                        text: strings.trKey("printerMaintenance.action.setXy")
                        theme: root.theme
                        onClicked: root.runAsyncAction(
                            strings.trKey("printerMaintenance.action.setXy"), "SetPrintXYValue",
                            {"xMm": root.printX, "yMm": root.printY}, true, null)
                    }
                    ActionButton {
                        text: strings.trKey("printerMaintenance.action.getXy")
                        theme: root.theme
                        onClicked: root.runAsyncAction(
                            strings.trKey("printerMaintenance.action.getXy"), "GetPrintXYValue", {}, false,
                            function (result, ok) {
                                if (ok) {
                                    root.printX = result.xMm;
                                    root.printY = result.yMm;
                                    root.statusText = strings.trKey("printerMaintenance.status.xy")
                                                      + result.xMm + ", " + result.yMm + " mm";
                                }
                            })
                    }
                    ActionButton {
                        text: strings.trKey("printerMaintenance.action.readUv")
                        theme: root.theme
                        onClicked: root.refreshUv()
                    }
                    ActionButton {
                        text: strings.trKey("printerMaintenance.action.applyUv")
                        theme: root.theme
                        onClicked: root.runAsyncAction(
                            strings.trKey("printerMaintenance.action.applyUv"), "SetUVParamValues",
                            {"settings": root.uvValues, "type": root.uvType}, true, null)
                    }
                    ActionButton {
                        text: strings.trKey("printerMaintenance.action.newUvSupport")
                        theme: root.theme
                        onClicked: root.runAsyncAction(
                            strings.trKey("printerMaintenance.action.newUvSupport"), "GetSupportNewUVParamFunction",
                            {}, false, function (result, ok) {
                                if (ok)
                                    root.statusText = strings.trKey("printerMaintenance.status.newUvSupport") + result;
                            })
                    }
                    ActionButton {
                        text: strings.trKey("printerMaintenance.action.runNewUv")
                        theme: root.theme
                        onClicked: root.runAsyncAction(
                            strings.trKey("printerMaintenance.action.runNewUv"), "SetNewUVParamFunction",
                            {"type": root.newUvFunctionType}, true, null)
                    }
                    ActionButton {
                        text: strings.trKey("printerMaintenance.action.readNewUv")
                        theme: root.theme
                        onClicked: root.refreshNewUv()
                    }
                    ActionButton {
                        text: strings.trKey("printerMaintenance.action.applyNewUv")
                        theme: root.theme
                        onClicked: root.runAsyncAction(
                            strings.trKey("printerMaintenance.action.applyNewUv"), "SetNewUVParamValues",
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
        title: strings.trKey("printerMaintenance.exportConfig.title")
        fileMode: P.FileDialog.SaveFile
        defaultSuffix: "pfg"
        nameFilters: ["Printer Config (*.pfg)", "All Files (*)"]
        onAccepted: root.runAsyncAction(
            strings.trKey("printerMaintenance.action.exportConfig"), "ExportConfigFile", {"path": file}, true, null)
    }

    P.FileDialog {
        id: importConfigDialog
        title: strings.trKey("printerMaintenance.importConfig.title")
        fileMode: P.FileDialog.OpenFile
        nameFilters: ["Printer Config (*.pfg)", "All Files (*)"]
        onAccepted: root.runAsyncAction(
            strings.trKey("printerMaintenance.action.importConfig"), "ImportConfigFile", {"path": file}, true, null)
    }

    Toast {
        id: toast
        parent: Overlay.overlay
    }
}
