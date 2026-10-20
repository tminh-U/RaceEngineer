pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    objectName: "setupWizard"
    required property var controller
    required property UiTheme theme
    anchors.fill: parent
    readonly property bool setupActive: controller.setupVisible
    readonly property int currentStep: Math.max(0, Math.min(3, Number(controller.setupStep)))
    signal openGoogleVoiceDialogRequested()

    visible: setupActive
    onSetupActiveChanged: if (setupActive) llmForm.resetDraft()

    function finishSetupWizard() {
        if (controller.ttsBenchmarkRunning)
            controller.cancelTtsBenchmark()
        controller.finishSetup()
    }

    function continueSetup() {
        if (currentStep === 2)
            llmForm.saveConfiguration()
        if (currentStep === 3) {
            finishSetupWizard()
            return
        }
        controller.setSetupStep(currentStep + 1)
    }

    component BodyText: Text {
        color: root.theme.text
        font.family: root.theme.uiFont
        font.pixelSize: root.theme.pageTextSize(14)
        font.weight: root.theme.uiWeight
        wrapMode: Text.Wrap
        textFormat: Text.PlainText
    }
    component MutedText: BodyText { color: root.theme.muted; font.pixelSize: root.theme.pageTextSize(13) }
    component HeadingText: BodyText {
        font.family: root.theme.displayFont
        font.pixelSize: root.theme.pageTextSize(26)
        font.weight: root.theme.displayWeight
    }
    component SurfaceCard: Rectangle {
        id: card
        default property alias content: body.data
        property string title: ""
        color: root.theme.surface
        border.color: root.theme.divider
        border.width: 1
        radius: 4
        implicitHeight: body.implicitHeight + 32
        Column {
            id: body
            anchors.fill: parent
            anchors.margins: 16
            spacing: 12
            BodyText { visible: card.title.length > 0; text: card.title; font.weight: root.theme.sectionWeight; font.pixelSize: root.theme.pageTextSize(15) }
            Rectangle { visible: card.title.length > 0; width: parent.width; height: 1; color: root.theme.divider }
        }
    }
    component ActionButton: Button {
        id: control
        property bool primaryAction: false
        implicitHeight: 44
        implicitWidth: Math.max(120, contentItem.implicitWidth + leftPadding + rightPadding)
        leftPadding: 18; rightPadding: 18
        topPadding: 10; bottomPadding: 10
        font.family: root.theme.uiFont
        font.pixelSize: root.theme.pageTextSize(14)
        font.weight: root.theme.sectionWeight
        Accessible.name: text
        contentItem: Text {
            text: control.text
            color: !control.enabled ? root.theme.muted : control.primaryAction ? root.theme.accentText : root.theme.text
            font: control.font
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            color: !control.enabled ? root.theme.canvas : control.primaryAction ? root.theme.accent : control.hovered || control.down ? root.theme.raised : root.theme.surface
            border.color: control.activeFocus ? root.theme.outline : control.enabled ? root.theme.outline : root.theme.divider
            border.width: control.activeFocus ? 2 : 1
            radius: 3
        }
    }
    component Field: TextField {
        id: field
        implicitHeight: 40
        leftPadding: 12
        rightPadding: 12
        color: field.enabled ? root.theme.text : root.theme.muted
        font.family: root.theme.uiFont
        font.pixelSize: root.theme.pageTextSize(14)
        font.weight: root.theme.uiWeight
        selectByMouse: true
        background: Rectangle {
            color: field.enabled ? root.theme.canvas : root.theme.surface
            border.color: field.activeFocus ? root.theme.outline : field.enabled ? root.theme.outline : root.theme.divider
            border.width: field.activeFocus ? 2 : 1
            radius: 3
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "#000000"
        opacity: 0.76
        MouseArea { anchors.fill: parent }
    }
    Rectangle {
        id: setupPanel
        objectName: "setupPanel"
        width: Math.min(1060, root.width - 28)
        height: Math.min(720, root.height - 24)
        anchors.centerIn: parent
        color: root.theme.surface
        border.color: root.theme.divider
        border.width: 1
        radius: 4

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 10

            RowLayout {
                Layout.fillWidth: true
                HeadingText { text: "Thiết lập ban đầu"; Layout.fillWidth: true }
                ActionButton {
                    objectName: "setupSkipButton"
                    text: "Bỏ qua thiết lập"
                    onClicked: root.finishSetupWizard()
                }
            }
            ProgressBar {
                id: setupProgress
                objectName: "setupProgress"
                Layout.fillWidth: true
                Layout.minimumHeight: 8
                Layout.preferredHeight: 8
                from: 0
                to: 1
                value: (root.currentStep + 1) / 4
                background: Rectangle { color: root.theme.raised; radius: 4 }
                contentItem: Item {
                    Rectangle {
                        width: parent.width * setupProgress.visualPosition
                        height: parent.height
                        radius: 4
                        color: root.theme.accent
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 4
                Repeater {
                    model: ["Chào mừng", "Giọng kỹ sư", "Kết nối LLM", "Tối ưu máy"]
                    delegate: BodyText {
                        required property int index
                        required property string modelData
                        Layout.fillWidth: true
                        text: modelData
                        color: index === root.currentStep ? root.theme.text : root.theme.muted
                        font.pixelSize: root.theme.pageTextSize(12)
                        font.weight: root.theme.uiWeight
                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideRight
                    }
                }
            }
            ScrollView {
                id: setupContentScroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 0
                Layout.preferredHeight: 0
                clip: true
                contentWidth: availableWidth
                contentHeight: setupStepContent.implicitHeight
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                StackLayout {
                    id: setupStepContent
                    objectName: "setupStepContent"
                    width: setupContentScroll.availableWidth
                    height: implicitHeight
                    currentIndex: root.currentStep

                    Item {
                        objectName: "setupWelcomeStep"
                        implicitHeight: welcomeColumn.implicitHeight
                        ColumnLayout {
                            id: welcomeColumn
                            width: parent.width
                            spacing: 12
                            HeadingText { text: "Chào mừng đến với RaceEngineer"; Layout.fillWidth: true }
                            MutedText {
                                text: "Thiết lập giọng kỹ sư, kết nối AI và số luồng TTS. Sau khi hoàn tất, mở Hệ thống & thiết bị để kiểm tra AC companion / ACC UDP trước khi vào track. Bạn có thể bỏ qua hoặc đổi lại sau."
                                Layout.fillWidth: true
                            }
                            SurfaceCard {
                                Layout.fillWidth: true
                                title: "Tên tay đua"
                                Field {
                                    id: setupDriverName
                                    objectName: "setupDriverName"
                                    width: parent.width
                                    text: root.controller.driverName
                                    placeholderText: "Nhập tên gọi trên radio"
                                    Accessible.name: "Tên tay đua"
                                    onEditingFinished: root.controller.setDriverName(text)
                                }
                            }
                        }
                    }
                    Item {
                        objectName: "setupVoiceStep"
                        implicitHeight: voiceColumn.implicitHeight
                        ColumnLayout {
                            id: voiceColumn
                            width: parent.width
                            spacing: 12
                            MutedText {
                                text: "Chọn backend và giọng kỹ sư. Âm thanh chỉ phát khi bạn bấm Nghe thử."
                                Layout.fillWidth: true
                            }
                            SurfaceCard {
                                Layout.fillWidth: true
                                title: "Giọng kỹ sư"
                                TtsVoiceSettings {
                                    id: setupVoiceSettings
                                    width: parent.width
                                    controller: root.controller
                                    theme: root.theme
                                    live: root.visible && root.currentStep === 1
                                    contentMode: "all"
                                    onAddGoogleVoiceRequested: root.openGoogleVoiceDialogRequested()
                                }
                            }
                        }
                    }
                    Item {
                        objectName: "setupLlmStep"
                        implicitHeight: llmColumn.implicitHeight
                        ColumnLayout {
                            id: llmColumn
                            width: parent.width
                            spacing: 12
                            MutedText {
                                text: "Nhập endpoint, API key và model để kiểm tra kết nối. Bạn vẫn có thể tiếp tục nếu chưa sẵn sàng hoặc máy chủ chưa phản hồi."
                                Layout.fillWidth: true
                            }
                            SurfaceCard {
                                Layout.fillWidth: true
                                title: "Kết nối LLM"
                                LlmConnectionForm {
                                    id: llmForm
                                    width: parent.width
                                    controller: root.controller
                                    theme: root.theme
                                    live: root.visible && root.currentStep === 2
                                }
                            }
                            ActionButton {
                                objectName: "setupLaterButton"
                                text: "Thiết lập sau"
                                onClicked: root.controller.setSetupStep(3)
                            }
                        }
                    }
                    Item {
                        objectName: "setupOptimizationStep"
                        implicitHeight: optimizationColumn.implicitHeight
                        ColumnLayout {
                            id: optimizationColumn
                            width: parent.width
                            spacing: 12
                            HeadingText { text: "Tối ưu số luồng TTS"; Layout.fillWidth: true }
                            MutedText {
                                text: "Đo VieNeu trên máy này để chọn số luồng CPU. Kết quả giúp chọn cấu hình TTS; không bảo đảm hết khựng trong game."
                                Layout.fillWidth: true
                            }
                            TtsBenchmarkPanel {
                                objectName: "setupTtsBenchmarkPanel"
                                Layout.fillWidth: true
                                visible: root.controller.ttsBackend === "VieNeu-TTS"
                                controller: root.controller
                                theme: root.theme
                                live: root.visible && root.currentStep === 3 && visible
                            }
                            SurfaceCard {
                                Layout.fillWidth: true
                                visible: root.controller.ttsBackend === "Google Gemini API"
                                title: "TTS Google"
                                BodyText {
                                    text: "Không cần benchmark TTS local khi bạn chọn Google Gemini API."
                                    Layout.fillWidth: true
                                }
                            }
                        }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                ActionButton {
                    objectName: "setupBackButton"
                    text: "Quay lại"
                    visible: root.currentStep > 0
                    enabled: !root.controller.ttsBenchmarkRunning
                    onClicked: root.controller.setSetupStep(Math.max(0, root.currentStep - 1))
                }
                Item { Layout.fillWidth: true }
                ActionButton {
                    objectName: "setupContinueButton"
                    primaryAction: true
                    text: root.currentStep === 3 ? "Hoàn tất" : "Tiếp tục"
                    enabled: !root.controller.ttsBenchmarkRunning
                    onClicked: root.continueSetup()
                }
            }
        }
    }

}
