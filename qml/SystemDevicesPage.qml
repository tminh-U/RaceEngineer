pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: page
    objectName: "systemDevicesPage"
    required property var controller
    required property UiTheme theme
    required property bool live
    readonly property bool compact: width < 1120

    function directInputStatusText() {
        switch (controller.directInputStatus) {
        case "DirectInput not initialized": return "DirectInput chưa khởi tạo."
        case "Waiting for a DirectInput button…": return "Chờ nút DirectInput để gán."
        case "No DirectInput controller found": return "Chưa tìm thấy tay lái / bộ điều khiển."
        case "DirectInput ready": return "Đã sẵn sàng."
        case "DirectInput unavailable": return "DirectInput không khả dụng."
        case "DirectInput is only available on Windows": return "DirectInput chỉ khả dụng trên Windows."
        case "Map a DirectInput button first": return "Hãy gán nút DirectInput trước."
        case "Mapping cancelled": return "Đã hủy gán nút."
        case "Press and release wheel button…": return "Nhấn rồi thả nút trên vô lăng để gán."
        default: return String(controller.directInputStatus).replace(" — Button ", " — nút ")
        }
    }

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
    component SectionHeading: BodyText {
        font.family: page.theme.uiFont
        font.pixelSize: page.theme.pageTextSize(18)
        font.weight: page.theme.sectionWeight
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
            spacing: 10
            BodyText { visible: card.title.length > 0; text: card.title; font.weight: page.theme.sectionWeight; font.pixelSize: page.theme.pageTextSize(15) }
            Rectangle { visible: card.title.length > 0; width: parent.width; height: 1; color: page.theme.divider }
        }
    }
    component ActionButton: Button {
        id: control
        property bool primaryAction: false
        implicitHeight: 44
        implicitWidth: Math.max(120, contentItem.implicitWidth + leftPadding + rightPadding)
        leftPadding: 18; rightPadding: 18
        topPadding: 10; bottomPadding: 10
        font.family: page.theme.uiFont
        font.pixelSize: page.theme.pageTextSize(14)
        font.weight: page.theme.sectionWeight
        Accessible.name: text
        contentItem: Text {
            text: control.text
            color: !control.enabled ? page.theme.muted : control.primaryAction ? page.theme.accentText : page.theme.text
            font: control.font
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            color: !control.enabled ? page.theme.canvas : control.primaryAction ? page.theme.accent : control.hovered || control.down ? page.theme.raised : page.theme.surface
            border.width: control.activeFocus ? 2 : 1
            border.color: control.activeFocus ? page.theme.outline : control.enabled ? page.theme.outline : page.theme.divider
            radius: 3
        }
    }
    component SettingSwitch: Switch {
        id: control
        implicitWidth: 44
        implicitHeight: 25
        Accessible.name: control.text
        contentItem: Text {
            text: control.text
            visible: text.length > 0
            color: control.enabled ? page.theme.text : page.theme.muted
            font.family: page.theme.uiFont
            font.pixelSize: page.theme.pageTextSize(14)
            font.weight: page.theme.uiWeight
            verticalAlignment: Text.AlignVCenter
        }
        indicator: Rectangle {
            x: control.width - width
            y: (control.height - height) / 2
            width: 42
            height: 22
            radius: height / 2
            color: !control.enabled ? page.theme.surface : control.checked ? page.theme.accent : page.theme.raised
            border.color: control.activeFocus ? page.theme.outline : control.enabled ? page.theme.outline : page.theme.divider
            border.width: control.activeFocus ? 2 : 1
            Rectangle {
                x: control.checked ? parent.width - width - 3 : 3
                y: 3
                width: 14
                height: 14
                radius: 7
                color: !control.enabled ? page.theme.muted : control.checked ? page.theme.accentText : page.theme.text
            }
        }
    }
    component ToggleRow: Item {
        id: row
        property string title: ""
        property string subtitle: ""
        property string switchObjectName: ""
        property bool checked: false
        property bool controlEnabled: true
        signal toggled(bool checked)
        implicitHeight: Math.max(54, rowText.implicitHeight + 14)
        RowLayout {
            anchors.fill: parent
            spacing: 12
            ColumnLayout {
                id: rowText
                Layout.fillWidth: true
                BodyText { text: row.title; font.weight: page.theme.sectionWeight; Layout.fillWidth: true }
                MutedText { text: row.subtitle; Layout.fillWidth: true }
            }
            SettingSwitch {
                objectName: row.switchObjectName
                Accessible.name: row.title
                checked: row.checked
                enabled: page.live && row.controlEnabled
                onToggled: row.toggled(checked)
            }
        }
        Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 1; color: page.theme.divider }
    }
    component DeviceComboBox: ComboBox {
        id: combo
        implicitHeight: 38
        font.family: page.theme.uiFont
        font.pixelSize: page.theme.pageTextSize(14)
        font.weight: page.theme.uiWeight
        leftPadding: 12
        rightPadding: 34
        contentItem: Text {
            text: combo.displayText
            color: combo.enabled ? page.theme.text : page.theme.muted
            font: combo.font
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        indicator: Text { text: "⌄"; color: combo.enabled ? page.theme.muted : page.theme.divider; font.family: page.theme.uiFont; font.pixelSize: page.theme.pageTextSize(18); font.weight: page.theme.uiWeight; anchors.right: parent.right; anchors.rightMargin: 10; anchors.verticalCenter: parent.verticalCenter }
        background: Rectangle { color: combo.enabled ? page.theme.canvas : page.theme.surface; border.color: combo.activeFocus ? page.theme.outline : combo.enabled ? page.theme.outline : page.theme.divider; border.width: combo.activeFocus ? 2 : 1; radius: 3 }
        delegate: ItemDelegate {
            id: deviceDelegate
            required property var modelData
            width: combo.width
            height: 36
            contentItem: Text {
                text: combo.textRole.length > 0 ? modelData[combo.textRole] : modelData
                color: deviceDelegate.enabled ? page.theme.text : page.theme.muted
                font: combo.font
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
            background: Rectangle {
                color: highlighted ? page.theme.raised : page.theme.surface
                border.color: deviceDelegate.activeFocus ? page.theme.outline : "transparent"
                border.width: deviceDelegate.activeFocus ? 2 : 0
            }
        }
        popup: Popup {
            y: combo.height + 3
            width: combo.width
            implicitHeight: Math.min(contentItem.implicitHeight + 10, 260)
            padding: 5
            contentItem: ListView { clip: true; implicitHeight: contentHeight; model: combo.popup.visible ? combo.delegateModel : null; currentIndex: combo.highlightedIndex; ScrollIndicator.vertical: ScrollIndicator {} }
            background: Rectangle { color: page.theme.surface; border.color: page.theme.outline; border.width: 1; radius: 3 }
        }
    }
    component DeviceRow: Item {
        id: row
        property string title: ""
        property string subtitle: ""
        property string subtitleObjectName: ""
        property string value: ""
        default property alias controlContent: controlSlot.data
        implicitHeight: 68
        Column {
            anchors.left: parent.left
            anchors.right: controlSlot.left
            anchors.rightMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            spacing: 3
            BodyText { text: row.title; font.weight: page.theme.sectionWeight; width: parent.width }
            MutedText { objectName: row.subtitleObjectName; text: row.subtitle; width: parent.width }
        }
        BodyText {
            visible: row.value.length > 0
            text: row.value
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            horizontalAlignment: Text.AlignRight
        }
        Item {
            id: controlSlot
            width: row.value.length > 0 ? 0 : Math.min(390, parent.width * 0.52)
            height: 40
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
        }
        Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 1; color: page.theme.divider }
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
            spacing: 16
            HeadingText { text: "Hệ thống & thiết bị"; Layout.fillWidth: true }
            GridLayout {
                Layout.fillWidth: true
                columns: page.compact ? 1 : 2
                columnSpacing: 18
                rowSpacing: 18
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    spacing: 16
                    SurfaceCard {
                        Layout.fillWidth: true
                        title: "Windows & giao diện"
                        ColumnLayout {
                            width: parent.width
                            spacing: 0
                            ToggleRow {
                                Layout.fillWidth: true
                                title: "Khởi động cùng Windows"
                                subtitle: "Tự động mở RaceEngineer khi đăng nhập hệ thống."
                                switchObjectName: "startWithWindowsToggle"
                                checked: page.controller.startWithWindows
                                onToggled: value => page.controller.setStartWithWindows(value)
                            }
                            ToggleRow {
                                Layout.fillWidth: true
                                title: "Thu nhỏ vào khay hệ thống"
                                subtitle: page.controller.trayAvailable ? "Tiếp tục chạy ngầm trong phiên đua." : "Khay hệ thống hiện không khả dụng."
                                switchObjectName: "minimizeToTrayToggle"
                                checked: page.controller.minimizeToTray
                                controlEnabled: page.controller.trayAvailable
                                onToggled: value => page.controller.setMinimizeToTray(value)
                            }
                            ToggleRow {
                                Layout.fillWidth: true
                                title: "Thu nhỏ khi đóng cửa sổ"
                                subtitle: page.controller.trayAvailable ? "Giữ các dịch vụ đang hoạt động." : "Khay hệ thống hiện không khả dụng."
                                switchObjectName: "minimizeOnCloseToggle"
                                checked: page.controller.minimizeOnClose
                                controlEnabled: page.controller.trayAvailable
                                onToggled: value => page.controller.setMinimizeOnClose(value)
                            }
                            ToggleRow {
                                Layout.fillWidth: true
                                title: "Tăng tốc phần cứng (GPU)"
                                subtitle: page.controller.gpuRendererRestartRequired ? "Khởi động lại ứng dụng để áp dụng." : "Dùng GPU để vẽ giao diện Qt Quick."
                                switchObjectName: "gpuRendererToggle"
                                checked: page.controller.gpuRendererEnabled
                                onToggled: value => page.controller.setGpuRendererEnabled(value)
                            }
                        }
                    }
                    SurfaceCard {
                        Layout.fillWidth: true
                        title: "AI cục bộ"
                        ColumnLayout {
                            width: parent.width
                            spacing: 8
                            DeviceRow {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                title: "Thiết bị chạy model AI"
                                subtitle: "Thiết bị tính toán cho nhận dạng và tổng hợp giọng nói."
                                DeviceComboBox {
                                    id: aiComputeDeviceCombo
                                    objectName: "aiComputeDeviceCombo"
                                    anchors.fill: parent
                                    textRole: "label"
                                    model: page.controller.aiComputeDevices
                                    currentIndex: {
                                        for (let i = 0; i < page.controller.aiComputeDevices.length; ++i) {
                                            if (page.controller.aiComputeDevices[i].id === page.controller.selectedAiComputeDevice) return i
                                        }
                                        return 0
                                    }
                                    enabled: page.live
                                    onActivated: function(index) {
                                        page.controller.setAiComputeDevice(page.controller.aiComputeDevices[index].id)
                                    }
                                }
                            }
                            MutedText { text: page.controller.aiComputeStatus; color: page.theme.muted; Layout.fillWidth: true }
                            MutedText { text: "Thay đổi thiết bị suy luận sẽ có hiệu lực sau khi khởi động lại."; Layout.fillWidth: true }
                            ActionButton {
                                objectName: "openSetupButton"
                                text: "Chạy lại thiết lập ban đầu"
                                enabled: page.live
                                Layout.topMargin: 8
                                onClicked: page.controller.openSetup()
                            }
                        }
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    spacing: 16
                    SurfaceCard {
                        Layout.fillWidth: true
                        title: "Điều khiển PTT"
                        ColumnLayout {
                            width: parent.width
                            spacing: 0
                            ToggleRow {
                                Layout.fillWidth: true
                                title: "PTT bàn phím"
                                subtitle: "Nhấn và giữ Ctrl+Space để nói chuyện với kỹ sư."
                                switchObjectName: "keyboardPttToggle"
                                checked: page.controller.keyboardPttEnabled
                                onToggled: value => page.controller.setPushToTalkOptions(value, page.controller.directInputPttEnabled)
                            }
                            ToggleRow {
                                Layout.fillWidth: true
                                title: "PTT qua thiết bị DirectInput"
                                subtitle: "Sử dụng nút trên vô lăng hoặc thiết bị chơi."
                                switchObjectName: "directInputPttToggle"
                                checked: page.controller.directInputPttEnabled
                                onToggled: value => page.controller.setPushToTalkOptions(page.controller.keyboardPttEnabled, value)
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 16
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    BodyText { text: "Nút đang gán"; font.weight: page.theme.sectionWeight; Layout.fillWidth: true }
                                    BodyText {
                                        objectName: "systemDirectInputBinding"
                                        text: page.controller.directInputBinding || "Chưa gán"
                                        font.family: page.theme.monoFont
                                        Layout.fillWidth: true
                                        wrapMode: Text.WrapAnywhere
                                        Accessible.name: "Nút đang gán: " + text
                                    }
                                    MutedText { text: page.directInputStatusText(); Layout.fillWidth: true }
                                }
                                ActionButton {
                                    objectName: "mapDirectInputButton"
                                    text: "Gán lại"
                                    enabled: page.live
                                    onClicked: page.controller.beginDirectInputMapping()
                                }
                            }
                        }
                    }
                    SurfaceCard {
                        Layout.fillWidth: true
                        title: "Âm thanh"
                        ColumnLayout {
                            width: parent.width
                            spacing: 0
                            DeviceRow {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                title: "Thiết bị nhập (Microphone)"
                                subtitleObjectName: "microphoneNameLabel"
                                subtitle: page.live && page.controller.microphoneName.length > 0
                                    ? "Đang dùng: " + page.controller.microphoneName
                                    : "Chọn micro để nhận giọng nói."
                                DeviceComboBox {
                                    id: audioInputCombo
                                    objectName: "audioInputCombo"
                                    anchors.fill: parent
                                    textRole: "name"
                                    model: page.controller.audioInputDevices
                                    currentIndex: {
                                        for (let i = 0; i < page.controller.audioInputDevices.length; ++i) {
                                            if (page.controller.audioInputDevices[i].id === page.controller.selectedAudioInputId) return i
                                        }
                                        return 0
                                    }
                                    enabled: page.live
                                    onPressedChanged: if (pressed && page.live) page.controller.refreshAudioInputDevices()
                                    onActivated: function(index) {
                                        page.controller.setAudioInputDevice(page.controller.audioInputDevices[index].id)
                                    }
                                }
                            }
                            DeviceRow {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                title: "Thiết bị phát âm thanh mặc định"
                                subtitle: "Chọn thiết bị phát giọng nói của kỹ sư."
                                DeviceComboBox {
                                    id: audioOutputCombo
                                    objectName: "audioOutputCombo"
                                    anchors.fill: parent
                                    model: page.controller.audioOutputDevices
                                    currentIndex: Math.max(0, page.controller.audioOutputDevices.indexOf(page.controller.selectedAudioOutput))
                                    enabled: page.live
                                    onActivated: page.controller.setAudioOutputDevice(currentText)
                                }
                            }
                            ToggleRow {
                                Layout.fillWidth: true
                                title: "Tự động giảm âm lượng trò chơi"
                                subtitle: "Giảm âm game khi kỹ sư AI phát giọng nói (-12 dB)."
                                switchObjectName: "audioDuckingToggle"
                                checked: page.controller.audioDuckingEnabled
                                onToggled: value => page.controller.setAudioDuckingEnabled(value)
                            }
                            MutedText {
                                objectName: "isAudioDuckedStatus"
                                text: page.live && page.controller.isAudioDucked ? "Âm lượng game đang giảm khi radio phát." : "Âm lượng game không giảm."
                                Layout.fillWidth: true
                                Layout.bottomMargin: 6
                            }
                            Item {
                                Layout.fillWidth: true
                                implicitHeight: 58
                                ColumnLayout {
                                    anchors.fill: parent
                                    spacing: 5
                                    RowLayout {
                                        Layout.fillWidth: true
                                        BodyText { text: "Âm lượng radio kỹ sư"; Layout.fillWidth: true }
                                        Text { text: Math.round(page.controller.ttsVolume * 100) + "%"; color: page.theme.text; font.family: page.theme.monoFont; font.pixelSize: page.theme.pageTextSize(14); font.weight: page.theme.monoWeight }
                                    }
                                    Slider {
                                        id: radioVolumeSlider
                                        objectName: "radioVolumeSlider"
                                        implicitWidth: 100
                                        implicitHeight: 32
                                        Layout.fillWidth: true
                                        Layout.minimumHeight: 32
                                        Layout.preferredHeight: 32
                                        from: 0
                                        to: 1
                                        value: page.controller.ttsVolume
                                        enabled: page.live
                                        Accessible.name: "Âm lượng radio kỹ sư"
                                        onMoved: page.controller.setTtsVolume(value)
                                        background: Rectangle {
                                            x: radioVolumeSlider.leftPadding
                                            y: radioVolumeSlider.topPadding + radioVolumeSlider.availableHeight / 2 - height / 2
                                            width: radioVolumeSlider.availableWidth
                                            height: 5
                                            radius: height / 2
                                            color: radioVolumeSlider.enabled ? page.theme.raised : page.theme.surface
                                            border.color: radioVolumeSlider.enabled ? page.theme.outline : page.theme.divider
                                            border.width: 1
                                            Rectangle { width: radioVolumeSlider.visualPosition * parent.width; height: parent.height; radius: parent.radius; color: radioVolumeSlider.enabled ? page.theme.accent : page.theme.muted }
                                        }
                                        handle: Rectangle {
                                            x: radioVolumeSlider.leftPadding + radioVolumeSlider.visualPosition * (radioVolumeSlider.availableWidth - width)
                                            y: radioVolumeSlider.topPadding + radioVolumeSlider.availableHeight / 2 - height / 2
                                            width: 18
                                            height: 18
                                            radius: 9
                                            color: radioVolumeSlider.enabled ? page.theme.text : page.theme.muted
                                            border.color: radioVolumeSlider.activeFocus ? page.theme.outline : radioVolumeSlider.enabled ? page.theme.outline : page.theme.divider
                                            border.width: radioVolumeSlider.activeFocus ? 2 : 1
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            MutedText {
                objectName: "versionLabel"
                text: "RaceEngineer " + Qt.application.version
                Layout.fillWidth: true
                Layout.topMargin: 4
            }
        }
    }
}
