pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: window
    width: 1600
    height: 900
    minimumWidth: 980
    minimumHeight: 620
    visible: true
    title: "Race Engineer"
    color: theme.background

    Theme { id: theme }
    property int page: 0
    property var telemetry: app.telemetry
    property bool narrow: width < 1180

    function has(value) { return value !== undefined && value !== null }
    function value(key, digits, suffix) {
        const item = telemetry[key]
        return has(item) ? Number(item).toFixed(digits) + suffix : "—"
    }
    function whole(key, suffix) { return value(key, 0, suffix) }
    function lap(seconds) {
        if (!has(seconds)) return "—"
        const minutes = Math.floor(seconds / 60)
        const remainder = seconds - minutes * 60
        return minutes + ":" + (remainder < 10 ? "0" : "") + remainder.toFixed(3)
    }
    function wheel(map, index, suffix) {
        const values = telemetry[map]
        return values && values.length > index ? Number(values[index]).toFixed(1) + suffix : "—"
    }

    component Dot: Rectangle {
        property color dotColor: theme.green
        implicitWidth: 8
implicitHeight: 8
radius: 4
color: dotColor
    }
    component Pill: Rectangle {
        id: pill
        property string label: ""
        property color dotColor: theme.green
        implicitHeight: 28
        implicitWidth: row.implicitWidth + 20
        radius: 14
        color: "#1b1b1e"
        RowLayout { id: row
anchors.centerIn: parent
spacing: 7
Dot { dotColor: pill.dotColor }
Label { text: pill.label
color: theme.secondaryText
font.family: theme.fontFamily
font.pixelSize: Math.round(13 * theme.fontScale)
font.weight: Font.DemiBold } }
    }
    component Card: Rectangle {
        color: theme.surface
        radius: 13
        border.width: 1
        border.color: theme.line
    }
    component Divider: Rectangle { Layout.fillWidth: true
implicitHeight: 1
color: theme.line }
    component Heading: Label {
        color: theme.text
font.family: theme.fontFamily
font.pixelSize: Math.round(19 * theme.fontScale)
font.weight: Font.DemiBold
    }
    component Caption: Label {
        color: theme.muted
font.family: theme.fontFamily
font.pixelSize: Math.round(12 * theme.fontScale)
font.weight: Font.Medium
font.letterSpacing: 0.55
    }
    component ActionButton: Button {
        id: button
        property bool primary: false
        implicitHeight: 36
        horizontalPadding: 14
        contentItem: Label { text: button.text
color: button.primary ? "#071525" : theme.text
font.family: theme.fontFamily
font.pixelSize: Math.round(13 * theme.fontScale)
font.weight: Font.DemiBold
horizontalAlignment: Text.AlignHCenter
verticalAlignment: Text.AlignVCenter }
        background: Rectangle { radius: 8
color: button.primary ? (button.down ? "#0876df" : theme.blue) : (button.down ? "#303136" : (button.hovered ? theme.surfaceHover : theme.surfaceRaised))
border.width: button.primary ? 0 : 1
border.color: theme.line }
    }
    component AppField: TextField {
        id: field
        implicitHeight: 36
        color: theme.text
font.family: theme.fontFamily
font.pixelSize: Math.round(13 * theme.fontScale)
        leftPadding: 11
rightPadding: 11
selectByMouse: true
        placeholderTextColor: theme.muted
        background: Rectangle { radius: 8
color: "#141416"
border.width: field.activeFocus ? 2 : 1
border.color: field.activeFocus ? theme.blue : theme.lineStrong }
    }
    component AppSwitch: Switch {
        id: control
        implicitWidth: 42
implicitHeight: 26
        indicator: Rectangle {
            width: 40
height: 22
radius: 11
x: 1
y: 2
            color: control.checked ? theme.green : "#424348"
            Rectangle { width: 18
height: 18
radius: 9
x: control.checked ? 19 : 3
anchors.verticalCenter: parent.verticalCenter
color: "#ffffff" }
        }
        contentItem: Item {}
    }
    component Metric: Rectangle {
        property string label: ""
property string number: "—"
property string detail: ""
        implicitHeight: 96
radius: 10
color: "#202023"
        ColumnLayout { anchors.fill: parent
anchors.margins: 13
spacing: 2
            Caption { text: parent.parent.label.toUpperCase() }
            Label { text: parent.parent.number
color: theme.blueSoft
font.family: theme.monoFont
font.pixelSize: Math.round(24 * theme.fontScale)
font.weight: Font.DemiBold
Layout.fillWidth: true
elide: Text.ElideRight }
            Label { text: parent.parent.detail
color: theme.muted
font.family: theme.fontFamily
font.pixelSize: Math.round(12 * theme.fontScale)
Layout.fillWidth: true
elide: Text.ElideRight }
        }
    }
    component SettingRow: Item {
        id: row
        property string icon: "•"
property string title: ""
property string detail: ""
property string value: ""
property color valueColor: theme.secondaryText
        property bool withSwitch: false
property bool checked: false
property var toggled: function(value) {}
        implicitHeight: 64
Layout.fillWidth: true
        RowLayout { anchors.fill: parent
anchors.leftMargin: 14
anchors.rightMargin: 14
spacing: 12
            Rectangle { Layout.preferredWidth: 30
Layout.preferredHeight: 30
radius: 7
color: "#2a2b30"
Label { anchors.centerIn: parent
text: row.icon
color: theme.blueSoft
font.pixelSize: Math.round(17 * theme.fontScale)
font.weight: Font.DemiBold } }
            ColumnLayout { Layout.fillWidth: true
spacing: 1
                Label { text: row.title
color: theme.text
font.family: theme.fontFamily
font.pixelSize: Math.round(14 * theme.fontScale)
font.weight: Font.DemiBold
Layout.fillWidth: true
elide: Text.ElideRight }
                Label { text: row.detail
color: theme.muted
font.family: theme.fontFamily
font.pixelSize: Math.round(12 * theme.fontScale)
Layout.fillWidth: true
elide: Text.ElideRight }
            }
            Label { visible: !row.withSwitch && text.length > 0
text: row.value
color: row.valueColor
font.family: theme.monoFont
font.pixelSize: Math.round(12 * theme.fontScale)
Layout.maximumWidth: 270
elide: Text.ElideLeft }
            AppSwitch { visible: row.withSwitch
checked: row.checked
onToggled: row.toggled(checked) }
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            id: sidebar
            Layout.fillHeight: true
            Layout.preferredWidth: window.narrow ? 218 : 286
            color: theme.sidebar
            border.color: theme.line
            ColumnLayout {
                anchors.fill: parent
anchors.margins: 16
spacing: 0
                RowLayout { Layout.fillWidth: true
Layout.bottomMargin: 28
spacing: 10
                    Rectangle { Layout.preferredWidth: 42
Layout.preferredHeight: 42
radius: 11
color: "#071b32"
border.color: "#1768b8"
Label { anchors.centerIn: parent
text: "◉"
color: theme.blue
font.pixelSize: Math.round(25 * theme.fontScale) } }
                    ColumnLayout { Layout.fillWidth: true
spacing: 1
                        Label { text: "Kỹ sư Đua xe AI"
color: theme.text
font.family: theme.fontFamily
font.pixelSize: Math.round(17 * theme.fontScale)
font.weight: Font.DemiBold
elide: Text.ElideRight
Layout.fillWidth: true }
                        Label { text: app.simulatorName
color: theme.muted
font.family: theme.fontFamily
font.pixelSize: Math.round(12 * theme.fontScale)
elide: Text.ElideRight
Layout.fillWidth: true }
                    }
                }
                Repeater {
                    model: [ ["▦", "Bảng điều khiển"], ["◉", "Kỹ sư"], ["▥", "Telemetry"], ["✦", "Trí tuệ nhân tạo"], ["⚙", "Cài đặt"] ]
                    delegate: Item {
                        required property var modelData
required property int index
                        Layout.fillWidth: true
implicitHeight: 48
                        Rectangle { anchors.fill: parent
radius: 9
color: window.page === index ? theme.blue : "transparent" }
                        RowLayout { anchors.fill: parent
anchors.leftMargin: 12
anchors.rightMargin: 12
spacing: 12
                            Label { text: modelData[0]
color: window.page === index ? "#062446" : theme.secondaryText
font.pixelSize: Math.round(20 * theme.fontScale)
Layout.preferredWidth: 19
horizontalAlignment: Text.AlignHCenter }
                            Label { text: modelData[1]
color: window.page === index ? "#062446" : theme.secondaryText
font.family: theme.fontFamily
font.pixelSize: Math.round(15 * theme.fontScale)
font.weight: window.page === index ? Font.DemiBold : Font.Medium
Layout.fillWidth: true
elide: Text.ElideRight }
                        }
                        MouseArea { anchors.fill: parent
cursorShape: Qt.PointingHandCursor
onClicked: window.page = index }
                    }
                }
                Item { Layout.fillHeight: true }
                Rectangle { Layout.fillWidth: true
implicitHeight: 30
radius: 15
color: "#1b1b1e"
RowLayout { anchors.centerIn: parent
spacing: 7
Dot { dotColor: app.connected ? theme.green : theme.orange }
Label { text: app.connectionText
color: theme.secondaryText
font.family: theme.fontFamily
font.pixelSize: Math.round(11 * theme.fontScale) } } }
                Label { Layout.topMargin: 14
Layout.alignment: Qt.AlignHCenter
text: "v0.1.0  •  Sẵn sàng"
color: theme.muted
font.family: theme.fontFamily
font.pixelSize: Math.round(11 * theme.fontScale) }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
Layout.fillHeight: true
spacing: 0
            Rectangle {
                Layout.fillWidth: true
implicitHeight: 70
color: "#101011"
border.color: theme.line
                RowLayout { anchors.fill: parent
anchors.leftMargin: 30
anchors.rightMargin: 25
spacing: 12
                    Rectangle { Layout.preferredWidth: 40
Layout.preferredHeight: 40
radius: 10
color: "#071b32"
border.color: "#1768b8"
Label { anchors.centerIn: parent
text: "◉"
color: theme.blue
font.pixelSize: Math.round(23 * theme.fontScale) } }
                    Label { text: "Phiên trực tiếp"
color: theme.text
font.family: theme.fontFamily
font.pixelSize: Math.round(20 * theme.fontScale)
font.weight: Font.DemiBold }
                    Rectangle { visible: !window.narrow
Layout.preferredHeight: 27
implicitWidth: trackText.implicitWidth + 16
radius: 6
color: "#29292d"
Label { id: trackText
anchors.centerIn: parent
text: telemetry.track || "CHỜ SIMULATOR"
color: theme.muted
font.family: theme.monoFont
font.pixelSize: Math.round(12 * theme.fontScale) } }
                    Item { Layout.fillWidth: true }
                    Pill { label: app.connected ? "Đã kết nối " + app.simulatorName : "Chờ AC / ACC"
dotColor: app.connected ? theme.green : theme.orange }
                    Pill { visible: !window.narrow
label: app.voiceStatus === "Listening" ? "Đang lắng nghe" : "Radio sẵn sàng"
dotColor: app.voiceStatus === "Listening" ? theme.blueSoft : theme.muted }
                    Pill { visible: !window.narrow
label: app.apiState === "Connected" ? "AI trực tuyến" : "AI chưa sẵn sàng"
dotColor: app.apiState === "Connected" ? theme.green : theme.orange }
                    Rectangle { Layout.preferredWidth: 40
Layout.preferredHeight: 40
radius: 20
color: theme.blueSoft
Label { anchors.centerIn: parent
text: "♙"
color: "#082449"
font.pixelSize: Math.round(22 * theme.fontScale) } }
                }
            }

            StackLayout {
                Layout.fillWidth: true
Layout.fillHeight: true
currentIndex: window.page

                // Dashboard
                Item {
                    Flickable {
                        anchors.fill: parent
clip: true
contentWidth: width
contentHeight: dashboard.implicitHeight + 36
                        ScrollBar.vertical: ScrollBar {}
                        ColumnLayout {
                            id: dashboard
x: 30
y: 20
width: parent.width - 60
spacing: 20
                            Card {
                                Layout.fillWidth: true
implicitHeight: 84
                                RowLayout { anchors.fill: parent
anchors.margins: 14
spacing: 22
                                    ColumnLayout { spacing: 1
Caption { text: "VỊ TRÍ" }
Label { text: telemetry.position ? "P" + telemetry.position : "—"
color: theme.blueSoft
font.family: theme.monoFont
font.pixelSize: Math.round(25 * theme.fontScale)
font.weight: Font.Bold } }
                                    Divider { Layout.preferredWidth: 1
Layout.fillHeight: true
Layout.fillWidth: false }
                                    ColumnLayout { spacing: 1
Caption { text: "TIẾN ĐỘ CHẶNG" }
Label { text: telemetry.currentLap ? "Vòng " + telemetry.currentLap + (telemetry.totalLaps ? " / " + telemetry.totalLaps : "") : "Chờ phiên đua"
color: theme.text
font.family: theme.fontFamily
font.pixelSize: Math.round(17 * theme.fontScale)
font.weight: Font.DemiBold } }
                                    Divider { Layout.preferredWidth: 1
Layout.fillHeight: true
Layout.fillWidth: false }
                                    ColumnLayout { Layout.fillWidth: true
spacing: 1
Caption { text: "NHIÊN LIỆU" }
Label { text: has(telemetry.fuelLiters) ? value("fuelLiters", 1, " L") : "Chưa có dữ liệu"
color: theme.green
font.family: theme.monoFont
font.pixelSize: Math.round(18 * theme.fontScale)
font.weight: Font.DemiBold } }
                                    Pill { label: app.ttsAvailable ? "Kỹ sư AI: Sẵn sàng" : "Âm thanh chưa sẵn sàng"
dotColor: app.ttsAvailable ? theme.green : theme.orange }
                                    ActionButton { text: "Yêu cầu pit"
primary: true
onClicked: app.askText("Box this lap") }
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
Layout.alignment: Qt.AlignTop
spacing: 20
                                Card {
                                    Layout.fillWidth: true
Layout.preferredWidth: 700
implicitHeight: 650
                                    ColumnLayout { anchors.fill: parent
anchors.margins: 20
spacing: 14
                                        RowLayout { Layout.fillWidth: true
                                            Rectangle { Layout.preferredWidth: 42
Layout.preferredHeight: 42
radius: 11
color: "#26344d"
Label { anchors.centerIn: parent
text: "♬"
color: theme.blueSoft
font.pixelSize: Math.round(22 * theme.fontScale) } }
                                            ColumnLayout { Layout.fillWidth: true
spacing: 1
Heading { text: "Nhật ký đàm thoại vô tuyến" }
Label { text: "Tương tác thời gian thực giữa tay đua và kỹ sư AI"
color: theme.muted
font.family: theme.fontFamily
font.pixelSize: Math.round(12 * theme.fontScale) } }
                                            Pill { label: app.voiceStatus
dotColor: app.voiceStatus === "Listening" ? theme.green : theme.blueSoft }
                                        }
                                        Divider {}
                                        Rectangle { Layout.fillWidth: true
implicitHeight: 70
radius: 11
color: "#202023"
border.color: theme.line
                                            RowLayout { anchors.fill: parent
anchors.margins: 12
spacing: 12
                                                Rectangle { Layout.preferredWidth: 40
Layout.preferredHeight: 40
radius: 20
color: app.voiceStatus === "Listening" ? "#124d2a" : "#273449"
Label { anchors.centerIn: parent
text: "●"
color: app.voiceStatus === "Listening" ? theme.green : theme.blueSoft
font.pixelSize: Math.round(18 * theme.fontScale) } }
                                                ColumnLayout { Layout.fillWidth: true
spacing: 2
Label { text: app.voiceStatus === "Listening" ? "Micro đang mở • Đang nhận diện" : "Giữ để nói với kỹ sư AI"
color: theme.text
font.family: theme.fontFamily
font.pixelSize: Math.round(14 * theme.fontScale)
font.weight: Font.DemiBold }
Label { text: app.microphoneName
color: theme.muted
font.family: theme.fontFamily
font.pixelSize: Math.round(12 * theme.fontScale)
elide: Text.ElideRight
Layout.fillWidth: true } }
                                                ActionButton { text: app.voiceStatus === "Listening" ? "Đang nói" : "Giữ để nói"
primary: app.voiceStatus === "Listening"
onPressed: app.beginPushToTalk()
onReleased: app.endPushToTalk() }
                                            }
                                        }
                                        Item { Layout.fillHeight: true }
                                        ColumnLayout { Layout.fillWidth: true
visible: app.latestUserText.length > 0
spacing: 5
                                            Label { text: "TAY ĐUA"
color: theme.muted
font.family: theme.fontFamily
font.pixelSize: Math.round(11 * theme.fontScale)
font.weight: Font.DemiBold
Layout.alignment: Qt.AlignRight }
                                            Rectangle { Layout.alignment: Qt.AlignRight
implicitWidth: Math.min(parent.width * .78, userReply.implicitWidth + 30)
implicitHeight: userReply.implicitHeight + 22
radius: 16
color: "#2a2a2d"
Label { id: userReply
anchors.centerIn: parent
width: Math.min(500, implicitWidth)
text: app.latestUserText
color: theme.text
font.family: theme.fontFamily
font.pixelSize: Math.round(14 * theme.fontScale)
wrapMode: Text.Wrap } }
                                        }
                                        ColumnLayout { Layout.fillWidth: true
visible: app.latestEngineerText.length > 0
spacing: 5
                                            Label { text: "⚙  KỸ SƯ AI"
color: theme.blueSoft
font.family: theme.fontFamily
font.pixelSize: Math.round(11 * theme.fontScale)
font.weight: Font.DemiBold }
                                            Rectangle { implicitWidth: Math.min(parent.width * .82, engineerReply.implicitWidth + 30)
implicitHeight: engineerReply.implicitHeight + 22
radius: 14
color: "#29303d"
border.color: "#48556d"
Label { id: engineerReply
anchors.centerIn: parent
width: Math.min(500, implicitWidth)
text: app.latestEngineerText
color: theme.text
font.family: theme.fontFamily
font.pixelSize: Math.round(14 * theme.fontScale)
wrapMode: Text.Wrap } }
                                        }
                                        Rectangle { Layout.fillWidth: true
implicitHeight: 43
radius: 8
color: "#141416"
border.color: theme.lineStrong
                                            RowLayout { anchors.fill: parent
anchors.leftMargin: 12
anchors.rightMargin: 5
spacing: 8
                                                TextField { id: askField
Layout.fillWidth: true
color: theme.text
placeholderText: "Nhập khẩu lệnh hoặc giữ nút vô lăng để nói…"
placeholderTextColor: theme.muted
font.family: theme.fontFamily
font.pixelSize: Math.round(13 * theme.fontScale)
background: Item {}
onAccepted: { app.askText(text)
text = "" } }
                                                ActionButton { text: "Gửi"
primary: true
enabled: askField.text.length > 0
onClicked: { app.askText(askField.text)
askField.text = "" } }
                                            }
                                        }
                                    }
                                }
                                ColumnLayout { Layout.preferredWidth: window.narrow ? 300 : 430
spacing: 16
                                    Card { Layout.fillWidth: true
implicitHeight: 220
ColumnLayout { anchors.fill: parent
anchors.margins: 18
spacing: 12
                                        Heading { text: "Lệnh thoại nhanh 1 chạm" }
                                        GridLayout { Layout.fillWidth: true
columns: 2
columnSpacing: 10
rowSpacing: 10
                                            Repeater { model: [["Tình trạng lốp?", "Tyre status"], ["Xăng còn bao nhiêu?", "Fuel status"], ["Khoảng cách xe sau?", "Gap behind"], ["Yêu cầu Box (Pit)", "Box this lap"]]
delegate: ActionButton { required property var modelData
text: modelData[0]
Layout.fillWidth: true
onClicked: app.askText(modelData[1]) } }
                                        }
                                    } }
                                    Card { Layout.fillWidth: true
implicitHeight: 264
ColumnLayout { anchors.fill: parent
anchors.margins: 18
spacing: 12
                                        Heading { text: "Cài đặt nhanh buồng lái" }
                                        Divider {}
                                        SettingRow { title: "Bộ máy TTS"
detail: app.ttsStatus
value: app.ttsBackend
valueColor: app.ttsAvailable ? theme.green : theme.orange }
                                        Divider {}
                                        RowLayout { Layout.fillWidth: true
Label { text: "Push-to-talk bàn phím"
color: theme.text
font.family: theme.fontFamily
font.pixelSize: Math.round(14 * theme.fontScale)
Layout.fillWidth: true }
AppSwitch { checked: app.keyboardPttEnabled
onToggled: app.setPushToTalkOptions(checked, app.directInputPttEnabled) } }
                                        RowLayout { Layout.fillWidth: true
Label { text: "Push-to-talk DirectInput"
color: theme.text
font.family: theme.fontFamily
font.pixelSize: Math.round(14 * theme.fontScale)
Layout.fillWidth: true }
AppSwitch { checked: app.directInputPttEnabled
onToggled: app.setPushToTalkOptions(app.keyboardPttEnabled, checked) } }
                                    } }
                                    Card { Layout.fillWidth: true
implicitHeight: 120
ColumnLayout { anchors.fill: parent
anchors.margins: 17
spacing: 8
                                        Caption { text: "TRẠNG THÁI LIÊN KẾT HỆ THỐNG" }
                                        RowLayout { Layout.fillWidth: true
Metric { Layout.fillWidth: true
label: "Simulator"
number: app.connected ? "ON" : "OFF"
detail: app.simulatorName }
Metric { Layout.fillWidth: true
label: "DirectInput"
number: app.directInputPttEnabled ? "ON" : "OFF"
detail: app.directInputBinding }
Metric { Layout.fillWidth: true
label: "AI"
number: app.apiState
detail: app.apiModel } }
                                    } }
                                }
                            }
                        }
                    }
                }

                // Engineer
                Item {
                    Flickable { anchors.fill: parent
clip: true
contentWidth: width
contentHeight: engineer.implicitHeight + 42
ScrollBar.vertical: ScrollBar {}
                        ColumnLayout { id: engineer
x: Math.max(30, (parent.width - 1080) / 2)
y: 22
width: Math.min(parent.width - 60, 1080)
spacing: 17
                            ColumnLayout { Layout.fillWidth: true
spacing: 4
Label { text: "Thiết lập Kỹ sư & Cảnh giới"
color: theme.text
font.family: theme.fontFamily
font.pixelSize: Math.round(28 * theme.fontScale)
font.weight: Font.DemiBold }
Label { text: "Tùy chỉnh giọng nói, cảnh báo chủ động và điều khiển radio."
color: theme.muted
font.family: theme.fontFamily
font.pixelSize: Math.round(14 * theme.fontScale) } }
                            RowLayout { Layout.fillWidth: true
spacing: 14
                                Metric { Layout.fillWidth: true
label: "Động cơ Piper"
number: app.ttsAvailable ? "Hoạt động" : "Chưa sẵn sàng"
detail: app.ttsStatus }
                                Metric { Layout.fillWidth: true
label: "Radar không gian"
number: app.connected ? "Hoạt động" : "Chờ simulator"
detail: "SpotterEngine cục bộ" }
                                Metric { Layout.fillWidth: true
label: "Whisper ASR"
number: app.voiceStatus
detail: "PhoWhisper • PTT" }
                            }
                            Caption { text: "KỸ SƯ ĐUA XE AI"
Layout.topMargin: 4 }
                            Card { Layout.fillWidth: true
implicitHeight: engineerRows.implicitHeight + 16
ColumnLayout { id: engineerRows
anchors.left: parent.left
anchors.right: parent.right
anchors.top: parent.top
anchors.margins: 8
spacing: 0
                                SettingRow { icon: "♙"
title: "Động cơ phản hồi"
detail: "Giọng nói cục bộ dùng cho phản hồi kỹ sư"
value: app.ttsBackend
valueColor: app.ttsAvailable ? theme.green : theme.orange }
                                Divider {}
                                SettingRow { icon: "◎"
title: "Nhận diện giọng nói"
detail: "Giữ PTT để ghi âm
nhả nút sẽ gửi ngay sang PhoWhisper"
value: app.voiceStatus
valueColor: theme.blueSoft }
                                Divider {}
                                SettingRow { icon: "⌁"
title: "Phím tắt Nói (PTT)"
detail: "Ctrl + Space hoạt động khi ứng dụng có focus"
withSwitch: true
checked: app.keyboardPttEnabled
toggled: function(v) { app.setPushToTalkOptions(v, app.directInputPttEnabled) } }
                                Divider {}
                                SettingRow { icon: "◉"
title: "PTT vô lăng DirectInput"
detail: app.directInputStatus
withSwitch: true
checked: app.directInputPttEnabled
toggled: function(v) { app.setPushToTalkOptions(app.keyboardPttEnabled, v) } }
                            } }
                            Caption { text: "CẢNH GIỚI ÂM THANH (SPOTTER)"
Layout.topMargin: 4 }
                            Card { Layout.fillWidth: true
implicitHeight: spotterRows.implicitHeight + 16
ColumnLayout { id: spotterRows
anchors.left: parent.left
anchors.right: parent.right
anchors.top: parent.top
anchors.margins: 8
spacing: 0
                                SettingRow { icon: "◌"
title: "Cảnh báo khoảng cách xe"
detail: "SpotterEngine đọc radar từ AC / ACC khi dữ liệu có sẵn"
value: app.connected ? "Đang theo dõi" : "Chờ dữ liệu"
valueColor: app.connected ? theme.green : theme.muted }
                                Divider {}
                                SettingRow { icon: "⚑"
title: "Cờ hiệu & nguy hiểm chặng"
detail: "Cảnh báo deterministic luôn ưu tiên trước hội thoại AI"
value: "Cục bộ"
valueColor: theme.green }
                                Divider {}
                                SettingRow { icon: "!"
title: "Thông báo cơ học khẩn cấp"
detail: "Nhiệt độ động cơ, áp suất lốp và nhiên liệu được kiểm tra ở backend"
value: "Bật"
valueColor: theme.green }
                            } }
                            Label { text: "Spotter và thông điệp an toàn chạy cục bộ, không phụ thuộc trạng thái server AI."
color: theme.muted
font.family: theme.fontFamily
font.pixelSize: Math.round(12 * theme.fontScale) }
                        }
                    }
                }

                // Telemetry
                Item {
                    Flickable { anchors.fill: parent
clip: true
contentWidth: width
contentHeight: telemetryPage.implicitHeight + 42
ScrollBar.vertical: ScrollBar {}
                        ColumnLayout { id: telemetryPage
x: 30
y: 20
width: parent.width - 60
spacing: 20
                            Card { Layout.fillWidth: true
implicitHeight: 78
RowLayout { anchors.fill: parent
anchors.margins: 14
spacing: 12
                                Pill { label: app.simulatorName
dotColor: app.connected ? theme.green : theme.muted }
Pill { label: app.connected ? "Live shared memory" : "Không có dữ liệu live"
dotColor: app.connected ? theme.green : theme.orange }
Label { text: telemetry.track || "Chờ AC / ACC"
color: theme.text
font.family: theme.fontFamily
font.pixelSize: Math.round(19 * theme.fontScale)
font.weight: Font.DemiBold
Layout.leftMargin: 8 }
Item { Layout.fillWidth: true }
Label { text: telemetry.sessionType || ""
color: theme.muted
font.family: theme.monoFont
font.pixelSize: Math.round(13 * theme.fontScale) }
                            } }
                            RowLayout { Layout.fillWidth: true
spacing: 20
                                Card { Layout.fillWidth: true
Layout.preferredWidth: 700
implicitHeight: 438
ColumnLayout { anchors.fill: parent
anchors.margins: 20
spacing: 14
                                    RowLayout { Layout.fillWidth: true
Heading { text: "Động lực học & Thao tác lái"
Layout.fillWidth: true }
Caption { text: "ĐỒNG BỘ KÊNH • HOẠT ĐỘNG" } }
                                    Rectangle { Layout.fillWidth: true
implicitHeight: 110
radius: 10
color: theme.surfaceRaised
RowLayout { anchors.fill: parent
anchors.margins: 16
spacing: 20
                                        ColumnLayout { Layout.preferredWidth: 120
Caption { text: "SỐ ĐANG GÀI" }
Label { text: has(telemetry.gear) ? telemetry.gear : "—"
color: theme.blueSoft
font.family: theme.monoFont
font.pixelSize: Math.round(54 * theme.fontScale)
font.weight: Font.Bold } }
                                        ColumnLayout { Layout.fillWidth: true
Caption { text: "TỐC ĐỘ ĐỘNG CƠ" }
Label { text: whole("rpm", " RPM")
color: theme.text
font.family: theme.monoFont
font.pixelSize: Math.round(27 * theme.fontScale)
font.weight: Font.DemiBold
horizontalAlignment: Text.AlignRight
Layout.fillWidth: true }
ProgressBar { Layout.fillWidth: true
value: has(telemetry.rpm) ? Math.min(1, telemetry.rpm / 9000) : 0
background: Rectangle { implicitHeight: 8
radius: 4
color: "#0d0d0f" }
contentItem: Item { Rectangle { width: parent.width * parent.parent.visualPosition
height: 8
radius: 4
color: theme.orange } } } }
                                    } }
                                    Repeater { model: [["Vận tốc di chuyển", "speedKmh", theme.blueSoft, " km/h"], ["Bướm ga (Throttle)", "throttle", theme.green, "%"], ["Áp lực phanh", "brake", theme.red, "%"]]
delegate: ColumnLayout { required property var modelData
Layout.fillWidth: true
spacing: 6
                                        RowLayout { Layout.fillWidth: true
Label { text: modelData[0]
color: theme.secondaryText
font.family: theme.fontFamily
font.pixelSize: Math.round(14 * theme.fontScale)
Layout.fillWidth: true }
Label { text: modelData[1] === "speedKmh" ? window.whole(modelData[1], modelData[3]) : window.has(telemetry[modelData[1]]) ? Math.round(telemetry[modelData[1]] * 100) + modelData[3] : "—"
color: modelData[2]
font.family: theme.monoFont
font.pixelSize: Math.round(17 * theme.fontScale)
font.weight: Font.DemiBold } }
                                        ProgressBar { Layout.fillWidth: true
value: modelData[1] === "speedKmh" ? (has(telemetry.speedKmh) ? Math.min(1, telemetry.speedKmh / 350) : 0) : (telemetry[modelData[1]] || 0)
background: Rectangle { implicitHeight: 7
radius: 4
color: "#0d0d0f" }
contentItem: Item { Rectangle { width: parent.width * parent.parent.visualPosition
height: 7
radius: 4
color: modelData[2] } } }
                                    } }
                                } }
                                ColumnLayout { Layout.preferredWidth: 430
spacing: 15
                                    Card { Layout.fillWidth: true
implicitHeight: 254
ColumnLayout { anchors.fill: parent
anchors.margins: 20
spacing: 13
                                        RowLayout { Layout.fillWidth: true
Heading { text: "Diễn biến chặng đua"
Layout.fillWidth: true }
Pill { label: telemetry.flag || "Chờ cờ"
dotColor: theme.green } }
                                        RowLayout { Layout.fillWidth: true
Label { text: telemetry.position ? "P" + telemetry.position : "—"
color: theme.blueSoft
font.family: theme.monoFont
font.pixelSize: Math.round(38 * theme.fontScale)
font.weight: Font.Bold }
Item { Layout.fillWidth: true }
Label { text: telemetry.currentLap ? "Vòng " + telemetry.currentLap + (telemetry.totalLaps ? " / " + telemetry.totalLaps : "") : "—"
color: theme.text
font.family: theme.fontFamily
font.pixelSize: Math.round(16 * theme.fontScale)
font.weight: Font.DemiBold } }
                                        Divider {}
                                        RowLayout { Layout.fillWidth: true
Label { text: "Xe trước"
color: theme.muted
font.family: theme.fontFamily
font.pixelSize: Math.round(13 * theme.fontScale)
Layout.fillWidth: true }
Label { text: telemetry.opponentAhead || "Chưa có"
color: theme.secondaryText
font.family: theme.fontFamily
font.pixelSize: Math.round(13 * theme.fontScale) }
Label { text: value("gapAheadSeconds", 2, " s")
color: theme.orange
font.family: theme.monoFont
font.pixelSize: Math.round(16 * theme.fontScale) } }
                                        RowLayout { Layout.fillWidth: true
Label { text: "Xe sau"
color: theme.muted
font.family: theme.fontFamily
font.pixelSize: Math.round(13 * theme.fontScale)
Layout.fillWidth: true }
Label { text: telemetry.opponentBehind || "Chưa có"
color: theme.secondaryText
font.family: theme.fontFamily
font.pixelSize: Math.round(13 * theme.fontScale) }
Label { text: value("gapBehindSeconds", 2, " s")
color: theme.green
font.family: theme.monoFont
font.pixelSize: Math.round(16 * theme.fontScale) } }
                                    } }
                                    Card { Layout.fillWidth: true
implicitHeight: 102
RowLayout { anchors.fill: parent
anchors.margins: 18
Rectangle { Layout.preferredWidth: 44
Layout.preferredHeight: 44
radius: 11
color: "#25354f"
Label { anchors.centerIn: parent
text: "◷"
color: theme.blueSoft
font.pixelSize: Math.round(24 * theme.fontScale) } }
ColumnLayout { Layout.fillWidth: true
Caption { text: "THỜI GIAN VÒNG" }
Label { text: lap(telemetry.currentLapTimeSeconds)
color: theme.text
font.family: theme.monoFont
font.pixelSize: Math.round(23 * theme.fontScale)
font.weight: Font.DemiBold } } } }
                                }
                            }
                            Card { Layout.fillWidth: true
implicitHeight: 230
ColumnLayout { anchors.fill: parent
anchors.margins: 20
spacing: 14
                                RowLayout { Layout.fillWidth: true
Heading { text: "Nhiên liệu, chiến lược & lốp"
Layout.fillWidth: true }
Pill { label: has(telemetry.fuelLiters) ? "Dữ liệu live" : "Chờ telemetry"
dotColor: has(telemetry.fuelLiters) ? theme.green : theme.muted } }
                                RowLayout { Layout.fillWidth: true
spacing: 13
                                    Metric { Layout.fillWidth: true
label: "Mức nhiên liệu"
number: value("fuelLiters", 1, " L")
detail: has(telemetry.fuelCapacityLiters) ? "/ " + value("fuelCapacityLiters", 1, " L") : "Dung lượng không rõ" }
                                    Metric { Layout.fillWidth: true
label: "Lốp FL"
number: wheel("tyrePressures", 0, " psi")
detail: wheel("tyreTemperatures", 0, " °C") }
                                    Metric { Layout.fillWidth: true
label: "Lốp FR"
number: wheel("tyrePressures", 1, " psi")
detail: wheel("tyreTemperatures", 1, " °C") }
                                    Metric { Layout.fillWidth: true
label: "Lốp RL / RR"
number: wheel("tyrePressures", 2, " psi")
detail: wheel("tyrePressures", 3, " psi") }
                                }
                            } }
                        }
                    }
                }

                // AI configuration
                Item {
                    Flickable { anchors.fill: parent
clip: true
contentWidth: width
contentHeight: aiPage.implicitHeight + 42
ScrollBar.vertical: ScrollBar {}
                        ColumnLayout { id: aiPage
x: 30
y: 21
width: parent.width - 60
spacing: 18
                            ColumnLayout { Layout.fillWidth: true
spacing: 4
Caption { text: "CÀI ĐẶT / BỘ MÁY SUY LUẬN TRÍ TUỆ NHÂN TẠO"
color: theme.blueSoft }
Label { text: "Máy chủ AI & Mô hình Cục bộ"
color: theme.text
font.family: theme.fontFamily
font.pixelSize: Math.round(30 * theme.fontScale)
font.weight: Font.DemiBold }
Label { text: "Cấu hình endpoint OpenAI-compatible và tham số phản hồi thời gian thực."
color: theme.muted
font.family: theme.fontFamily
font.pixelSize: Math.round(14 * theme.fontScale) } }
                            RowLayout { Layout.fillWidth: true
Layout.alignment: Qt.AlignTop
spacing: 20
                                ColumnLayout { Layout.fillWidth: true
Layout.preferredWidth: 720
spacing: 18
                                    Card { Layout.fillWidth: true
implicitHeight: 212
color: app.apiState === "Connected" ? "#19221d" : theme.surface
ColumnLayout { anchors.fill: parent
anchors.margins: 20
spacing: 13
                                        RowLayout { Layout.fillWidth: true
ColumnLayout { Layout.fillWidth: true
Heading { text: app.apiProvider }
Label { text: app.apiBaseUrl
color: theme.muted
font.family: theme.monoFont
font.pixelSize: Math.round(12 * theme.fontScale)
elide: Text.ElideMiddle
Layout.fillWidth: true } }
Pill { label: app.apiState
dotColor: app.apiState === "Connected" ? theme.green : theme.orange } }
                                        RowLayout { Layout.fillWidth: true
spacing: 10
Metric { Layout.fillWidth: true
label: "Độ trễ"
number: String(app.apiStatistics.lastLatencyMs || "—") + " ms"
detail: "Phản hồi gần nhất" }
Metric { Layout.fillWidth: true
label: "Mô hình"
number: app.apiModel
detail: app.apiStreaming ? "Streaming bật" : "Streaming tắt" }
Metric { Layout.fillWidth: true
label: "Yêu cầu"
number: String(app.apiStatistics.requests || 0)
detail: "Lỗi " + String(app.apiStatistics.failed || 0) } }
                                    } }
                                    Caption { text: "THÔNG SỐ MÁY CHỦ & GIAO THỨC" }
                                    Card { Layout.fillWidth: true
implicitHeight: serverForm.implicitHeight + 34
ColumnLayout { id: serverForm
anchors.left: parent.left
anchors.right: parent.right
anchors.top: parent.top
anchors.margins: 17
spacing: 12
                                        RowLayout { Layout.fillWidth: true
Label { text: "Nhà cung cấp AI"
color: theme.secondaryText
font.family: theme.fontFamily
font.pixelSize: Math.round(14 * theme.fontScale)
Layout.preferredWidth: 220 }
StyledComboBox { id: provider
Layout.fillWidth: true
model: ["OpenAI Compatible"]
currentIndex: 0 } }
                                        Divider {}
                                        RowLayout { Layout.fillWidth: true
Label { text: "Cổng Endpoint API"
color: theme.secondaryText
font.family: theme.fontFamily
font.pixelSize: Math.round(14 * theme.fontScale)
Layout.preferredWidth: 220 }
AppField { id: baseUrl
Layout.fillWidth: true
text: app.apiBaseUrl } }
                                        Divider {}
                                        RowLayout { Layout.fillWidth: true
Label { text: "Mô hình suy luận"
color: theme.secondaryText
font.family: theme.fontFamily
font.pixelSize: Math.round(14 * theme.fontScale)
Layout.preferredWidth: 220 }
AppField { id: model
Layout.fillWidth: true
text: app.apiModel } }
                                        Divider {}
                                        RowLayout { Layout.fillWidth: true
Label { text: "Khóa API"
color: theme.secondaryText
font.family: theme.fontFamily
font.pixelSize: Math.round(14 * theme.fontScale)
Layout.preferredWidth: 220 }
AppField { id: apiKey
Layout.fillWidth: true
echoMode: TextInput.Password
text: app.apiKey
placeholderText: "Tùy chọn cho local endpoint" } }
                                        RowLayout { Layout.fillWidth: true
ActionButton { text: "Kiểm tra kết nối"
primary: true
onClicked: { app.saveAiSettings(provider.currentText, baseUrl.text, apiKey.text, model.text, streaming.checked, Number(timeout.text), Number(tokens.text), Number(temperature.text))
app.testApiConnection() } }
Label { text: app.apiDetail
color: app.apiState === "Connected" ? theme.green : theme.secondaryText
font.family: theme.monoFont
font.pixelSize: Math.round(12 * theme.fontScale)
Layout.fillWidth: true
wrapMode: Text.Wrap } }
                                    } }
                                }
                                ColumnLayout { Layout.preferredWidth: 430
spacing: 18
                                    Card { Layout.fillWidth: true
implicitHeight: 338
ColumnLayout { anchors.fill: parent
anchors.margins: 19
spacing: 12
                                        Heading { text: "Tham số nâng cao" }
Divider {}
                                        Caption { text: "ĐỘ NGẪU NHIÊN (TEMPERATURE)" }
RowLayout { Layout.fillWidth: true
Slider { id: temperature
Layout.fillWidth: true
from: 0
to: 1
value: app.apiTemperature
stepSize: .05 }
Label { text: temperature.value.toFixed(2)
color: theme.blueSoft
font.family: theme.monoFont
font.pixelSize: Math.round(14 * theme.fontScale) } }
                                        Divider {}
                                        Caption { text: "SỐ TOKEN XUẤT TỐI ĐA" }
RowLayout { Layout.fillWidth: true
Slider { id: tokens
Layout.fillWidth: true
from: 16
to: 128
value: app.apiMaximumTokens
stepSize: 8 }
Label { text: Math.round(tokens.value)
color: theme.blueSoft
font.family: theme.monoFont
font.pixelSize: Math.round(14 * theme.fontScale) } }
                                        Divider {}
                                        RowLayout { Layout.fillWidth: true
Label { text: "Thời gian chờ (ms)"
color: theme.text
font.family: theme.fontFamily
font.pixelSize: Math.round(14 * theme.fontScale)
Layout.fillWidth: true }
AppField { id: timeout
Layout.preferredWidth: 106
text: app.apiTimeoutMilliseconds
inputMethodHints: Qt.ImhDigitsOnly } }
                                        RowLayout { Layout.fillWidth: true
Label { text: "Streaming response"
color: theme.text
font.family: theme.fontFamily
font.pixelSize: Math.round(14 * theme.fontScale)
Layout.fillWidth: true }
AppSwitch { id: streaming
checked: app.apiStreaming } }
                                        ActionButton { Layout.fillWidth: true
text: "Lưu cấu hình AI"
onClicked: app.saveAiSettings(provider.currentText, baseUrl.text, apiKey.text, model.text, streaming.checked, Number(timeout.text), Number(tokens.text), Number(temperature.text)) }
                                    } }
                                    Card { Layout.fillWidth: true
implicitHeight: 165
ColumnLayout { anchors.fill: parent
anchors.margins: 18
spacing: 9
Heading { text: "Hoạt động công cụ" }
Divider {}
Repeater { model: app.toolLog
delegate: Label { required property var modelData
Layout.fillWidth: true
text: modelData.name + "  " + modelData.result
color: theme.secondaryText
font.family: theme.monoFont
font.pixelSize: Math.round(11 * theme.fontScale)
elide: Text.ElideRight } }
Label { visible: app.toolLog.length === 0
text: "Chưa có lời gọi tool trong phiên này."
color: theme.muted
font.family: theme.fontFamily
font.pixelSize: Math.round(12 * theme.fontScale) } } }
                                }
                            }
                        }
                    }
                }

                // Settings
                Item {
                    Flickable { anchors.fill: parent
clip: true
contentWidth: width
contentHeight: settingsPage.implicitHeight + 42
ScrollBar.vertical: ScrollBar {}
                        ColumnLayout { id: settingsPage
x: Math.max(30, (parent.width - 950) / 2)
y: 21
width: Math.min(parent.width - 60, 950)
spacing: 16
                            ColumnLayout { Layout.fillWidth: true
spacing: 4
Label { text: "Cài đặt"
color: theme.text
font.family: theme.fontFamily
font.pixelSize: Math.round(29 * theme.fontScale)
font.weight: Font.DemiBold }
Label { text: "Thiết bị, game integration, push-to-talk và định tuyến âm thanh."
color: theme.muted
font.family: theme.fontFamily
font.pixelSize: Math.round(14 * theme.fontScale) } }
                            Caption { text: "TÍCH HỢP MÔ PHỎNG ĐUA XE" }
                            Card { Layout.fillWidth: true
implicitHeight: simRows.implicitHeight + 16
ColumnLayout { id: simRows
anchors.left: parent.left
anchors.right: parent.right
anchors.top: parent.top
anchors.margins: 8
spacing: 0
                                SettingRow { icon: "◉"
title: "Tự động phát hiện trò chơi đang mở"
detail: "Theo dõi Assetto Corsa và Assetto Corsa Competizione"
value: app.connectionText
valueColor: app.connected ? theme.green : theme.orange }
                                Divider {}
                                SettingRow { icon: "ACC"
title: "Assetto Corsa Competizione"
detail: "Shared memory telemetry và ACC broadcaster UDP 9000 nếu cấu hình game cung cấp"
value: app.simulatorName
valueColor: app.connected ? theme.green : theme.secondaryText }
                                Divider {}
                                SettingRow { icon: "AC"
title: "Assetto Corsa"
detail: "Shared memory telemetry khi game đang chạy"
value: "Tự động"
valueColor: theme.secondaryText }
                                Divider {}
                                SettingRow { visible: app.mockAvailable
icon: "◇"
title: "Mock telemetry"
detail: "Chỉ dùng kiểm thử khi không mở simulator"
withSwitch: true
checked: app.mockEnabled
toggled: function(v) { app.setUseMockTelemetry(v) } }
                            } }
                            Caption { text: "GÁN PHÍM DIRECTINPUT (VÔ LĂNG & BÀN ĐẠP)" }
                            Card { Layout.fillWidth: true
implicitHeight: inputRows.implicitHeight + 16
ColumnLayout { id: inputRows
anchors.left: parent.left
anchors.right: parent.right
anchors.top: parent.top
anchors.margins: 8
spacing: 0
                                SettingRow { icon: "◍"
title: "Thiết bị phát hiện"
detail: "Vô lăng, bàn đạp hoặc bàn phím gắn ngoài"
value: app.directInputStatus
valueColor: theme.secondaryText }
                                Divider {}
                                Item { Layout.fillWidth: true
implicitHeight: 64
RowLayout { anchors.fill: parent
anchors.leftMargin: 14
anchors.rightMargin: 14
spacing: 12
                                    Rectangle { Layout.preferredWidth: 30
Layout.preferredHeight: 30
radius: 7
color: "#2a2b30"
Label { anchors.centerIn: parent
text: "♬"
color: theme.blueSoft
font.pixelSize: Math.round(17 * theme.fontScale) } }
                                    ColumnLayout { Layout.fillWidth: true
spacing: 1
Label { text: "Nút đàm thoại PTT (Push-to-Talk)"
color: theme.text
font.family: theme.fontFamily
font.pixelSize: Math.round(14 * theme.fontScale)
font.weight: Font.DemiBold }
Label { text: "Giữ nút trên vô lăng để ghi âm, nhả nút để gửi PhoWhisper"
color: theme.muted
font.family: theme.fontFamily
font.pixelSize: Math.round(12 * theme.fontScale) } }
                                    Label { text: app.directInputBinding
color: theme.green
font.family: theme.monoFont
font.pixelSize: Math.round(12 * theme.fontScale)
elide: Text.ElideLeft
Layout.maximumWidth: 210 }
                                    ActionButton { text: "Gán lại"
onClicked: app.beginDirectInputMapping() }
                                } }
                                Divider {}
                                SettingRow { icon: "⌁"
title: "Bật PTT DirectInput"
detail: "Nhận press/release nền từ wheel khi được bật"
withSwitch: true
checked: app.directInputPttEnabled
toggled: function(v) { app.setPushToTalkOptions(app.keyboardPttEnabled, v) } }
                            } }
                            Caption { text: "ĐỊNH TUYẾN ÂM THANH MẶC ĐỊNH" }
                            Card { Layout.fillWidth: true
implicitHeight: audioRows.implicitHeight + 16
ColumnLayout { id: audioRows
anchors.left: parent.left
anchors.right: parent.right
anchors.top: parent.top
anchors.margins: 8
spacing: 0
                                SettingRow { icon: "●"
title: "Thiết bị Thu âm Mặc định"
detail: "WASAPI default input đang được dùng cho PTT"
value: app.microphoneName
valueColor: theme.secondaryText }
                                Divider {}
                                Item { Layout.fillWidth: true
implicitHeight: 70
RowLayout { anchors.fill: parent
anchors.leftMargin: 14
anchors.rightMargin: 14
spacing: 12
                                    Rectangle { Layout.preferredWidth: 30
Layout.preferredHeight: 30
radius: 7
color: "#2a2b30"
Label { anchors.centerIn: parent
text: "♪"
color: theme.blueSoft
font.pixelSize: Math.round(17 * theme.fontScale) } }
                                    ColumnLayout { Layout.fillWidth: true
spacing: 1
Label { text: "Thiết bị Phát âm thanh Mặc định"
color: theme.text
font.family: theme.fontFamily
font.pixelSize: Math.round(14 * theme.fontScale)
font.weight: Font.DemiBold }
Label { text: "WASAPI output cho phản hồi kỹ sư và âm báo PTT"
color: theme.muted
font.family: theme.fontFamily
font.pixelSize: Math.round(12 * theme.fontScale) } }
                                    StyledComboBox { id: outputDevice
Layout.preferredWidth: 310
model: app.audioOutputDevices
currentIndex: Math.max(0, app.audioOutputDevices.indexOf(app.selectedAudioOutput))
onActivated: app.setAudioOutputDevice(currentText) }
                                } }
                                Divider {}
                                Item { Layout.fillWidth: true
implicitHeight: 64
RowLayout { anchors.fill: parent
anchors.leftMargin: 14
anchors.rightMargin: 14
spacing: 12
                                    Rectangle { Layout.preferredWidth: 30
Layout.preferredHeight: 30
radius: 7
color: "#2a2b30"
Label { anchors.centerIn: parent
text: "♪"
color: theme.blueSoft
font.pixelSize: Math.round(17 * theme.fontScale) } }
                                    ColumnLayout { Layout.fillWidth: true
spacing: 1
Label { text: "Giọng phản hồi kỹ sư"
color: theme.text
font.family: theme.fontFamily
font.pixelSize: Math.round(14 * theme.fontScale)
font.weight: Font.DemiBold }
Label { text: app.ttsStatus
color: theme.muted
font.family: theme.fontFamily
font.pixelSize: Math.round(12 * theme.fontScale) } }
                                    StyledComboBox { id: tts
model: ["Piper", "Gwen-TTS"]
currentIndex: app.ttsBackend === "Gwen-TTS" ? 1 : 0
onActivated: app.setTtsBackend(currentText) }
                                } }
                            } }
                            Caption { text: "BẢO TRÌ & NHẬT KÝ HOẠT ĐỘNG" }
                            Card { Layout.fillWidth: true
implicitHeight: 146
ColumnLayout { anchors.fill: parent
anchors.margins: 18
spacing: 9
                                RowLayout { Layout.fillWidth: true
ColumnLayout { Layout.fillWidth: true
Label { text: "Hội thoại hiện tại"
color: theme.text
font.family: theme.fontFamily
font.pixelSize: Math.round(14 * theme.fontScale)
font.weight: Font.DemiBold }
Label { text: "Xóa lịch sử hội thoại AI của phiên đang chạy"
color: theme.muted
font.family: theme.fontFamily
font.pixelSize: Math.round(12 * theme.fontScale) } }
ActionButton { text: "Xóa hội thoại"
onClicked: app.resetConversation() } }
                                Divider {}
                                RowLayout { Layout.fillWidth: true
ColumnLayout { Layout.fillWidth: true
Label { text: "Cấu hình AI"
color: theme.text
font.family: theme.fontFamily
font.pixelSize: Math.round(14 * theme.fontScale)
font.weight: Font.DemiBold }
Label { text: app.apiDetail
color: theme.muted
font.family: theme.fontFamily
font.pixelSize: Math.round(12 * theme.fontScale)
Layout.fillWidth: true
elide: Text.ElideRight } }
ActionButton { text: "Mở AI"
onClicked: window.page = 3 } }
                            } }
                        }
                    }
                }
            }
        }
    }
}
