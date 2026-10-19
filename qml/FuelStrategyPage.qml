pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: page
    objectName: "fuelStrategyPage"
    required property var controller
    required property UiTheme theme
    required property bool live
    readonly property var telemetry: live ? controller.telemetry : ({})
    readonly property var inactiveStrategyData: ({
        enabled: false,
        simulatorConnected: false,
        savedRecords: 0,
        realisticSetupConfirmed: false,
        raceActive: false,
        busy: false,
        status: "",
        storagePath: "",
        sharingConfigured: false,
        sharingEnabled: false,
        sharingStatus: ""
    })
    readonly property var strategyData: live ? controller.strategyData : inactiveStrategyData
    readonly property bool strategyAvailable: live ? controller.strategyAvailable : false
    readonly property string strategyStatus: live ? controller.strategyStatus : ""
    readonly property string strategyDetail: live ? controller.strategyDetail : ""
    readonly property int strategyPitLap: live ? controller.strategyPitLap : -1
    readonly property bool strategyEnabled: live ? controller.strategyEnabled : false
    property int sectionIndex: 0
    readonly property bool compact: width < 980

    component BodyText: Text {
        color: page.theme.text
        font.family: page.theme.uiFont
        font.pixelSize: page.theme.pageTextSize(14)
        font.weight: page.theme.uiWeight
        wrapMode: Text.Wrap
        textFormat: Text.PlainText
    }
    component MutedText: BodyText { color: page.theme.muted; font.pixelSize: page.theme.pageTextSize(13) }
    component SectionTitle: BodyText {
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
        implicitWidth: 48
        implicitHeight: 26
        Accessible.name: control.text
        contentItem: Text {
            text: control.text
            visible: text.length > 0
            leftPadding: 0
            rightPadding: 10
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
    component Metric: Column {
        id: metric
        property string label: ""
        property string value: "—"
        property bool estimate: false
        Layout.preferredWidth: 1
        Layout.minimumWidth: 0
        spacing: 7
        BodyText { width: metric.width; text: metric.label; color: page.theme.muted; font.pixelSize: page.theme.pageTextSize(13); font.weight: page.theme.sectionWeight }
        Text {
            width: metric.width
            text: metric.value
            color: page.theme.text
            font.family: page.theme.displayFont
            font.pixelSize: page.theme.pageTextSize(page.compact ? 46 : 76)
            font.weight: page.theme.displayWeight
            wrapMode: Text.Wrap
        }
        MutedText { visible: metric.estimate; text: "Ước tính"; font.pixelSize: page.theme.pageTextSize(12) }
    }
    component TabButton: Button {
        id: control
        property bool selected: false
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
            Rectangle { visible: control.selected; anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 2; color: page.theme.text }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        ScrollView {
            id: pageScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            Column {
                width: pageScroll.availableWidth
                spacing: 16
                padding: 24
                SectionTitle { text: "Nhiên liệu & chiến thuật"; width: parent.width - 48 }
                Row {
                    width: parent.width - 48
                    spacing: 24
                    TabButton { text: "Tổng quan"; selected: page.sectionIndex === 0; onClicked: page.sectionIndex = 0 }
                    TabButton { text: "Dữ liệu & huấn luyện"; selected: page.sectionIndex === 1; onClicked: page.sectionIndex = 1 }
                    Rectangle { anchors.verticalCenter: parent.verticalCenter; width: Math.max(0, parent.width - 420); height: 1; color: page.theme.divider }
                }
                StackLayout {
                    width: parent.width - 48
                    currentIndex: page.sectionIndex
                    Item {
                        implicitHeight: overviewColumn.implicitHeight
                        Column {
                            id: overviewColumn
                            width: parent.width
                            spacing: 16
                            SurfaceCard {
                                width: parent.width
                                title: "Tính toán nhiên liệu"
                                RowLayout {
                                    width: parent.width
                                    spacing: 18
                                    Metric {
                                        Layout.fillWidth: true
                                        label: "Nhiên liệu còn lại"
                                        value: page.theme.number(page.telemetry.fuelLiters, 1, " L")
                                    }
                                    Rectangle { Layout.fillHeight: true; width: 1; color: page.theme.divider }
                                    Metric {
                                        Layout.fillWidth: true
                                        label: "Tiêu thụ trung bình"
                                        value: page.theme.number(page.telemetry.fuelPerLapLiters, 2, " L/vòng")
                                    }
                                    Rectangle { Layout.fillHeight: true; width: 1; color: page.theme.divider }
                                    Metric {
                                        Layout.fillWidth: true
                                        label: "Ước tính chạy thêm"
                                        estimate: true
                                        value: page.theme.number(page.telemetry.estimatedFuelLaps, 1, " vòng")
                                    }
                                }
                            }
                            SurfaceCard {
                                width: parent.width
                                title: "Trạng thái chiến thuật"
                                ColumnLayout {
                                    width: parent.width
                                    spacing: 20
                                    BodyText {
                                        text: page.strategyAvailable ? page.strategyStatus : "Chiến thuật pit chưa khả dụng"
                                        font.pixelSize: page.theme.pageTextSize(page.compact ? 22 : 32)
                                        font.weight: page.theme.sectionWeight
                                        Layout.fillWidth: true
                                    }
                                    MutedText {
                                        text: page.strategyAvailable ? page.strategyDetail : "Đang chờ dữ liệu chiến thuật đủ điều kiện để duyệt."
                                        Layout.fillWidth: true
                                    }
                                    Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: page.theme.divider }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 24
                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            BodyText { text: "Khuyến nghị vào pit"; color: page.theme.muted; font.pixelSize: page.theme.pageTextSize(14); Layout.fillWidth: true }
                                            Text {
                                                text: page.strategyAvailable && page.strategyPitLap > 0 ? "Vòng " + page.strategyPitLap : "—"
                                                color: page.theme.text
                                                font.family: page.theme.displayFont
                                                font.pixelSize: page.theme.pageTextSize(44)
                                                font.weight: page.theme.displayWeight
                                            }
                                        }
                                        ColumnLayout {
                                            objectName: "strategyToggleGroup"
                                            Layout.preferredWidth: 100
                                            Layout.minimumWidth: 92
                                            spacing: 5
                                            BodyText {
                                                objectName: "strategyToggleLabel"
                                                text: "Tự tính lại"
                                                font.pixelSize: page.theme.pageTextSize(14)
                                                Layout.fillWidth: true
                                            }
                                            SettingSwitch {
                                                id: strategySwitch
                                                objectName: "strategyToggle"
                                                Accessible.name: "Tự động tính lại chiến thuật Pit"
                                                text: ""
                                                checked: page.strategyAvailable && page.strategyEnabled
                                                enabled: page.live && page.controller.strategyAvailable
                                                Layout.alignment: Qt.AlignRight
                                                onToggled: page.controller.setStrategyEnabled(checked)
                                            }
                                        }
                                    }
                                }
                            }
                            ActionButton {
                                objectName: "openTrainingDataButton"
                                text: "Dữ liệu & huấn luyện →"
                                onClicked: page.sectionIndex = 1
                            }
                        }
                    }
                    Item {
                        implicitHeight: dataColumn.implicitHeight
                        RowLayout {
                            id: dataColumn
                            width: parent.width
                            spacing: 16
                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.preferredWidth: 1
                                Layout.alignment: Qt.AlignTop
                                spacing: 16
                                SurfaceCard {
                                    Layout.fillWidth: true
                                    title: "Ghi dữ liệu & huấn luyện"
                                    ColumnLayout {
                                        width: parent.width
                                        spacing: 10
                                        BodyText {
                                            text: (page.strategyData.enabled ? (page.strategyData.simulatorConnected ? "Đã bật ghi — chờ vòng đua / sự kiện pit." : "Đã bật ghi — chờ AC / ACC.") : "Đã tắt ghi dữ liệu.")
                                                + (page.live ? " Đã lưu " + page.strategyData.savedRecords + " bản ghi từ khi mở app." : "")
                                            Layout.fillWidth: true
                                        }
                                        RowLayout {
                                            Layout.fillWidth: true
                                            BodyText { text: "Record"; font.weight: page.theme.sectionWeight; Layout.fillWidth: true }
                                            SettingSwitch {
                                                objectName: "strategyRecordToggle"
                                                Accessible.name: "Bật hoặc tắt ghi dữ liệu"
                                                checked: page.strategyData.enabled
                                                enabled: page.live
                                                onToggled: page.strategyData.enabled = checked
                                            }
                                        }
                                        RowLayout {
                                            Layout.fillWidth: true
                                            BodyText { text: "Xác nhận cài đặt đua thực tế"; font.weight: page.theme.sectionWeight; Layout.fillWidth: true }
                                            SettingSwitch {
                                                objectName: "realisticSetupToggle"
                                                Accessible.name: "Xác nhận cài đặt đua thực tế"
                                                checked: page.strategyData.realisticSetupConfirmed
                                                enabled: page.live && !page.strategyData.raceActive
                                                onToggled: page.strategyData.realisticSetupConfirmed = checked
                                            }
                                        }
                                        MutedText {
                                            text: "Kiểm tra mức hao lốp, nhiên liệu và nhiệt độ trong game trước phiên Race. Cần xác nhận lại mỗi phiên."
                                            Layout.fillWidth: true
                                        }
                                        Flow {
                                            Layout.fillWidth: true
                                            spacing: 8
                                            ActionButton {
                                                objectName: "processRecordedDataButton"
                                                text: "Xử lý dữ liệu đã ghi"
                                                enabled: page.live && !page.strategyData.busy && !page.strategyData.simulatorConnected
                                                onClicked: page.strategyData.processData()
                                            }
                                            ActionButton {
                                                objectName: "trainPaceButton"
                                                text: "Train model pace"
                                                enabled: page.live && !page.strategyData.busy && !page.strategyData.simulatorConnected
                                                onClicked: page.strategyData.trainModel()
                                            }
                                            ActionButton {
                                                objectName: "trainTyresButton"
                                                text: "Train model lốp"
                                                enabled: page.live && !page.strategyData.busy && !page.strategyData.simulatorConnected
                                                onClicked: page.strategyData.trainTyreModels()
                                            }
                                            ActionButton {
                                                objectName: "cancelTrainingButton"
                                                text: "Dừng"
                                                visible: page.live && page.strategyData.busy
                                                enabled: page.live
                                                onClicked: page.strategyData.cancelJob()
                                            }
                                        }
                                        ProgressBar { Layout.fillWidth: true; Layout.preferredHeight: 8; indeterminate: true; visible: page.live && page.strategyData.busy }
                                        BodyText { text: page.strategyData.status; Layout.fillWidth: true }
                                        RowLayout {
                                            Layout.fillWidth: true
                                            ActionButton { objectName: "openStrategyDataButton"; text: "Mở thư mục dữ liệu"; enabled: page.live; onClicked: page.strategyData.openStorage() }
                                            ActionButton { objectName: "loggingGuideButton"; text: "Hướng dẫn thiết lập log"; enabled: page.live; onClicked: Qt.openUrlExternally("https://github.com/tminh-U/RaceEngineer/blob/main/docs/logging-guide.md") }
                                        }
                                        BodyText { text: "Nơi lưu: " + (page.live ? page.strategyData.storagePath : ""); Layout.fillWidth: true; wrapMode: Text.WrapAnywhere }
                                    }
                                }
                                MutedText {
                                    text: "Record lưu mỗi vòng và sự kiện pit khi AC / ACC kết nối. Process và train chỉ dùng phiên Race đã xác nhận cài đặt thực tế. Train pace dự đoán delta thời gian vòng kế tiếp; Train model lốp nghiên cứu thay đổi độ mòn và nhiệt độ lõi từ mẫu qua vạch hiện tại sang mẫu kế tiếp. Tầm hoạt động nhiên liệu vẫn tính tất định từ telemetry. Xử lý và train chạy khi ngắt kết nối, dùng một luồng CPU."
                                    Layout.fillWidth: true
                                }
                                MutedText {
                                    text: "Train pace và từng mục tiêu lốp cần tối thiểu 3 phiên đua và 60 cặp vòng cho mỗi nhóm xe trong từng simulator. Cả hai model đều không chọn vòng pit; việc chọn pit cần Ranker riêng cùng dữ liệu chiến thuật có nhãn."
                                    Layout.fillWidth: true
                                }
                            }
                            SurfaceCard {
                                Layout.fillWidth: true
                                Layout.preferredWidth: 0.8
                                Layout.alignment: Qt.AlignTop
                                title: "Chia sẻ dữ liệu (tùy chọn)"
                                ColumnLayout {
                                    width: parent.width
                                    spacing: 12
                                    BodyText { text: "Giúp cải thiện độ chính xác của các model trong tương lai."; Layout.fillWidth: true }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        BodyText { text: "Đồng ý tự gửi dữ liệu sau phiên đua"; Layout.fillWidth: true; font.weight: page.theme.sectionWeight }
                                        SettingSwitch {
                                            objectName: "sharingConsentToggle"
                                            Accessible.name: "Đồng ý tự gửi dữ liệu sau phiên đua"
                                            checked: page.strategyData.sharingEnabled
                                            enabled: page.live && (page.strategyData.sharingConfigured || page.strategyData.sharingEnabled)
                                            onToggled: page.strategyData.sharingEnabled = checked
                                        }
                                    }
                                    MutedText {
                                        text: page.strategyData.sharingConfigured
                                            ? "Mặc định tắt. Có thể rút lại đồng ý bất cứ lúc nào."
                                            : "Chưa có nơi nhận dữ liệu; tính năng tạm chưa sẵn sàng."
                                        Layout.fillWidth: true
                                    }
                                    BodyText {
                                        text: page.strategyData.sharingEnabled ? "Chia sẻ đang bật" : "Chia sẻ đang tắt"
                                        font.family: page.theme.uiFont
                                        font.pixelSize: page.theme.pageTextSize(16)
                                        font.weight: page.theme.sectionWeight
                                    }
                                    BodyText {
                                        text: page.strategyData.sharingStatus
                                        Layout.fillWidth: true
                                    }
                                    MutedText {
                                        text: "Chỉ tự gửi phiên Race đã xác nhận cài đặt thực tế: xe, đường đua, thời gian vòng, nhiên liệu, độ mòn và nhiệt độ lốp. Mã phiên được thay mới; mốc giờ được dời. Không gửi âm thanh, hội thoại hay khóa của dịch vụ AI. Dữ liệu đã gửi không tự thu hồi khi tắt chia sẻ."
                                        Layout.fillWidth: true
                                    }
                                    ActionButton {
                                        objectName: "retrySharingButton"
                                        text: "Gửi lại"
                                        visible: page.live && page.strategyData.sharingConfigured && page.strategyData.sharingEnabled
                                        enabled: page.live
                                        onClicked: page.strategyData.retrySharing()
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
