pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: panel
    objectName: "ttsBenchmarkPanel"
    required property var controller
    required property UiTheme theme
    required property bool live
    readonly property bool complete: controller.ttsBenchmarkResults.length === 3
        && String(controller.ttsBenchmarkStatus).indexOf("Đã áp dụng") === 0
    Layout.fillWidth: true
    spacing: 10

    component BodyText: Text {
        color: panel.theme.text
        font.family: panel.theme.uiFont
        font.pixelSize: panel.theme.pageTextSize(14)
        font.weight: panel.theme.uiWeight
        wrapMode: Text.Wrap
        textFormat: Text.PlainText
    }
    component MutedText: BodyText { color: panel.theme.muted; font.pixelSize: panel.theme.pageTextSize(13) }
    component ActionButton: Button {
        id: control
        property bool primaryAction: false
        implicitHeight: 44
        implicitWidth: Math.max(120, contentItem.implicitWidth + leftPadding + rightPadding)
        leftPadding: 18; rightPadding: 18
        topPadding: 10; bottomPadding: 10
        font.family: panel.theme.uiFont
        font.pixelSize: panel.theme.pageTextSize(14)
        font.weight: panel.theme.sectionWeight
        Accessible.name: text
        contentItem: Text { text: control.text; color: !control.enabled ? panel.theme.muted : control.primaryAction ? panel.theme.accentText : panel.theme.text; font: control.font; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
        background: Rectangle { color: !control.enabled ? panel.theme.canvas : control.primaryAction ? panel.theme.accent : control.hovered || control.down ? panel.theme.raised : panel.theme.surface; border.color: control.activeFocus ? panel.theme.outline : control.enabled ? panel.theme.outline : panel.theme.divider; border.width: control.activeFocus ? 2 : 1; radius: 3 }
    }
    component SurfaceCard: Rectangle {
        id: card
        default property alias content: body.data
        property string title: ""
        color: panel.theme.surface
        border.color: panel.theme.divider
        border.width: 1
        radius: 4
        implicitHeight: body.implicitHeight + 32
        Column {
            id: body
            anchors.fill: parent
            anchors.margins: 16
            spacing: 10
            BodyText { visible: card.title.length > 0; text: card.title; font.weight: panel.theme.sectionWeight; font.pixelSize: panel.theme.pageTextSize(15) }
            Rectangle { visible: card.title.length > 0; width: parent.width; height: 1; color: panel.theme.divider }
        }
    }

    SurfaceCard {
        Layout.fillWidth: true
        title: "Benchmark TTS local"
        ColumnLayout {
            width: parent.width
            spacing: 10
            BodyText {
                text: panel.controller.ttsBenchmarkStatus.length > 0 ? panel.controller.ttsBenchmarkStatus : "Chưa chạy benchmark."
                font.weight: panel.theme.sectionWeight
                Layout.fillWidth: true
            }
            MutedText {
                text: "Kết quả chọn số luồng TTS local; không bảo đảm hết khựng trong game."
                Layout.fillWidth: true
            }
            MutedText {
                text: "Mục tiêu p95 câu ngắn: không quá 1.000 ms. Nếu máy không đạt, ứng dụng chọn mức nhanh nhất đã đo thành công."
                Layout.fillWidth: true
            }
            MutedText {
                visible: panel.controller.ttsBenchmarkStale
                text: "Cấu hình giọng, GPU hoặc runtime đã đổi. Hãy chạy lại để cập nhật kết quả."
                Layout.fillWidth: true
            }
            MutedText {
                visible: !panel.controller.ttsBenchmarkAllowed && !panel.controller.ttsBenchmarkRunning
                text: panel.controller.ttsBenchmarkUnavailableReason
                Layout.fillWidth: true
            }
            RowLayout {
                Layout.fillWidth: true
                ActionButton {
                    id: ttsBenchmarkButton
                    objectName: "ttsBenchmarkButton"
                    text: panel.controller.ttsBenchmarkRunning ? "Hủy benchmark" : "Chạy benchmark"
                    enabled: panel.live && (panel.controller.ttsBenchmarkRunning || panel.controller.ttsBenchmarkAllowed)
                    primaryAction: !panel.controller.ttsBenchmarkRunning && panel.controller.ttsBenchmarkAllowed
                    onClicked: panel.controller.ttsBenchmarkRunning ? panel.controller.cancelTtsBenchmark() : panel.controller.startTtsBenchmark()
                }
                MutedText {
                    visible: panel.controller.ttsBenchmarkTimestamp.length > 0
                    text: "Đo lúc " + panel.controller.ttsBenchmarkTimestamp
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignVCenter
                }
            }
            ProgressBar {
                objectName: "ttsBenchmarkProgress"
                Layout.fillWidth: true
                Layout.preferredHeight: 8
                visible: panel.live && panel.controller.ttsBenchmarkRunning
                value: Math.max(0, Math.min(1, panel.controller.ttsBenchmarkProgress))
            }
            GridLayout {
                Layout.fillWidth: true
                columns: 4
                columnSpacing: 12
                rowSpacing: 4
                BodyText { text: "Luồng"; font.weight: panel.theme.sectionWeight; Layout.preferredWidth: 50 }
                BodyText { text: "p95 tổng hợp (ms)"; font.weight: panel.theme.sectionWeight; Layout.preferredWidth: 130; wrapMode: Text.Wrap }
                BodyText { text: "CPU TB (ms)"; font.weight: panel.theme.sectionWeight; Layout.preferredWidth: 100; wrapMode: Text.Wrap }
                BodyText { text: "RAM đỉnh (WS / riêng), MB"; font.weight: panel.theme.sectionWeight; Layout.fillWidth: true; wrapMode: Text.Wrap }
                Repeater {
                    model: panel.controller.ttsBenchmarkResults
                    delegate: RowLayout {
                        required property var modelData
                        Layout.columnSpan: 4
                        Layout.fillWidth: true
                        spacing: 12
                        BodyText {
                            text: String(modelData.threads) + (panel.complete && !panel.controller.ttsBenchmarkStale && modelData.threads === panel.controller.ttsCpuThreads ? " ✓" : "")
                            font.family: panel.theme.monoFont
                            font.weight: panel.theme.monoWeight
                            Layout.preferredWidth: 50
                        }
                        BodyText { text: Number(modelData.p95_ms).toFixed(0); font.family: panel.theme.monoFont; font.weight: panel.theme.monoWeight; Layout.preferredWidth: 130 }
                        BodyText { text: Number(modelData.cpu_ms).toFixed(0); font.family: panel.theme.monoFont; font.weight: panel.theme.monoWeight; Layout.preferredWidth: 100 }
                        MutedText { text: (Number(modelData.peak_working_set) / 1048576).toFixed(0) + " / " + (Number(modelData.peak_private_bytes) / 1048576).toFixed(0); font.family: panel.theme.monoFont; font.weight: panel.theme.monoWeight; Layout.fillWidth: true }
                    }
                }
            }
            MutedText {
                visible: panel.controller.ttsBenchmarkResults.length === 0
                text: "Kết quả sẽ xuất hiện sau khi hoàn thành lượt đo 2, 3 và 4 luồng."
                Layout.fillWidth: true
            }
        }
    }
}
