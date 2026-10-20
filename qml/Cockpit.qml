pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

Item {
    id: cockpit
    objectName: "cockpit"
    required property QtObject controller
    required property UiTheme theme
    required property bool active
    readonly property bool compact: width < 1100 || height < 700
    readonly property bool live: active && Window.window !== null && Window.window.visible && Window.window.visibility !== Window.Minimized
    readonly property var telemetry: live ? controller.telemetry : ({})
    readonly property bool canInteract: controller.startupReady && !controller.ttsBenchmarkRunning && !controller.ttsBenchmarkRestoring
    property bool pttHeld: false
    onCanInteractChanged: if (!canInteract) releasePtt()
    onActiveChanged: if (!active) { releasePtt(); closeHistory() }
    onVisibleChanged: if (!visible) releasePtt()
    onLiveChanged: {
        if (!live) releasePtt()
        else if (history.visible && historyContentLoader.item)
            Qt.callLater(function() {
                if (historyContentLoader.item) historyContentLoader.item.syncHistory()
            })
    }
    Component.onDestruction: releasePtt()
    Connections {
        target: cockpit.Window.window
        function onActiveChanged() { if (cockpit.Window.window && !cockpit.Window.window.active) cockpit.releasePtt() }
    }
    function releasePtt() {
        if (pttHeld) {
            pttHeld = false
            if (controller) controller.endPushToTalk()
        }
    }
    function sendRequest() {
        var text = command.text.trim()
        if (!canInteract || !text) return
        controller.askText(text)
        command.clear()
    }
    function closeHistory() {
        if (history.visible) {
            history.close()
            history.visible = false
        }
    }
    component Label: Text {
        color: cockpit.theme.muted
        font.family: cockpit.theme.uiFont
        font.pixelSize: 14
        font.weight: cockpit.theme.uiWeight
        textFormat: Text.PlainText
    }
    component Action: Button {
        id: action
        property bool primary: false
        implicitHeight: cockpit.compact ? 40 : 52
        leftPadding: cockpit.compact ? 12 : 18; rightPadding: cockpit.compact ? 12 : 18
        font.family: cockpit.theme.uiFont; font.pixelSize: cockpit.compact ? 14 : 15; font.weight: cockpit.theme.sectionWeight
        opacity: enabled ? 1 : 0.5
        contentItem: Text { text: action.text; font: action.font; color: action.primary ? cockpit.theme.accentText : cockpit.theme.text; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
        background: Rectangle {
            radius: 4
            color: action.primary ? (action.down ? Qt.darker(cockpit.theme.accent, 1.2) : cockpit.theme.accent) : action.hovered ? cockpit.theme.raised : cockpit.theme.surface
            border.color: action.activeFocus ? cockpit.theme.accent : action.primary ? "transparent" : cockpit.theme.outline
            border.width: action.activeFocus ? 2 : 1
        }
    }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: cockpit.compact ? 12 : 24
        anchors.topMargin: cockpit.compact ? 8 : 4
        spacing: cockpit.compact ? 6 : 10
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: cockpit.compact ? 48 : Math.max(trackText.implicitHeight, positionText.implicitHeight)
            Layout.maximumHeight: Layout.preferredHeight
            spacing: cockpit.compact ? 12 : 24
            Label {
                id: trackText
                objectName: "trackValue"
                Layout.maximumWidth: (cockpit.width - (cockpit.compact ? 32 : 52)) * 0.40
                text: !cockpit.controller.connected ? "CHỜ PHIÊN"
                    : cockpit.telemetry.track ? String(cockpit.telemetry.track).toUpperCase() : "ĐÃ KẾT NỐI"
                font.family: cockpit.theme.displayFont
                font.weight: cockpit.theme.displayWeight
                font.pixelSize: cockpit.compact ? 38 : cockpit.width >= 1450 ? 112 : 96
                color: cockpit.theme.text
                elide: Text.ElideRight
                Accessible.name: text
                HoverHandler { id: trackHover }
                ToolTip.visible: trackHover.hovered && trackText.implicitWidth > trackText.width
                ToolTip.text: trackText.text
                ToolTip.delay: 500
            }
            Rectangle { visible: !cockpit.compact; width: 1; Layout.preferredHeight: 72; color: cockpit.theme.divider }
            Column {
                Layout.fillWidth: true
                spacing: 5
                Label { text: cockpit.controller.connected ? cockpit.controller.simulatorName : "Chờ AC / ACC"; font.pixelSize: cockpit.compact ? 13 : 16; font.weight: cockpit.theme.sectionWeight }
                Label {
                    id: carText
                    objectName: "carValue"
                    width: parent.width
                    text: !cockpit.controller.connected ? "Khởi động simulator để xem dữ liệu xe"
                        : cockpit.telemetry.carModel || "Chưa có thông tin xe từ simulator"
                    elide: Text.ElideRight
                    font.pixelSize: cockpit.compact ? 12 : 14
                    Accessible.name: text
                    HoverHandler { id: carHover }
                    ToolTip.visible: carHover.hovered && carText.implicitWidth > carText.width
                    ToolTip.text: carText.text
                    ToolTip.delay: 500
                }
            }
            Column {
                spacing: 0
                Label { text: "VÒNG"; font.pixelSize: cockpit.compact ? 12 : 14; font.weight: cockpit.theme.uiWeight }
                Label { objectName: "currentLapValue"; text: cockpit.theme.number(cockpit.telemetry.currentLap, 0, ""); font.family: cockpit.theme.displayFont; font.weight: cockpit.theme.displayWeight; font.pixelSize: cockpit.compact ? 34 : 80; color: cockpit.theme.text }
            }
            Label {
                id: positionText
                objectName: "positionValue"
                text: cockpit.theme.hasNumber(cockpit.telemetry.position) ? "P" + String(cockpit.telemetry.position).padStart(2, "0") : "P—"
                font.family: cockpit.theme.displayFont
                font.weight: cockpit.theme.displayWeight
                font.pixelSize: cockpit.compact ? 42 : cockpit.width >= 1450 ? 112 : 96
                color: cockpit.theme.text
            }
        }
        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: cockpit.theme.divider }
        Item {
            id: liveDataViewport
            objectName: "cockpitDataViewport"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 0
            DrivingData { id: drivingData; objectName: "cockpitDrivingData"; anchors.fill: parent; availableHeight: parent.height; theme: cockpit.theme; telemetry: cockpit.telemetry }
        }
        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: cockpit.theme.divider }
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: cockpit.compact ? 30 : 48
            spacing: cockpit.compact ? 10 : 20
            ColumnLayout {
                spacing: 2
                Label { text: "KỸ SƯ"; color: cockpit.theme.accent; font.weight: cockpit.theme.sectionWeight; font.pixelSize: cockpit.compact ? 12 : 14 }
                Label {
                    Layout.maximumWidth: cockpit.compact ? 100 : 150
                    text: cockpit.live && cockpit.controller.microphoneName ? cockpit.controller.microphoneName : "MIC"
                    color: cockpit.theme.muted
                    font.pixelSize: cockpit.compact ? 11 : 13
                    elide: Text.ElideRight
                    Accessible.name: "Micro: " + text
                }
            }
            Label {
                Layout.fillWidth: true
                text: (cockpit.live ? cockpit.controller.latestEngineerText : "") || (cockpit.canInteract ? "Radio sẵn sàng. Giữ PTT hoặc nhập câu hỏi cho kỹ sư." : "Đang chuẩn bị giọng nói. Telemetry vẫn cập nhật.")
                color: cockpit.theme.text
                font.family: cockpit.theme.uiFont
                font.weight: cockpit.theme.sectionWeight
                font.pixelSize: cockpit.compact ? 14 : 20
                maximumLineCount: cockpit.compact ? 1 : 2
                wrapMode: Text.Wrap
                elide: Text.ElideRight
                textFormat: Text.PlainText
            }
            Label {
                objectName: "audioDuckedStatus"
                visible: cockpit.live && cockpit.controller.isAudioDucked
                text: "HẠ ÂM GAME"
                color: cockpit.theme.muted
                font.pixelSize: 12
                font.weight: cockpit.theme.sectionWeight
                Accessible.name: text
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: cockpit.compact ? 8 : 12
            TextField {
                id: command
                objectName: "radioCommand"
                Layout.fillWidth: true
                implicitHeight: cockpit.compact ? 40 : 52
                enabled: cockpit.canInteract
                font.family: cockpit.theme.uiFont
                font.pixelSize: cockpit.compact ? 13 : 15
                font.weight: cockpit.theme.uiWeight
                color: cockpit.theme.text
                placeholderTextColor: cockpit.theme.muted
                placeholderText: "Nhập câu hỏi cho kỹ sư…"
                Accessible.name: "Câu hỏi cho kỹ sư"
                selectByMouse: true
                onAccepted: cockpit.sendRequest()
                background: Rectangle { radius: 4; color: cockpit.theme.surface; border.color: command.activeFocus ? cockpit.theme.accent : cockpit.theme.outline; border.width: command.activeFocus ? 2 : 1 }
            }
            Action { objectName: "radioSend"; text: "Gửi"; enabled: cockpit.canInteract && command.text.trim().length > 0; onClicked: cockpit.sendRequest() }
            Action {
                objectName: "radioHistoryButton"
                text: cockpit.compact ? "Lịch sử" : "Lịch sử radio"
                onClicked: history.open()
            }
            Action {
                objectName: "radioPtt"
                Layout.preferredWidth: cockpit.compact ? 132 : 220
                primary: true
                text: cockpit.pttHeld ? "Đang nghe…" : "Giữ để nói"
                enabled: cockpit.canInteract
                Accessible.name: "Giữ để nói với kỹ sư"
                ToolTip.visible: hovered
                ToolTip.text: cockpit.controller.keyboardPttEnabled ? "Giữ chuột hoặc Ctrl+Space để nói" : "Giữ chuột để nói"
                onPressedChanged: {
                    if (pressed && !cockpit.pttHeld) {
                        cockpit.pttHeld = true
                        cockpit.controller.beginPushToTalk()
                    } else if (!pressed) {
                        cockpit.releasePtt()
                    }
                }
            }
        }
    }
    Drawer {
        id: history
        objectName: "radioHistory"
        parent: Overlay.overlay
        edge: Qt.RightEdge
        width: Math.min(620, parent.width - 24)
        height: parent.height
        padding: 24
        background: Rectangle { color: cockpit.theme.canvas; border.color: cockpit.theme.divider }
        Loader {
            id: historyContentLoader
            anchors.fill: parent
            anchors.margins: history.padding
            active: history.visible
            sourceComponent: ColumnLayout {
                id: historyContent
                property bool syncingHistory: false
                property int syncGeneration: 0
                spacing: 20
                function syncHistory() {
                    if (!cockpit.live || !history.visible) return
                    var entries = cockpit.controller.conversationLog || []
                    var desiredCount = entries.length
                    var startIndex = entries.length - desiredCount
                    var viewportOffset = log.contentY - log.originY
                    var nextGeneration = ++syncGeneration
                    syncingHistory = true
                    if (desiredCount === historyMessages.count && desiredCount > 0) {
                        var firstEntry = entries[startIndex] || ({})
                        var firstMessage = historyMessages.get(0)
                        if (firstMessage.role !== String(firstEntry.role || "") || firstMessage.text !== String(firstEntry.text || ""))
                            historyMessages.remove(0)
                    }
                    while (historyMessages.count > desiredCount)
                        historyMessages.remove(historyMessages.count - 1)
                    for (var i = 0; i < desiredCount; ++i) {
                        var entry = entries[startIndex + i] || ({})
                        var nextRole = String(entry.role || "")
                        var nextText = String(entry.text || "")
                        if (i >= historyMessages.count) {
                            historyMessages.append({ role: nextRole, text: nextText })
                        } else {
                            var current = historyMessages.get(i)
                            if (current.role !== nextRole || current.text !== nextText)
                                historyMessages.set(i, { role: nextRole, text: nextText })
                        }
                    }
                    Qt.callLater(function() {
                        if (nextGeneration !== historyContent.syncGeneration) return
                        if (log.followTail) log.positionViewAtEnd()
                        else {
                            var maxY = Math.max(log.originY, log.contentHeight - log.height)
                            log.contentY = Math.min(maxY, Math.max(log.originY, viewportOffset + log.originY))
                        }
                        historyContent.syncingHistory = false
                    })
                }
                ListModel { id: historyMessages }
                Connections {
                    target: cockpit.live && history.visible ? cockpit.controller : null
                    ignoreUnknownSignals: true
                    function onConversationLogChanged() { historyContent.syncHistory() }
                    function onChanged() { historyContent.syncHistory() }
                }
                Component.onCompleted: Qt.callLater(syncHistory)
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: "Lịch sử radio"; color: cockpit.theme.text; font.pixelSize: 23; font.weight: Font.DemiBold; Layout.fillWidth: true }
                    Action { text: "Đóng"; onClicked: history.close() }
                }
                ListView {
                    id: log
                    objectName: "radioHistoryList"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    reuseItems: true
                    spacing: 20
                    model: historyMessages
                    property bool followTail: true
                    function scrollToTail() { if (followTail) positionViewAtEnd() }
                    onCountChanged: Qt.callLater(scrollToTail)
                    onContentHeightChanged: Qt.callLater(scrollToTail)
                    onMovementStarted: followTail = false
                    onMovementEnded: followTail = atYEnd
                    ScrollBar.vertical: ScrollBar {}
                    delegate: Column {
                        required property string role
                        required property string text
                        width: log.width
                        spacing: 7
                        Label { text: parent.role === "driver" ? "TAY ĐUA" : "KỸ SƯ"; color: parent.role === "driver" ? cockpit.theme.muted : cockpit.theme.accent; font.pixelSize: 12 }
                        Label { width: parent.width; text: parent.text; color: cockpit.theme.text; wrapMode: Text.Wrap; font.pixelSize: 15; textFormat: Text.PlainText }
                    }
                    Label { anchors.centerIn: parent; visible: log.count === 0; text: "Chưa có hội thoại trong phiên này." }
                }
                Action { visible: !log.followTail; text: "Về cuối hội thoại"; onClicked: { log.followTail = true; log.scrollToTail() } }
            }
        }
    }
}
