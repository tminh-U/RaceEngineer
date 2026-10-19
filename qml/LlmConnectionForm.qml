pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: form
    objectName: "llmConnectionForm"
    required property var controller
    required property UiTheme theme
    required property bool live
    property bool wideLayout: false
    property bool streamingSelection: controller.apiStreaming
    property bool reasoningSelection: controller.apiReasoning
    property real temperatureSelection: controller.apiTemperature
    property int maximumTokensSelection: controller.apiMaximumTokens
    property var lastSyncedSnapshot: null
    readonly property bool hasUnsavedDraft: lastSyncedSnapshot !== null && (endpointField.text.trim() !== lastSyncedSnapshot.endpoint.trim()
        || modelField.text.trim() !== lastSyncedSnapshot.model.trim()
        || keyField.text !== lastSyncedSnapshot.key
        || streamingSelection !== lastSyncedSnapshot.streaming
        || Math.abs(temperatureSelection - lastSyncedSnapshot.temperature) > 0.01
        || maximumTokensSelection !== lastSyncedSnapshot.maximumTokens)
    readonly property bool isModified: endpointField.text.trim() !== controller.apiBaseUrl.trim()
        || modelField.text.trim() !== controller.apiModel.trim()
        || keyField.text !== controller.apiKey
        || streamingSelection !== controller.apiStreaming
        || reasoningSelection !== controller.apiReasoning
        || Math.abs(temperatureSelection - controller.apiTemperature) > 0.01
        || maximumTokensSelection !== controller.apiMaximumTokens
    readonly property string statusSummary: {
        if (!controller.apiConfigured) return "Chưa cấu hình"
        switch (controller.apiState) {
        case "Connected": return "Đã kết nối"
        case "Requesting": return "Đang gửi yêu cầu"
        case "RateLimited": return "Bị giới hạn tần suất"
        case "AuthenticationError": return "Lỗi xác thực"
        case "LLM Server Offline": return "Máy chủ ngoại tuyến"
        case "ProviderError": return "Lỗi API"
        default: return "Chưa kiểm tra kết nối"
        }
    }
    readonly property string statusDetail: {
        var detail = String(controller.apiDetail)
        if (detail === "Configured; connection not tested yet") return "Chưa kiểm tra kết nối tới máy chủ."
        if (detail === "Base URL and model are required" || detail === "Base URL model required") return "Cần nhập địa chỉ máy chủ và tên mô hình."
        if (controller.apiState === "Connected" && ["Model available", "Connected", "Connected — model available"].indexOf(detail) >= 0)
            return "Máy chủ đã phản hồi; mô hình khả dụng."
        return detail
    }

    function saveConfiguration() {
        controller.saveAiSettings("OpenAI Compatible", endpointField.text, keyField.text, modelField.text,
            streamingSelection, controller.apiTimeoutMilliseconds, maximumTokensSelection,
            temperatureSelection, reasoningSelection)
    }
    function testConnection() {
        if (live && isModified && controller.apiState !== "Requesting")
            controller.testApiConnection(endpointField.text, keyField.text, modelField.text)
    }
    function resetDraft() {
        endpointField.text = String(controller.apiBaseUrl)
        modelField.text = String(controller.apiModel)
        keyField.text = String(controller.apiKey)
        streamingSelection = Boolean(controller.apiStreaming)
        reasoningSelection = Boolean(controller.apiReasoning)
        temperatureSelection = Number(controller.apiTemperature)
        maximumTokensSelection = Number(controller.apiMaximumTokens)
        lastSyncedSnapshot = controllerSnapshot()
    }
    function controllerSnapshot() {
        return {
            endpoint: String(controller.apiBaseUrl),
            model: String(controller.apiModel),
            key: String(controller.apiKey),
            streaming: Boolean(controller.apiStreaming),
            reasoning: Boolean(controller.apiReasoning),
            temperature: Number(controller.apiTemperature),
            maximumTokens: Number(controller.apiMaximumTokens)
        }
    }

    Component.onCompleted: resetDraft()

    Connections {
        target: form.controller
        function onApiSettingsChanged() {
            const preserveDraft = form.hasUnsavedDraft
            form.reasoningSelection = Boolean(form.controller.apiReasoning)
            if (!preserveDraft)
                form.resetDraft()
            else
                form.lastSyncedSnapshot = form.controllerSnapshot()
        }
    }

    spacing: 10
    Layout.fillWidth: true
    Layout.minimumWidth: 0

    component LabelText: Text {
        color: form.theme.text
        font.family: form.theme.uiFont
        font.pixelSize: form.theme.pageTextSize(14)
        font.weight: form.theme.uiWeight
        wrapMode: Text.Wrap
    }
    component HintText: LabelText { color: form.theme.muted; font.pixelSize: form.theme.pageTextSize(13) }
    component Field: TextField {
        id: field
        implicitHeight: 40
        leftPadding: 12
        rightPadding: 12
        color: field.enabled ? form.theme.text : form.theme.muted
        font.family: form.theme.uiFont
        font.pixelSize: form.theme.pageTextSize(14)
        font.weight: form.theme.uiWeight
        selectByMouse: true
        background: Rectangle {
            color: field.enabled ? form.theme.canvas : form.theme.surface
            border.color: field.activeFocus ? form.theme.outline : field.enabled ? form.theme.outline : form.theme.divider
            border.width: field.activeFocus ? 2 : 1
            radius: 3
        }
    }
    component ActionButton: Button {
        id: control
        property bool primaryAction: false
        implicitHeight: 44
        implicitWidth: Math.max(120, contentItem.implicitWidth + leftPadding + rightPadding)
        leftPadding: 18; rightPadding: 18
        topPadding: 10; bottomPadding: 10
        font.family: form.theme.uiFont
        font.pixelSize: form.theme.pageTextSize(14)
        font.weight: form.theme.sectionWeight
        Accessible.name: text
        contentItem: Text {
            text: control.text
            color: !control.enabled ? form.theme.muted : control.primaryAction ? form.theme.accentText : form.theme.text
            font: control.font
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            color: !control.enabled ? form.theme.canvas : control.primaryAction ? form.theme.accent : control.hovered || control.down ? form.theme.raised : form.theme.surface
            border.color: control.activeFocus ? form.theme.outline : control.enabled ? form.theme.outline : form.theme.divider
            border.width: control.activeFocus ? 2 : 1
            radius: 3
        }
    }
    component Toggle: Switch {
        id: control
        Accessible.name: control.text
        implicitWidth: 44
        implicitHeight: 25
        contentItem: Text { text: control.text; color: control.enabled ? form.theme.text : form.theme.muted; font.family: form.theme.uiFont; font.pixelSize: form.theme.pageTextSize(14); font.weight: form.theme.uiWeight; verticalAlignment: Text.AlignVCenter }
        indicator: Rectangle {
            x: control.width - width
            y: (control.height - height) / 2
            width: 42
            height: 22
            radius: height / 2
            color: !control.enabled ? form.theme.surface : control.checked ? form.theme.accent : form.theme.raised
            border.color: control.activeFocus ? form.theme.outline : control.enabled ? form.theme.outline : form.theme.divider
            border.width: control.activeFocus ? 2 : 1
            Rectangle { x: control.checked ? parent.width - width - 3 : 3; y: 3; width: 14; height: 14; radius: 7; color: !control.enabled ? form.theme.muted : control.checked ? form.theme.accentText : form.theme.text }
        }
    }
    component ValueSlider: Slider {
        id: slider
        implicitWidth: 100
        implicitHeight: 32
        background: Rectangle {
            x: slider.leftPadding
            y: slider.topPadding + slider.availableHeight / 2 - height / 2
            width: slider.availableWidth
            height: 5
            radius: height / 2
            color: slider.enabled ? form.theme.raised : form.theme.surface
            border.color: slider.enabled ? form.theme.outline : form.theme.divider
            border.width: 1
            Rectangle { width: slider.visualPosition * parent.width; height: parent.height; radius: parent.radius; color: slider.enabled ? form.theme.accent : form.theme.muted }
        }
        handle: Rectangle {
            x: slider.leftPadding + slider.visualPosition * (slider.availableWidth - width)
            y: slider.topPadding + slider.availableHeight / 2 - height / 2
            width: 18
            height: 18
            radius: 9
            color: slider.enabled ? form.theme.text : form.theme.muted
            border.color: slider.activeFocus ? form.theme.outline : slider.enabled ? form.theme.outline : form.theme.divider
            border.width: slider.activeFocus ? 2 : 1
        }
    }

    GridLayout {
        id: formColumns
        objectName: "llmFormColumns"
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        columns: form.wideLayout ? 2 : 1
        columnSpacing: 24
        rowSpacing: 18

        ColumnLayout {
            objectName: "apiCredentialsColumn"
            Layout.column: 0
            Layout.row: 0
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.preferredWidth: 1
            spacing: 10

            Rectangle {
                objectName: "apiStatusStrip"
                Layout.fillWidth: true
                implicitHeight: statusContent.implicitHeight + 18
                color: form.theme.raised
                border.color: form.theme.divider
                border.width: 1
                radius: 3
                ColumnLayout {
                    id: statusContent
                    anchors.fill: parent
                    anchors.margins: 9
                    spacing: 4
                    RowLayout {
                        Layout.fillWidth: true
                        Rectangle { width: 8; height: 8; radius: 4; color: form.theme.text; Layout.alignment: Qt.AlignVCenter }
                        LabelText {
                            objectName: "apiStatusSummaryLabel"
                            text: "API: " + form.statusSummary + "   ·   " + (form.controller.apiModel.length > 0 ? form.controller.apiModel : "Chưa chọn mô hình")
                            font.weight: form.theme.sectionWeight
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }
                    }
                    HintText { objectName: "apiStatusDetailLabel"; text: form.statusDetail; Layout.fillWidth: true }
                }
            }

            LabelText { text: "Cổng Endpoint API" }
            Field { id: endpointField; objectName: "apiEndpointField"; Layout.fillWidth: true; text: form.controller.apiBaseUrl; enabled: form.live; Accessible.name: "Cổng Endpoint API" }
            HintText { text: "Địa chỉ endpoint của API (ví dụ: http://localhost:8080/v1)."; Layout.fillWidth: true }
            LabelText { text: "Tên mô hình" }
            Field { id: modelField; objectName: "apiModelField"; Layout.fillWidth: true; text: form.controller.apiModel; enabled: form.live; Accessible.name: "Mô hình suy luận" }
            HintText { text: "Tên mô hình sẽ được sử dụng."; Layout.fillWidth: true }
            LabelText { text: "Khóa API" }
            Field { id: keyField; objectName: "apiKeyField"; Layout.fillWidth: true; text: form.controller.apiKey; echoMode: TextInput.Password; enabled: form.live; Accessible.name: "Khóa API" }
            HintText { text: "Nhập khóa API. Khóa sẽ được lưu an toàn trên thiết bị này."; Layout.fillWidth: true }
            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                ActionButton {
                    objectName: "testLlmConnectionButton"
                    text: form.controller.apiState === "Requesting" ? "Đang kết nối..." : "Kiểm tra kết nối"
                    enabled: form.live && form.isModified && form.controller.apiState !== "Requesting"
                    primaryAction: form.isModified
                    onClicked: form.testConnection()
                }
                ActionButton {
                    objectName: "saveLlmConfigurationButton"
                    text: "Lưu cấu hình"
                    primaryAction: !form.isModified
                    enabled: form.live
                    onClicked: form.saveConfiguration()
                }
            }
        }

        ColumnLayout {
            objectName: "apiParametersColumn"
            Layout.column: form.wideLayout ? 1 : 0
            Layout.row: form.wideLayout ? 0 : 1
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.preferredWidth: 1
            spacing: 12

            RowLayout {
                Layout.fillWidth: true
                LabelText { text: "Hỗ trợ gọi hàm (Tool Calling)"; Layout.fillWidth: true }
                LabelText { text: "Luôn bật" }
            }
            RowLayout {
                Layout.fillWidth: true
                LabelText { text: "Truyền dữ liệu dạng luồng (Streaming)"; Layout.fillWidth: true }
                Toggle {
                    objectName: "apiStreamingToggle"
                    text: ""
                    checked: form.streamingSelection
                    enabled: form.live
                    Accessible.name: "Truyền dữ liệu dạng luồng (Streaming)"
                    onToggled: form.streamingSelection = checked
                }
            }
            RowLayout {
                Layout.fillWidth: true
                LabelText { text: "Chế độ suy luận chuyên sâu"; Layout.fillWidth: true }
                Toggle {
                    objectName: "apiReasoningToggle"
                    text: ""
                    checked: form.reasoningSelection
                    enabled: form.live
                    Accessible.name: "Chế độ suy luận chuyên sâu"
                    onToggled: {
                        form.reasoningSelection = checked
                        form.controller.setApiReasoning(checked)
                    }
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                RowLayout {
                    Layout.fillWidth: true
                    LabelText { text: "Độ ngẫu nhiên (Temperature)"; Layout.fillWidth: true }
                    LabelText { text: form.temperatureSelection.toFixed(2); font.family: form.theme.monoFont; font.weight: form.theme.monoWeight }
                }
                ValueSlider {
                    id: temperatureSlider
                    objectName: "apiTemperatureSlider"
                    Layout.fillWidth: true
                    Layout.minimumHeight: 32
                    Layout.preferredHeight: 32
                    from: 0
                    to: 1
                    value: form.temperatureSelection
                    enabled: form.live
                    Accessible.name: "Độ ngẫu nhiên (Temperature)"
                    onMoved: form.temperatureSelection = value
                    onValueChanged: if (activeFocus) form.temperatureSelection = value
                }
                RowLayout {
                    Layout.fillWidth: true
                    HintText { text: "0,00"; Layout.fillWidth: true }
                    HintText { text: "1,00"; Layout.fillWidth: true; horizontalAlignment: Text.AlignRight }
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                RowLayout {
                    Layout.fillWidth: true
                    LabelText { text: "Số token xuất tối đa"; Layout.fillWidth: true }
                    LabelText { text: String(form.maximumTokensSelection); font.family: form.theme.monoFont; font.weight: form.theme.monoWeight }
                }
                ValueSlider {
                    id: maximumTokensSlider
                    objectName: "apiMaximumTokensSlider"
                    Layout.fillWidth: true
                    Layout.minimumHeight: 32
                    Layout.preferredHeight: 32
                    from: 16
                    to: 128
                    stepSize: 1
                    value: form.maximumTokensSelection
                    enabled: form.live
                    Accessible.name: "Số token xuất tối đa"
                    onMoved: form.maximumTokensSelection = Math.round(value)
                    onValueChanged: if (activeFocus) form.maximumTokensSelection = Math.round(value)
                }
                RowLayout {
                    Layout.fillWidth: true
                    HintText { text: "16"; Layout.fillWidth: true }
                    HintText { text: "128"; Layout.fillWidth: true; horizontalAlignment: Text.AlignRight }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                LabelText { text: "Bộ nhớ ngữ cảnh hội thoại"; Layout.fillWidth: true }
                LabelText { text: "4 lượt"; font.family: form.theme.monoFont; font.weight: form.theme.monoWeight }
            }
            RowLayout {
                Layout.fillWidth: true
                LabelText { text: "Thời gian chờ phản hồi"; Layout.fillWidth: true }
                LabelText { text: form.controller.apiTimeoutMilliseconds + " ms"; font.family: form.theme.monoFont; font.weight: form.theme.monoWeight }
            }
        }
    }
}
