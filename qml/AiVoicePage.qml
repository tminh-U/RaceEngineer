pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls as Controls
import QtQuick.Layouts

Item {
    id: page
    objectName: "aiVoicePage"
    required property var controller
    required property UiTheme theme
    required property bool live
    readonly property bool compact: width < 1080
    property alias sectionIndex: sectionTabs.currentIndex
    signal openGoogleVoiceDialogRequested()

    component BodyText: Text {
        color: page.theme.text
        font.family: page.theme.uiFont
        font.pixelSize: page.theme.pageTextSize(14)
        font.weight: page.theme.uiWeight
        wrapMode: Text.Wrap
        textFormat: Text.PlainText
    }
    component MutedText: BodyText { color: page.theme.muted; font.pixelSize: page.theme.pageTextSize(13) }
    component HeadingText: BodyText {
        font.family: page.theme.displayFont
        font.pixelSize: page.theme.pageTextSize(page.compact ? 32 : 52)
        font.weight: page.theme.displayWeight
    }
    component SurfaceCard: Rectangle {
        id: card
        default property alias content: body.data
        property string title: ""
        color: page.theme.canvas
        border.color: page.theme.divider
        border.width: 1
        radius: 4
        implicitHeight: body.implicitHeight + 32
        Column {
            id: body
            anchors.fill: parent
            anchors.margins: 16
            spacing: 12
            BodyText { visible: card.title.length > 0; text: card.title; font.weight: page.theme.sectionWeight; font.pixelSize: page.theme.pageTextSize(15) }
            Rectangle { visible: card.title.length > 0; width: parent.width; height: 1; color: page.theme.divider }
        }
    }
    component TabButton: Controls.TabButton {
        id: control
        implicitHeight: 42
        font.family: page.theme.uiFont
        font.pixelSize: page.theme.pageTextSize(14)
        font.weight: page.theme.uiWeight
        contentItem: Text {
            text: control.text
            color: control.enabled ? page.theme.text : page.theme.muted
            font: control.font
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            color: "transparent"
            border.color: control.activeFocus ? page.theme.outline : "transparent"
            border.width: control.activeFocus ? 2 : 0
            Rectangle { visible: control.checked; anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 2; color: page.theme.text }
        }
    }

    ScrollView {
        id: pageScroll
        anchors.fill: parent
        clip: true
        contentWidth: availableWidth
        contentHeight: pageContent.y + pageContent.implicitHeight + 24
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ColumnLayout {
            id: pageContent
            x: 24
            y: 24
            width: pageScroll.availableWidth - 48
            spacing: 14
            HeadingText { text: "AI & giọng nói"; Layout.fillWidth: true }
            TabBar {
                id: sectionTabs
                objectName: "aiVoiceSectionTabs"
                Layout.fillWidth: true
                background: Rectangle { color: "transparent"; Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: page.theme.divider } }
                TabButton { text: "Kết nối AI"; Accessible.name: text }
                TabButton { text: "Giọng cục bộ"; Accessible.name: text }
                TabButton { text: "Google TTS"; Accessible.name: text }
            }
            StackLayout {
                objectName: "aiVoiceSectionContent"
                Layout.fillWidth: true
                currentIndex: page.sectionIndex
                Item {
                    implicitHeight: connectionColumn.implicitHeight
                    ColumnLayout {
                        id: connectionColumn
                        width: parent.width
                        spacing: 14
                        BodyText {
                            text: "Thiết lập kết nối đến mô hình ngôn ngữ qua API tương thích OpenAI."
                            Layout.fillWidth: true
                        }
                        SurfaceCard {
                            Layout.fillWidth: true
                            title: "Cấu hình kết nối"
                            LlmConnectionForm {
                                id: apiForm
                                width: parent.width
                                controller: page.controller
                                theme: page.theme
                                live: page.live && page.sectionIndex === 0
                                wideLayout: !page.compact
                            }
                        }
                    }
                }
                Item {
                    implicitHeight: localColumn.implicitHeight
                    ColumnLayout {
                        id: localColumn
                        width: parent.width
                        spacing: 14
                        RowLayout {
                            Layout.fillWidth: true
                            HeadingText { text: "Giọng cục bộ"; Layout.fillWidth: true }
                            BodyText { text: "VIE NEU"; font.family: page.theme.monoFont; font.weight: page.theme.monoWeight; color: page.theme.muted }
                        }
                        GridLayout {
                            Layout.fillWidth: true
                            columns: page.compact ? 1 : 2
                            columnSpacing: 16
                            rowSpacing: 16
                            SurfaceCard {
                                Layout.fillWidth: true
                                Layout.alignment: Qt.AlignTop
                                title: "Giọng kỹ sư"
                                ColumnLayout {
                                    width: parent.width
                                    BodyText { text: "Sử dụng VieNeu trên máy để tổng hợp giọng nói hướng dẫn của kỹ sư."; Layout.fillWidth: true }
                                    TtsVoiceSettings {
                                        id: localVoiceSettings
                                        width: parent.width
                                        controller: page.controller
                                        theme: page.theme
                                        live: page.live && page.sectionIndex === 1
                                        contentMode: "local"
                                        onBackendSelectionChanged: backendName => page.sectionIndex = backendName === "Google Gemini API" ? 2 : 1
                                        onAddGoogleVoiceRequested: page.openGoogleVoiceDialogRequested()
                                    }
                                }
                            }
                            TtsBenchmarkPanel {
                                Layout.fillWidth: true
                                Layout.alignment: Qt.AlignTop
                                controller: page.controller
                                theme: page.theme
                                live: page.live && page.sectionIndex === 1
                            }
                        }
                    }
                }
                Item {
                    implicitHeight: googleColumn.implicitHeight
                    ColumnLayout {
                        id: googleColumn
                        width: parent.width
                        spacing: 14
                        RowLayout {
                            Layout.fillWidth: true
                            HeadingText { text: "Google TTS"; Layout.fillWidth: true }
                            BodyText { text: "GEMINI"; font.family: page.theme.monoFont; font.weight: page.theme.monoWeight; color: page.theme.muted }
                        }
                        GridLayout {
                            Layout.fillWidth: true
                            columns: page.compact ? 1 : 2
                            columnSpacing: 16
                            rowSpacing: 16
                            SurfaceCard {
                                Layout.fillWidth: true
                                Layout.alignment: Qt.AlignTop
                                title: "Google Gemini Text-to-Speech"
                                ColumnLayout {
                                    width: parent.width
                                    BodyText { text: "Dùng Google Gemini Text-to-Speech để tạo giọng kỹ sư. Chỉ phát khi bạn bấm Nghe thử."; Layout.fillWidth: true }
                                    TtsVoiceSettings {
                                        id: googleVoiceSettings
                                        width: parent.width
                                        controller: page.controller
                                        theme: page.theme
                                        live: page.live && page.sectionIndex === 2
                                        contentMode: "google"
                                        onBackendSelectionChanged: backendName => page.sectionIndex = backendName === "Google Gemini API" ? 2 : 1
                                        onAddGoogleVoiceRequested: page.openGoogleVoiceDialogRequested()
                                    }
                                }
                            }
                            SurfaceCard {
                                Layout.fillWidth: true
                                Layout.alignment: Qt.AlignTop
                                title: "Trạng thái"
                                ColumnLayout {
                                    width: parent.width
                                    spacing: 10
                                    BodyText { text: page.controller.ttsStatus; font.family: page.theme.uiFont; font.pixelSize: page.theme.pageTextSize(16); font.weight: page.theme.sectionWeight; Layout.fillWidth: true }
                                    Rectangle { Layout.fillWidth: true; height: 1; color: page.theme.divider }
                                    MutedText { text: "Lượt gần nhất"; Layout.fillWidth: true }
                                    BodyText {
                                        visible: page.live && page.controller.googleTtsFirstAudioMs >= 0
                                        text: "Audio đầu tiên: " + (page.controller.googleTtsFirstAudioMs / 1000).toFixed(2) + " giây"
                                        Layout.fillWidth: true
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
