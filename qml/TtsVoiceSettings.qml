pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: settings
    objectName: "ttsVoiceSettings"
    required property var controller
    required property UiTheme theme
    required property bool live
    property string contentMode: "all" // all, local, google
    signal backendSelectionChanged(string backendName)
    signal addGoogleVoiceRequested()
    readonly property bool controlsAvailable: live && !controller.ttsBenchmarkRunning && !controller.ttsBenchmarkRestoring

    spacing: 12
    Layout.fillWidth: true
    enabled: controlsAvailable

    component BodyText: Text {
        color: settings.theme.text
        font.family: settings.theme.uiFont
        font.pixelSize: settings.theme.pageTextSize(14)
        font.weight: settings.theme.uiWeight
        wrapMode: Text.Wrap
        textFormat: Text.PlainText
    }
    component MutedText: BodyText { color: settings.theme.muted; font.pixelSize: settings.theme.pageTextSize(13) }
    component ActionButton: Button {
        id: control
        property bool primaryAction: false
        implicitHeight: 44
        implicitWidth: Math.max(120, contentItem.implicitWidth + leftPadding + rightPadding)
        leftPadding: 18; rightPadding: 18
        topPadding: 10; bottomPadding: 10
        font.family: settings.theme.uiFont
        font.pixelSize: settings.theme.pageTextSize(14)
        font.weight: settings.theme.sectionWeight
        Accessible.name: text
        contentItem: Text {
            text: control.text
            color: !control.enabled ? settings.theme.muted : control.primaryAction ? settings.theme.accentText : settings.theme.text
            font: control.font
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            color: !control.enabled ? settings.theme.canvas : control.primaryAction ? settings.theme.accent : control.hovered || control.down ? settings.theme.raised : settings.theme.surface
            border.color: control.activeFocus ? settings.theme.outline : control.enabled ? settings.theme.outline : settings.theme.divider
            border.width: control.activeFocus ? 2 : 1
            radius: 3
        }
    }
    component VoiceComboBox: ComboBox {
        id: combo
        implicitHeight: 40
        font.family: settings.theme.uiFont
        font.pixelSize: settings.theme.pageTextSize(14)
        font.weight: settings.theme.uiWeight
        leftPadding: 12
        rightPadding: 34
        contentItem: Text { text: combo.displayText; color: combo.enabled ? settings.theme.text : settings.theme.muted; font: combo.font; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
        indicator: Text { text: "⌄"; color: combo.enabled ? settings.theme.muted : settings.theme.divider; font.family: settings.theme.uiFont; font.pixelSize: settings.theme.pageTextSize(18); font.weight: settings.theme.uiWeight; anchors.right: parent.right; anchors.rightMargin: 10; anchors.verticalCenter: parent.verticalCenter }
        background: Rectangle { color: combo.enabled ? settings.theme.canvas : settings.theme.surface; border.color: combo.activeFocus ? settings.theme.outline : combo.enabled ? settings.theme.outline : settings.theme.divider; border.width: combo.activeFocus ? 2 : 1; radius: 3 }
        delegate: ItemDelegate {
            id: voiceDelegate
            required property var modelData
            width: combo.width
            height: 36
            contentItem: Text { text: combo.textRole.length > 0 ? modelData[combo.textRole] : modelData; color: voiceDelegate.enabled ? settings.theme.text : settings.theme.muted; font: combo.font; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
            background: Rectangle {
                color: highlighted ? settings.theme.raised : settings.theme.surface
                border.color: voiceDelegate.activeFocus ? settings.theme.outline : "transparent"
                border.width: voiceDelegate.activeFocus ? 2 : 0
            }
        }
        popup: Popup {
            y: combo.height + 3
            width: combo.width
            implicitHeight: Math.min(contentItem.implicitHeight + 10, 260)
            padding: 5
            contentItem: ListView { clip: true; implicitHeight: contentHeight; model: combo.popup.visible ? combo.delegateModel : null; currentIndex: combo.highlightedIndex; ScrollIndicator.vertical: ScrollIndicator {} }
            background: Rectangle { color: settings.theme.surface; border.color: settings.theme.outline; border.width: 1; radius: 3 }
        }
    }
    component Field: TextField {
        id: field
        implicitHeight: 40
        leftPadding: 12
        rightPadding: 12
        color: field.enabled ? settings.theme.text : settings.theme.muted
        font.family: settings.theme.uiFont
        font.pixelSize: settings.theme.pageTextSize(14)
        font.weight: settings.theme.uiWeight
        background: Rectangle { color: field.enabled ? settings.theme.canvas : settings.theme.surface; border.color: field.activeFocus ? settings.theme.outline : field.enabled ? settings.theme.outline : settings.theme.divider; border.width: field.activeFocus ? 2 : 1; radius: 3 }
    }
    component Toggle: Switch {
        id: control
        implicitWidth: 44
        implicitHeight: 25
        Accessible.name: control.text
        contentItem: Text { text: control.text; color: control.enabled ? settings.theme.text : settings.theme.muted; font.family: settings.theme.uiFont; font.pixelSize: settings.theme.pageTextSize(14); font.weight: settings.theme.uiWeight; verticalAlignment: Text.AlignVCenter }
        indicator: Rectangle {
            x: control.width - width
            y: (control.height - height) / 2
            width: 42
            height: 22
            radius: height / 2
            color: !control.enabled ? settings.theme.surface : control.checked ? settings.theme.accent : settings.theme.raised
            border.color: control.activeFocus ? settings.theme.outline : control.enabled ? settings.theme.outline : settings.theme.divider
            border.width: control.activeFocus ? 2 : 1
            Rectangle { x: control.checked ? parent.width - width - 3 : 3; y: 3; width: 14; height: 14; radius: 7; color: !control.enabled ? settings.theme.muted : control.checked ? settings.theme.accentText : settings.theme.text }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        BodyText { text: "Backend giọng kỹ sư"; font.weight: settings.theme.sectionWeight; Layout.fillWidth: true }
        VoiceComboBox {
            id: ttsBackendCombo
            objectName: "ttsBackendCombo"
            Layout.minimumWidth: 160
            Layout.preferredWidth: 320
            Layout.maximumWidth: 370
            model: ["VieNeu local", "Google Gemini API"]
            currentIndex: settings.controller.ttsBackend === "Google Gemini API" ? 1 : 0
            Accessible.name: "Backend giọng kỹ sư"
            onActivated: function(index) {
                var backendName = index === 1 ? "Google Gemini API" : "VieNeu-TTS"
                settings.controller.setTtsBackend(backendName)
                settings.backendSelectionChanged(backendName)
            }
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        visible: settings.controller.ttsBackend === "VieNeu-TTS" && (settings.contentMode === "all" || settings.contentMode === "local")
        spacing: 10
        RowLayout {
            Layout.fillWidth: true
            BodyText { text: "Giọng VieNeu"; font.weight: settings.theme.sectionWeight; Layout.fillWidth: true }
            VoiceComboBox {
                id: localVoiceCombo
                objectName: "localVoiceCombo"
                Layout.minimumWidth: 160
                Layout.preferredWidth: 320
                Layout.maximumWidth: 370
                textRole: "name"
                model: settings.controller.availableTtsVoices
                currentIndex: {
                    for (let i = 0; i < settings.controller.availableTtsVoices.length; ++i) {
                        if (settings.controller.availableTtsVoices[i].id === settings.controller.selectedTtsVoice) return i
                    }
                    return 0
                }
                Accessible.name: "Giọng Kỹ sư AI (VieNeu)"
                onActivated: function(index) {
                    settings.controller.setTtsVoice(settings.controller.availableTtsVoices[index].id)
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            ActionButton { objectName: "previewLocalTtsButton"; text: "Nghe thử"; enabled: settings.controlsAvailable; onClicked: settings.controller.previewTtsVoice() }
            MutedText { text: "Phát câu kiểm tra khi bạn bấm nút."; Layout.fillWidth: true }
        }
        RowLayout {
            Layout.fillWidth: true
            BodyText { text: "Luồng CPU TTS"; font.weight: settings.theme.sectionWeight; Layout.fillWidth: true }
            VoiceComboBox {
                id: ttsCpuThreadsCombo
                objectName: "ttsCpuThreadsCombo"
                Layout.preferredWidth: 140
                model: ["2", "3", "4"]
                currentIndex: model.indexOf(String(settings.controller.ttsCpuThreads))
                enabled: settings.live && !settings.controller.connected && !settings.controller.ttsBenchmarkRunning
                    && !settings.controller.ttsBenchmarkRestoring && !settings.controller.ttsExternalThreadOverride
                    && settings.controller.voiceStatus === "Idle"
                Accessible.name: "Luồng CPU TTS"
                onActivated: settings.controller.setTtsCpuThreads(Number(currentText))
            }
        }
        MutedText { text: "Áp dụng ngay; tùy chọn gồm 2, 3 hoặc 4 luồng CPU."; Layout.fillWidth: true }
        BodyText { text: settings.controller.ttsStatus; Layout.fillWidth: true }
    }

    ColumnLayout {
        Layout.fillWidth: true
        visible: settings.controller.ttsBackend === "Google Gemini API" && (settings.contentMode === "all" || settings.contentMode === "google")
        spacing: 10
        RowLayout {
            Layout.fillWidth: true
            BodyText { text: "Mô hình"; font.weight: settings.theme.sectionWeight; Layout.fillWidth: true }
            VoiceComboBox {
                id: googleTtsModelCombo
                objectName: "googleTtsModelCombo"
                Layout.minimumWidth: 160
                Layout.preferredWidth: 320
                Layout.maximumWidth: 370
                model: ["gemini-3.8-flash-lite-tts", "gemini-3.8-flash-tts"]
                currentIndex: settings.controller.googleTtsModel === "gemini-3.8-flash-tts" ? 1 : 0
                Accessible.name: "Mô hình Google TTS"
                onActivated: settings.controller.setGoogleTtsModel(currentText)
            }
        }
        RowLayout {
            Layout.fillWidth: true
            BodyText { text: "Giọng Google"; font.weight: settings.theme.sectionWeight; Layout.fillWidth: true }
            VoiceComboBox {
                id: googleTtsVoiceCombo
                objectName: "googleTtsVoiceCombo"
                Layout.minimumWidth: 160
                Layout.preferredWidth: 320
                Layout.maximumWidth: 370
                model: settings.controller.googleTtsVoices
                currentIndex: Math.max(0, settings.controller.googleTtsVoices.indexOf(settings.controller.googleTtsVoice))
                Accessible.name: "Giọng Google"
                onActivated: settings.controller.setGoogleTtsVoice(currentText)
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Field {
                id: googleTtsApiKey
                objectName: "googleTtsApiKey"
                Layout.fillWidth: true
                placeholderText: settings.controller.googleTtsApiKeyConfigured ? "Đã lưu key; nhập để thay thế" : "Google AI Studio API key"
                echoMode: TextInput.Password
                Accessible.name: "Google AI Studio API key"
                onAccepted: { settings.controller.setGoogleTtsApiKey(text); clear() }
            }
            ActionButton {
                objectName: "saveGoogleTtsKeyButton"
                text: "Lưu key"
                onClicked: { settings.controller.setGoogleTtsApiKey(googleTtsApiKey.text); googleTtsApiKey.clear() }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            ActionButton { objectName: "previewGoogleTtsButton"; text: "Nghe thử"; primaryAction: true; onClicked: settings.controller.previewTtsVoice() }
            ActionButton { objectName: "addGoogleVoiceButton"; text: "Thêm voice"; onClicked: settings.addGoogleVoiceRequested() }
            Item { Layout.fillWidth: true }
        }
        MutedText {
            text: "Google Gemini TTS có thể phát sinh phí API. Chỉ nghe thử khi bạn bấm nút."
            Layout.fillWidth: true
        }
        BodyText { text: settings.controller.ttsStatus; Layout.fillWidth: true }
        MutedText {
            visible: settings.controller.googleTtsFirstAudioMs >= 0
            text: "Lượt gần nhất — audio đầu tiên: " + (settings.controller.googleTtsFirstAudioMs / 1000).toFixed(2) + " giây"
            Layout.fillWidth: true
        }
    }

    BodyText {
        visible: settings.contentMode !== "all" && settings.controller.ttsBackend === "Google Gemini API" && settings.contentMode === "local"
        text: "Đang chọn Google Gemini API. Chọn tab Google TTS để cấu hình giọng này."
        color: settings.theme.muted
        Layout.fillWidth: true
    }
    BodyText {
        visible: settings.contentMode !== "all" && settings.controller.ttsBackend === "VieNeu-TTS" && settings.contentMode === "google"
        text: "Đang chọn VieNeu local. Chọn tab Giọng cục bộ để cấu hình giọng này."
        color: settings.theme.muted
        Layout.fillWidth: true
    }
}
