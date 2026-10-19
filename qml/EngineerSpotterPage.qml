pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: page
    objectName: "engineerSpotterPage"
    required property var controller
    required property UiTheme theme
    required property bool live
    readonly property bool compact: width < 1120
    signal openAiVoiceRequested()

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
    component ReadonlyRow: Item {
        id: readonlyRow
        property string title: ""
        property string subtitle: ""
        property string value: ""
        property string valueObjectName: ""
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        implicitHeight: Math.max(64, rowContent.implicitHeight + 16)
        RowLayout {
            id: rowContent
            anchors.fill: parent
            spacing: 16
            ColumnLayout {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.preferredWidth: 3
                BodyText { text: readonlyRow.title; font.weight: page.theme.sectionWeight; Layout.fillWidth: true }
                MutedText { text: readonlyRow.subtitle; Layout.fillWidth: true }
            }
            BodyText {
                objectName: readonlyRow.valueObjectName
                text: readonlyRow.value
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.preferredWidth: 2
                horizontalAlignment: Text.AlignRight
                wrapMode: Text.WrapAnywhere
            }
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
            spacing: 18
            HeadingText { text: "Kỹ sư & Spotter"; Layout.fillWidth: true }
            GridLayout {
                id: workspaceColumns
                objectName: "engineerWorkspaceColumns"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                columns: page.compact ? 1 : 2
                columnSpacing: 18
                rowSpacing: 18
                ColumnLayout {
                    objectName: "engineerProfileColumn"
                    Layout.column: 0
                    Layout.row: 0
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.preferredWidth: 1
                    Layout.alignment: Qt.AlignTop
                    spacing: 14
                    SurfaceCard {
                        objectName: "engineerProfileCard"
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        title: "Kỹ sư đua xe AI"
                        ColumnLayout {
                            width: parent.width
                            spacing: 12
                            BodyText { text: "Tên gọi tay đua"; font.weight: page.theme.sectionWeight }
                            TextField {
                                id: driverNameField
                                objectName: "driverNameField"
                                Layout.fillWidth: true
                                text: page.controller.driverName
                                placeholderText: "Nhập tên..."
                                color: enabled ? page.theme.text : page.theme.muted
                                font.family: page.theme.uiFont
                                font.pixelSize: page.theme.pageTextSize(14)
                                font.weight: page.theme.uiWeight
                                Accessible.name: "Tên gọi của tay đua"
                                background: Rectangle { color: driverNameField.enabled ? page.theme.canvas : page.theme.surface; border.color: driverNameField.activeFocus ? page.theme.outline : driverNameField.enabled ? page.theme.outline : page.theme.divider; border.width: driverNameField.activeFocus ? 2 : 1; radius: 3 }
                                onEditingFinished: page.controller.setDriverName(text)
                            }
                            MutedText { text: "Cách Kỹ sư AI xưng hô với bạn qua radio."; Layout.fillWidth: true }
                            BodyText { text: "Phong cách phản hồi"; font.weight: page.theme.sectionWeight; Layout.topMargin: 4 }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 6
                                Repeater {
                                    model: ["Tối giản", "Tiêu chuẩn", "Chi tiết"]
                                    delegate: ActionButton {
                                        required property string modelData
                                        objectName: "responseStyle" + modelData.replace(" ", "") + "Button"
                                        Layout.fillWidth: true
                                        text: modelData
                                        highlighted: page.controller.responseStyle === modelData
                                        primaryAction: page.controller.responseStyle === modelData
                                        enabled: page.live
                                        onClicked: page.controller.setResponseStyle(modelData)
                                    }
                                }
                            }
                        }
                    }
                    SurfaceCard {
                        objectName: "engineerAlertsCard"
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        title: "Cảnh báo kỹ sư"
                        ColumnLayout {
                            width: parent.width
                            spacing: 0
                            ToggleRow {
                                Layout.fillWidth: true
                                title: "Nhiên liệu"
                                subtitle: "Cảnh báo khi nhiên liệu sắp hết."
                                switchObjectName: "fuelAlertsToggle"
                                checked: page.controller.fuelAlertsEnabled
                                onToggled: value => page.controller.setFuelAlertsEnabled(value)
                            }
                            ToggleRow {
                                Layout.fillWidth: true
                                title: "Lốp quá nóng"
                                subtitle: "Cảnh báo khi nhiệt độ lốp vượt ngưỡng."
                                switchObjectName: "tyreAlertsToggle"
                                checked: page.controller.tyreAlertsEnabled
                                onToggled: value => page.controller.setTyreAlertsEnabled(value)
                            }
                            ToggleRow {
                                Layout.fillWidth: true
                                title: "Delta vòng & vòng tốt nhất"
                                subtitle: "Thông báo delta so với vòng tham chiếu và vòng tốt nhất."
                                switchObjectName: "lapDeltaToggle"
                                checked: page.controller.lapDeltaEnabled
                                onToggled: value => page.controller.setLapDeltaEnabled(value)
                            }
                            ToggleRow {
                                Layout.fillWidth: true
                                title: "Cờ hiệu & nguy hiểm"
                                subtitle: "Cảnh báo cờ hiệu và các tình huống nguy hiểm trên đường đua."
                                switchObjectName: "flagAlertsToggle"
                                checked: page.controller.flagAlertsEnabled
                                onToggled: value => page.controller.setFlagAlertsEnabled(value)
                            }
                            ToggleRow {
                                Layout.fillWidth: true
                                title: "Hư hại"
                                subtitle: "Khi telemetry ghi nhận mức hư hại tăng; ưu tiên trước phản hồi AI."
                                switchObjectName: "damageAlertsToggle"
                                checked: page.controller.damageAlertsEnabled
                                onToggled: value => page.controller.setDamageAlertsEnabled(value)
                            }
                            ToggleRow {
                                Layout.fillWidth: true
                                title: "Tổng kết sau mỗi vòng"
                                subtitle: page.controller.lapSummaryStatus
                                switchObjectName: "lapSummaryToggle"
                                checked: page.controller.lapSummaryEnabled
                                onToggled: value => page.controller.setLapSummaryEnabled(value)
                            }
                            BodyText {
                                visible: page.live && page.controller.latestLapSummary.length > 0
                                text: page.controller.latestLapSummary
                                color: page.theme.muted
                                font.pixelSize: page.theme.pageTextSize(12)
                                Layout.fillWidth: true
                                Layout.topMargin: 8
                            }
                        }
                    }
                }
                ColumnLayout {
                    objectName: "spotterColumn"
                    Layout.column: page.compact ? 0 : 1
                    Layout.row: page.compact ? 1 : 0
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.preferredWidth: 1
                    Layout.alignment: Qt.AlignTop
                    spacing: 14
                    SurfaceCard {
                        objectName: "spotterCard"
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        title: "Spotter"
                        ColumnLayout {
                            width: parent.width
                            spacing: 10
                            ToggleRow {
                                Layout.fillWidth: true
                                title: "Xe trái / xe phải"
                                subtitle: page.controller.spotterStatus
                                switchObjectName: "spotterEnabledToggle"
                                checked: page.controller.spotterEnabled
                                onToggled: value => page.controller.setSpotterEnabled(value)
                            }
                            MutedText {
                                text: "Cần dữ liệu hình học bánh xe để cảnh báo khoảng cách."
                                Layout.fillWidth: true
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                BodyText { text: "Khoảng cách cảnh báo ngang"; Layout.fillWidth: true }
                                Text { text: Number(page.controller.spotterWarningGapMeters).toFixed(1) + " m"; color: page.theme.text; font.family: page.theme.monoFont; font.pixelSize: page.theme.pageTextSize(15); font.weight: page.theme.monoWeight }
                            }
                            Slider {
                                id: spotterGapSlider
                                objectName: "spotterGapSlider"
                                implicitWidth: 100
                                implicitHeight: 32
                                Layout.fillWidth: true
                                Layout.minimumHeight: 32
                                Layout.preferredHeight: 32
                                from: 0.5
                                to: 2.5
                                stepSize: 0.1
                                value: page.controller.spotterWarningGapMeters
                                enabled: page.live
                                Accessible.name: "Khoảng cách cảnh báo Spotter"
                                onPressedChanged: if (!pressed) page.controller.setSpotterWarningGapMeters(value)
                                background: Rectangle {
                                    x: spotterGapSlider.leftPadding
                                    y: spotterGapSlider.topPadding + spotterGapSlider.availableHeight / 2 - height / 2
                                    width: spotterGapSlider.availableWidth
                                    height: 5
                                    radius: height / 2
                                    color: spotterGapSlider.enabled ? page.theme.raised : page.theme.surface
                                    border.color: spotterGapSlider.enabled ? page.theme.outline : page.theme.divider
                                    border.width: 1
                                    Rectangle { width: spotterGapSlider.visualPosition * parent.width; height: parent.height; radius: parent.radius; color: spotterGapSlider.enabled ? page.theme.accent : page.theme.muted }
                                }
                                handle: Rectangle {
                                    x: spotterGapSlider.leftPadding + spotterGapSlider.visualPosition * (spotterGapSlider.availableWidth - width)
                                    y: spotterGapSlider.topPadding + spotterGapSlider.availableHeight / 2 - height / 2
                                    width: 18
                                    height: 18
                                    radius: 9
                                    color: spotterGapSlider.enabled ? page.theme.text : page.theme.muted
                                    border.color: spotterGapSlider.activeFocus ? page.theme.outline : spotterGapSlider.enabled ? page.theme.outline : page.theme.divider
                                    border.width: spotterGapSlider.activeFocus ? 2 : 1
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                MutedText { text: "0,5 m"; Layout.fillWidth: true }
                                MutedText { text: "2,5 m"; horizontalAlignment: Text.AlignRight; Layout.fillWidth: true }
                            }
                        }
                    }
                    SurfaceCard {
                        objectName: "pttCard"
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        title: "PTT (Push-to-Talk)"
                        ColumnLayout {
                            width: parent.width
                            spacing: 0
                            ReadonlyRow { width: parent.width; title: "Phím PTT trên bàn phím"; subtitle: "Giữ phím để nói với kỹ sư."; value: "Ctrl+Space" }
                            ToggleRow {
                                Layout.fillWidth: true
                                title: "PTT bàn phím"
                                subtitle: "Ctrl+Space"
                                switchObjectName: "engineerKeyboardPttToggle"
                                checked: page.controller.keyboardPttEnabled
                                onToggled: value => page.controller.setPushToTalkOptions(value, page.controller.directInputPttEnabled)
                            }
                            ToggleRow {
                                Layout.fillWidth: true
                                title: "PTT DirectInput"
                                subtitle: "Vô lăng hoặc tay cầm"
                                switchObjectName: "engineerDirectInputPttToggle"
                                checked: page.controller.directInputPttEnabled
                                onToggled: value => page.controller.setPushToTalkOptions(page.controller.keyboardPttEnabled, value)
                            }
                            ReadonlyRow { width: parent.width; valueObjectName: "engineerDirectInputBinding"; title: "Nút đang gán"; subtitle: "Nhấn giữ trên thiết bị DirectInput để nói."; value: page.controller.directInputBinding }
                            ActionButton {
                                objectName: "engineerMapDirectInputButton"
                                text: "Gán lại nút"
                                enabled: page.live
                                Layout.topMargin: 8
                                onClicked: page.controller.beginDirectInputMapping()
                            }
                            MutedText { text: page.directInputStatusText(); Layout.fillWidth: true }
                        }
                    }
                    ActionButton {
                        objectName: "openAiVoicePageButton"
                        text: "Cấu hình giọng nói"
                        enabled: page.live
                        onClicked: page.openAiVoiceRequested()
                    }
                }
            }
        }
    }
}
