pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

Item {
    id: root
    objectName: "googleVoiceDialogHost"
    required property var controller
    required property UiTheme theme
    required property bool live

    function open() {
        if (!live) return
        voiceCreationMode.currentIndex = 0
        addGoogleVoiceDialog.open()
    }

    property alias dialog: addGoogleVoiceDialog

    component BodyText: Text {
        color: root.theme.text
        font.family: root.theme.uiFont
        font.pixelSize: root.theme.pageTextSize(14)
        font.weight: root.theme.uiWeight
        wrapMode: Text.Wrap
        textFormat: Text.PlainText
    }
    component MutedText: BodyText { color: root.theme.muted; font.pixelSize: root.theme.pageTextSize(13) }
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
        background: Rectangle { color: field.enabled ? root.theme.canvas : root.theme.surface; border.color: field.activeFocus ? root.theme.outline : field.enabled ? root.theme.outline : root.theme.divider; border.width: field.activeFocus ? 2 : 1; radius: 3 }
    }
    Connections {
        target: root
        function onLiveChanged() {
            if (!root.live && addGoogleVoiceDialog.visible) addGoogleVoiceDialog.close()
        }
    }

    Dialog {
        id: addGoogleVoiceDialog
        objectName: "googleVoiceDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        z: 3000
        modal: true
        title: "Thêm giọng Google"
        font.family: root.theme.uiFont
        font.pixelSize: root.theme.pageTextSize(14)
        implicitWidth: Math.min(620, Math.max(400, parent ? parent.width - 40 : 620))
        property string sourceWavPath: ""
        property string consentWavPath: ""
        standardButtons: Dialog.Ok | Dialog.Cancel
        palette.window: root.theme.surface
        palette.windowText: root.theme.text
        palette.button: root.theme.raised
        palette.buttonText: root.theme.text
        palette.base: root.theme.canvas
        palette.text: root.theme.text
        palette.highlight: root.theme.accent
        background: Rectangle { color: root.theme.surface; border.color: root.theme.outline; border.width: 1; radius: 4 }
        onAccepted: {
            if (!root.live) return
            if (voiceCreationMode.currentIndex === 0)
                root.controller.createGooglePromptedVoice(googleVoiceName.text, voiceDescription.text)
            else root.controller.createGoogleTtsVoice(googleVoiceName.text, sourceWavPath, consentWavPath)
        }

        contentItem: ScrollView {
            id: voiceDialogScroll
            implicitWidth: Math.min(570, addGoogleVoiceDialog.parent.width - 70)
            implicitHeight: Math.min(voiceDialogFields.implicitHeight, addGoogleVoiceDialog.parent.height - 210)
            contentWidth: availableWidth
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ColumnLayout {
                id: voiceDialogFields
                width: voiceDialogScroll.availableWidth
                spacing: 12
                TabBar {
                    id: voiceCreationMode
                    objectName: "googleVoiceModeTabs"
                    Layout.fillWidth: true
                    currentIndex: 0
                    background: Rectangle { color: root.theme.canvas; border.color: root.theme.outline; border.width: 1; radius: 3 }
                    TabButton {
                        id: promptVoiceTab
                        text: "Tạo bằng prompt"
                        Accessible.name: text
                        font.family: root.theme.uiFont
                        font.pixelSize: root.theme.pageTextSize(14)
                        font.weight: root.theme.uiWeight
                        contentItem: Text { text: promptVoiceTab.text; color: root.theme.text; font: promptVoiceTab.font; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                        background: Rectangle { color: promptVoiceTab.checked ? root.theme.raised : "transparent"; border.color: promptVoiceTab.activeFocus ? root.theme.outline : "transparent"; border.width: promptVoiceTab.activeFocus ? 2 : 0; Rectangle { visible: promptVoiceTab.checked; anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 2; color: root.theme.text } }
                    }
                    TabButton {
                        id: wavVoiceTab
                        text: "Từ WAV"
                        Accessible.name: text
                        font.family: root.theme.uiFont
                        font.pixelSize: root.theme.pageTextSize(14)
                        font.weight: root.theme.uiWeight
                        contentItem: Text { text: wavVoiceTab.text; color: root.theme.text; font: wavVoiceTab.font; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                        background: Rectangle { color: wavVoiceTab.checked ? root.theme.raised : "transparent"; border.color: wavVoiceTab.activeFocus ? root.theme.outline : "transparent"; border.width: wavVoiceTab.activeFocus ? 2 : 0; Rectangle { visible: wavVoiceTab.checked; anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 2; color: root.theme.text } }
                    }
                }
                ActionButton {
                    objectName: "raceEngineerVoicePromptButton"
                    Layout.fillWidth: true
                    visible: voiceCreationMode.currentIndex === 0
                    text: "Dùng prompt RaceEngineer (mặc định)"
                    onClicked: {
                        googleVoiceName.text = "RaceEngineer · Tiếng Việt"
                        voiceDescription.text = root.controller.bonoVoiceDescription()
                    }
                }
                BodyText { text: "Tên giọng"; Layout.fillWidth: true; font.weight: root.theme.sectionWeight }
                Field {
                    id: googleVoiceName
                    objectName: "googleVoiceName"
                    Layout.fillWidth: true
                    text: "RaceEngineer · Tiếng Việt"
                    maximumLength: 80
                    placeholderText: "Tên giọng"
                    Accessible.name: "Tên giọng"
                }
                BodyText { visible: voiceCreationMode.currentIndex === 0; text: "Mô tả giọng"; Layout.fillWidth: true; font.weight: root.theme.sectionWeight }
                TextArea {
                    id: voiceDescription
                    objectName: "googleVoiceDescription"
                    Layout.fillWidth: true
                    Layout.minimumHeight: 150
                    Layout.preferredHeight: Math.max(150, implicitHeight)
                    visible: voiceCreationMode.currentIndex === 0
                    text: root.controller.bonoVoiceDescription()
                    placeholderText: "Mô tả chất giọng, cách phát âm và nhịp nói"
                    wrapMode: TextEdit.Wrap
                    color: voiceDescription.enabled ? root.theme.text : root.theme.muted
                    font.family: root.theme.uiFont
                    font.pixelSize: root.theme.pageTextSize(14)
                    font.weight: root.theme.uiWeight
                    selectByMouse: true
                    Accessible.name: "Mô tả giọng"
                    background: Rectangle { color: voiceDescription.enabled ? root.theme.canvas : root.theme.surface; border.color: voiceDescription.activeFocus ? root.theme.outline : voiceDescription.enabled ? root.theme.outline : root.theme.divider; border.width: voiceDescription.activeFocus ? 2 : 1; radius: 3 }
                }
                BodyText { visible: voiceCreationMode.currentIndex === 1; text: "Chọn mẫu WAV"; Layout.fillWidth: true; font.weight: root.theme.sectionWeight }
                RowLayout {
                    Layout.fillWidth: true
                    visible: voiceCreationMode.currentIndex === 1
                    ActionButton { objectName: "sourceVoiceFileButton"; text: "Chọn mẫu WAV"; onClicked: sourceVoiceFileDialog.open() }
                    MutedText { objectName: "sourceVoicePath"; text: addGoogleVoiceDialog.sourceWavPath.length > 0 ? "Đã chọn mẫu WAV" : "Mẫu giọng 10–30 giây, giọng nói rõ ràng."; Layout.fillWidth: true }
                }
                BodyText { visible: voiceCreationMode.currentIndex === 1; text: "Chọn WAV đồng ý"; Layout.fillWidth: true; font.weight: root.theme.sectionWeight }
                RowLayout {
                    Layout.fillWidth: true
                    visible: voiceCreationMode.currentIndex === 1
                    ActionButton { objectName: "consentVoiceFileButton"; text: "Chọn WAV đồng ý"; onClicked: consentVoiceFileDialog.open() }
                    MutedText { objectName: "consentVoicePath"; text: addGoogleVoiceDialog.consentWavPath.length > 0 ? "Đã chọn bản ghi đồng ý" : "Phát câu đồng ý bằng chính giọng này."; Layout.fillWidth: true }
                }
                BodyText {
                    visible: voiceCreationMode.currentIndex === 1
                    text: "Định dạng WAV PCM mono 16-bit. Bản ghi đồng ý phải đọc chính xác: “Tôi là chủ sở hữu giọng nói này và tôi đồng cho Google sử dụng giọng nói này để tạo mô hình giọng nói tổng hợp.”"
                    Layout.fillWidth: true
                }
                MutedText {
                    text: "Bấm OK để tạo giọng qua Google API; có thể phát sinh phí. Tạo thành công sẽ lưu tên vào danh sách giọng và chọn giọng mới."
                    Layout.fillWidth: true
                }
            }
        }
    }

    FileDialog {
        id: sourceVoiceFileDialog
        objectName: "sourceVoiceFileDialog"
        title: "Chọn bản ghi giọng nói"
        fileMode: FileDialog.OpenFile
        nameFilters: ["WAV audio (*.wav)"]
        onAccepted: addGoogleVoiceDialog.sourceWavPath = selectedFile.toLocalFile()
    }
    FileDialog {
        id: consentVoiceFileDialog
        objectName: "consentVoiceFileDialog"
        title: "Chọn bản ghi đồng ý"
        fileMode: FileDialog.OpenFile
        nameFilters: ["WAV audio (*.wav)"]
        onAccepted: addGoogleVoiceDialog.consentWavPath = selectedFile.toLocalFile()
    }
}
