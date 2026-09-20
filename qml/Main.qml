pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

ApplicationWindow {
    id: root
    width: 1280
    height: 800
    minimumWidth: 880
    minimumHeight: 576
    visible: true
    title: "Race Engineer"
    color: "#131315"
    flags: Qt.Window | Qt.FramelessWindowHint

    readonly property real ui: Math.max(0.75, Math.min(1.15, width / 1390))
    readonly property real sideWidth: Math.max(210, Math.min(286, width * 0.18))
    readonly property real headerHeight: width >= 1500 ? 70 : 56
    readonly property color bg: "#131315"
    readonly property color lowest: "#0e0e10"
    readonly property color panel: "#1b1b1d"
    readonly property color raised: "#2a2a2c"
    readonly property color border: "#414754"
    readonly property color textMain: "#e4e2e4"
    readonly property color textMuted: "#8b91a0"
    readonly property color primary: "#aac7ff"
    readonly property color blue: "#3e90ff"
    readonly property color green: "#47e266"
    readonly property color amber: "#ffb868"
    property int currentPage: 0

    font.family: "Segoe UI"
    font.pixelSize: 14 * ui

    component BodyText: Text {
        color: root.textMain
        font.family: "Segoe UI"
        font.pixelSize: 14 * root.ui
        renderType: Text.NativeRendering
    }
    component MutedText: BodyText { color: root.textMuted; font.pixelSize: 13 * root.ui }
    component HeadingText: BodyText { font.family: "Segoe UI"; font.pixelSize: 20 * root.ui; font.weight: Font.DemiBold }
    component MonoText: BodyText { font.family: "Cascadia Mono"; font.pixelSize: 20 * root.ui }

    component AppButton: Button {
        id: control
        implicitHeight: 38 * root.ui
        leftPadding: 16 * root.ui; rightPadding: 16 * root.ui
        font.family: "Segoe UI"; font.weight: Font.DemiBold; font.pixelSize: 13 * root.ui
        opacity: control.enabled ? 1.0 : 0.45
        contentItem: Text { text: control.text; color: control.highlighted ? "#002957" : root.textMain; font: control.font; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
        background: Rectangle {
            radius: 8 * root.ui
            color: control.highlighted ? (control.down ? Qt.darker(root.primary, 1.15) : control.hovered ? Qt.lighter(root.primary, 1.08) : root.primary)
                                       : (control.down ? "#353437" : control.hovered ? "#303033" : root.raised)
            border.color: control.highlighted ? Qt.rgba(1, 1, 1, 0.4) : Qt.rgba(1, 1, 1, .05)
            border.width: control.highlighted ? 1.5 : 1
        }
    }
    component StatusPill: Rectangle {
        property alias text: statusText.text
        property color statusColor: root.green
        implicitWidth: statusRow.implicitWidth + 20 * root.ui
        implicitHeight: 28 * root.ui
        radius: height / 2; color: root.panel
        Row { id: statusRow; anchors.centerIn: parent; spacing: 7 * root.ui
            Rectangle { width: 8 * root.ui; height: width; radius: width/2; color: parent.parent.statusColor; anchors.verticalCenter: parent.verticalCenter }
            BodyText { id: statusText; font.pixelSize: 12 * root.ui }
        }
    }
    component SwitchControl: Item {
        id: toggle
        property bool checked: true
        signal userToggled(bool checked)
        implicitWidth: 44 * root.ui; implicitHeight: 24 * root.ui
        Rectangle { anchors.fill: parent; radius: height/2; color: toggle.checked ? root.green : "#3a3b3f"; Behavior on color { ColorAnimation { duration: 100 } }
            Rectangle { width: 18*root.ui; height: width; radius: width/2; color: "white"; y: (parent.height-height)/2; x: toggle.checked ? parent.width-width-3*root.ui : 3*root.ui; Behavior on x { NumberAnimation { duration: 100 } } }
        }
        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: { toggle.checked = !toggle.checked; toggle.userToggled(toggle.checked) } }
    }
    component Card: Rectangle {
        id: card
        property string title: ""
        default property alias content: body.data
        radius: 12 * root.ui; color: root.panel; border.width: 1; border.color: Qt.rgba(.25,.28,.33,.55)
        implicitHeight: body.implicitHeight + 34 * root.ui
        Column { id: body; anchors.fill: parent; anchors.margins: 18 * root.ui; spacing: 10 * root.ui
            BodyText { visible: card.title.length > 0; text: card.title; font.family: "Segoe UI"; font.weight: Font.DemiBold; font.pixelSize: 15 * root.ui }
            Rectangle { visible: card.title.length > 0; width: parent.width; height: 1; color: Qt.rgba(.25,.28,.33,.45) }
        }
    }
    component SettingRow: Item {
        id: row
        property string title: ""
        property string subtitle: ""
        property string value: ""
        property bool valueInteractive: false
        property bool showSwitch: false
        property bool checked: true
        signal toggled(bool checked)
        signal valueClicked()
        implicitHeight: 54 * root.ui
        BodyText { text: row.title; anchors.left: parent.left; anchors.top: parent.top; font.family: "Segoe UI"; font.weight: Font.DemiBold }
        MutedText { text: row.subtitle; anchors.left: parent.left; anchors.top: parent.top; anchors.topMargin: 23*root.ui; width: parent.width - 180*root.ui; elide: Text.ElideRight }
        BodyText {
            id: valueText
            visible: row.value.length > 0
            text: row.value
            color: root.primary
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
        }
        MouseArea {
            visible: row.valueInteractive && valueText.visible
            anchors.fill: valueText
            cursorShape: Qt.PointingHandCursor
            onClicked: row.valueClicked()
        }
        SwitchControl { visible: row.showSwitch; checked: row.checked; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; onUserToggled: v => row.toggled(v) }
        Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 1; color: Qt.rgba(.25,.28,.33,.35) }
    }
    component ProgressLine: Rectangle {
        property real value: 0
        property color fillColor: root.primary
        implicitHeight: 8 * root.ui; radius: height/2; color: root.lowest
        Rectangle { width: parent.width * Math.max(0, Math.min(1, parent.value)); height: parent.height; radius: height/2; color: parent.fillColor }
    }
    component AppSlider: Slider {
        id: slider
        implicitHeight: 24 * root.ui
        background: Rectangle {
            x: slider.leftPadding; y: slider.topPadding + slider.availableHeight/2-height/2
            width: slider.availableWidth; height: 5*root.ui; radius: height/2; color: "#353437"
            Rectangle { width: slider.visualPosition*parent.width; height: parent.height; radius: parent.radius; color: root.primary }
        }
        handle: Rectangle {
            x: slider.leftPadding + slider.visualPosition*(slider.availableWidth-width)
            y: slider.topPadding + slider.availableHeight/2-height/2
            width: 18*root.ui; height: width; radius: width/2; color: root.primary
        }
    }
    component AppComboBox: ComboBox {
        id: combo
        implicitHeight: 36 * root.ui
        leftPadding: 12 * root.ui
        rightPadding: 36 * root.ui
        font.family: "Segoe UI"
        font.pixelSize: 13 * root.ui
        contentItem: Text {
            text: combo.displayText
            color: root.textMain
            font: combo.font
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        indicator: Text {
            text: "⌄"
            color: root.textMuted
            font.family: "Segoe UI"
            font.pixelSize: 18 * root.ui
            anchors.right: parent.right
            anchors.rightMargin: 12 * root.ui
            anchors.verticalCenter: parent.verticalCenter
        }
        background: Rectangle {
            radius: 7 * root.ui
            color: combo.pressed ? "#303136" : root.raised
            border.width: 1
            border.color: combo.activeFocus ? root.primary : root.border
        }
        delegate: ItemDelegate {
            required property var modelData
            width: combo.width
            height: 36 * root.ui
            contentItem: Text {
                text: combo.textRole.length > 0 ? modelData[combo.textRole] : modelData
                color: root.textMain
                font: combo.font
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
            background: Rectangle {
                radius: 5 * root.ui
                color: highlighted ? Qt.rgba(.04, .52, 1, .22) : "transparent"
            }
        }
        popup: Popup {
            y: combo.height + 4 * root.ui
            width: combo.width
            implicitHeight: Math.min(contentItem.implicitHeight + 10 * root.ui, 260 * root.ui)
            padding: 5 * root.ui
            contentItem: ListView {
                clip: true
                implicitHeight: contentHeight
                model: combo.popup.visible ? combo.delegateModel : null
                currentIndex: combo.highlightedIndex
                ScrollIndicator.vertical: ScrollIndicator { }
            }
            background: Rectangle {
                radius: 8 * root.ui
                color: root.panel
                border.width: 1
                border.color: root.border
            }
        }
    }

    Rectangle {
        id: sidebar
        width: root.sideWidth; anchors.top: parent.top; anchors.bottom: parent.bottom
        color: root.lowest
        Column {
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
            anchors.margins: 15 * root.ui; spacing: 16 * root.ui
            Row { spacing: 10 * root.ui; height: 46 * root.ui
                Image {
                    width: 40 * root.ui
                    height: width
                    source: "../assets/final_icon_64.png"
                    fillMode: Image.PreserveAspectFit
                    mipmap: true
                    smooth: true
                }
                Column { anchors.verticalCenter: parent.verticalCenter; spacing: 2
                    BodyText { text: "Kỹ sư Đua xe AI"; font.family: "Segoe UI"; font.weight: Font.DemiBold; font.pixelSize: 15*root.ui }
                    MutedText { text: backend.simulatorName; width: root.sideWidth-80*root.ui; elide: Text.ElideRight; font.pixelSize: 11*root.ui }
                }
            }
            Column { width: parent.width; spacing: 4 * root.ui
                Repeater { model: [
                        { label: "Bảng điều khiển", icon: "\uE80F" },
                        { label: "Kỹ sư", icon: "\uE720" },
                        { label: "Telemetry", icon: "\uE9D2" },
                        { label: "Trí tuệ nhân tạo", icon: "\uE781" },
                        { label: "Cài đặt", icon: "\uE713" }
                    ]
                    delegate: Rectangle {
                        required property int index; required property var modelData
                        width: parent.width; height: 46 * root.ui; radius: 8 * root.ui
                        color: root.currentPage === index ? root.blue : navMouse.containsMouse ? root.panel : "transparent"
                        Text { text: modelData.icon; x: 12*root.ui; anchors.verticalCenter: parent.verticalCenter; color: root.currentPage===index ? "#002957" : "#c0c6d6"; font.family: "Segoe Fluent Icons"; font.pixelSize: 18*root.ui }
                        BodyText { text: modelData.label; x: 42*root.ui; anchors.verticalCenter: parent.verticalCenter; color: root.currentPage===index ? "#002957" : root.textMain; font.family: "Segoe UI"; font.weight: root.currentPage===index ? Font.DemiBold : Font.Normal }
                        MouseArea { id: navMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.currentPage=index }
                    }
                }
            }
        }
        Column { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 20*root.ui; spacing: 8*root.ui
            StatusPill { anchors.horizontalCenter: parent.horizontalCenter; text: backend.connected ? "Đã kết nối ACC" : "Đang chờ AC / ACC"; statusColor: backend.connected ? root.green : root.amber }
            MutedText { anchors.horizontalCenter: parent.horizontalCenter; text: "v0.1.0  •  Sẵn sàng"; font.pixelSize: 11*root.ui }
        }
    }

    Rectangle {
        id: header
        x: sidebar.width; width: root.width-sidebar.width; height: root.headerHeight; color: root.lowest
        Row { anchors.left: parent.left; anchors.leftMargin: 30*root.ui; anchors.verticalCenter: parent.verticalCenter; spacing: 10*root.ui
            BodyText { text: "Phiên trực tiếp"; font.family: "Segoe UI"; font.weight: Font.DemiBold; font.pixelSize: 15*root.ui }
            Rectangle { color: root.raised; radius: 4*root.ui; width: trackText.implicitWidth+12*root.ui; height: 26*root.ui
                MutedText { id: trackText; anchors.centerIn: parent; text: backend.telemetry.track ? backend.telemetry.track.toUpperCase() : "CHỜ SIMULATOR"; font.family: "Cascadia Mono"; font.pixelSize: 11*root.ui }
            }
        }
        Row { anchors.right: windowButtons.left; anchors.rightMargin: 14*root.ui; anchors.verticalCenter: parent.verticalCenter; spacing: 8*root.ui
            StatusPill { text: backend.connected ? "Đã kết nối ACC" : "Chưa kết nối"; statusColor: backend.connected ? root.green : root.amber }
            StatusPill { text: backend.voiceStatus === "Listening" ? "Đang lắng nghe" : "Radio sẵn sàng"; statusColor: root.primary }
            StatusPill { text: backend.apiConfigured ? "AI Trực tuyến" : "AI Chưa cấu hình"; statusColor: backend.apiConfigured ? root.green : root.amber }
        }
        Row { id: windowButtons; anchors.right: parent.right; anchors.top: parent.top; height: parent.height
            Repeater { model: ["—", "□", "×"]
                delegate: Rectangle { required property int index; required property string modelData; width: 46*root.ui; height: header.height; color: winMouse.containsMouse ? (index===2 ? "#c42b1c" : root.panel) : "transparent"
                    BodyText { anchors.centerIn: parent; text: modelData; font.pixelSize: index===1 ? 16*root.ui : 19*root.ui }
                    MouseArea { id: winMouse; anchors.fill: parent; hoverEnabled: true; onClicked: { if(index===0) root.showMinimized(); else if(index===1) root.visibility===Window.Maximized ? root.showNormal() : root.showMaximized(); else root.close() } }
                }
            }
        }
        DragHandler { target: null; onActiveChanged: if(active) root.startSystemMove() }
    }

    Loader { id: pageLoader; x: sidebar.width; y: header.height; width: root.width-sidebar.width; height: root.height-header.height
        sourceComponent: [dashboardPage, engineerPage, telemetryPage, aiPage, settingsPage][root.currentPage]
    }

    component PageScroll: ScrollView {
        clip: true; ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        contentWidth: availableWidth
        background: Rectangle { color: root.bg }
    }

    Component { id: dashboardPage
        PageScroll { Column { width: parent.width; padding: 28*root.ui; spacing: 16*root.ui
            Card { width: parent.width-56*root.ui; height: 86*root.ui; title: ""
                RowLayout { width: parent.width; height: parent.height; spacing: 35*root.ui
                    Column { MutedText { text: "VỊ TRÍ" } BodyText { text: "P"+(backend.telemetry.position||"—"); font.pixelSize: 20*root.ui; font.family: "Cascadia Mono" } }
                    Column { MutedText { text: "TIẾN ĐỘ CHẶNG" } BodyText { text: "Vòng "+(backend.telemetry.currentLap||"—"); font.pixelSize: 17*root.ui } }
                    Column { MutedText { text: "NHIÊN LIỆU" } BodyText { text: (backend.telemetry.fuelLiters||"—")+" L"; color: root.green; font.pixelSize: 17*root.ui } }
                    Item { Layout.fillWidth: true }
                    StatusPill {
                        text: backend.apiState === "Connected" ? "Kỹ sư AI: Sẵn sàng" : ("Kỹ sư AI: " + backend.apiState)
                        statusColor: backend.apiState === "Connected" ? root.green : root.amber
                    }
                }
            }
            RowLayout { width: parent.width-56*root.ui; spacing: 16*root.ui
                Card { Layout.fillWidth: true; Layout.preferredWidth: 720; Layout.preferredHeight: 620*root.ui; Layout.alignment: Qt.AlignTop; title: "Nhật ký đàm thoại vô tuyến"
                    Row { spacing: 8*root.ui; StatusPill { text: "Kênh ACC Radio"; statusColor: root.primary } StatusPill { text: backend.voiceStatus; statusColor: backend.voiceStatus==="Listening"?root.green:root.primary } StatusPill { text: "HẠ ÂM GAME"; statusColor: root.amber; visible: backend.isAudioDucked } }
                    Rectangle { width: parent.width; height: 72*root.ui; radius: 10*root.ui; color: root.bg; border.color: Qt.rgba(.25,.28,.33,.45)
                        Row { anchors.left: parent.left; anchors.leftMargin: 15*root.ui; anchors.verticalCenter: parent.verticalCenter; spacing: 12*root.ui
                            Rectangle { width: 38*root.ui; height: width; radius: width/2; color: "#123d26"; border.color: root.green; BodyText { anchors.centerIn: parent; text: "MIC"; color: root.green; font.pixelSize: 9*root.ui } }
                            Column { BodyText { text: backend.voiceStatus==="Listening" ? "Micro đang mở • Đang nhận diện" : "Radio sẵn sàng • Giữ PTT để nói"; font.family: "Segoe UI"; font.weight: Font.DemiBold } MutedText { text: backend.microphoneName } }
                        }
                    }
                    Item { width: parent.width; height: 370*root.ui
                        ListView {
                            id: conversationList
                            anchors.fill: parent
                            clip: true
                            spacing: 10*root.ui
                            model: backend.conversationLog
                            onCountChanged: Qt.callLater(function() { positionViewAtEnd() })
                            delegate: Item {
                                required property var modelData
                                readonly property bool isDriver: modelData.role === "driver"
                                width: conversationList.width
                                height: speakerLabel.implicitHeight + bubble.height + 8*root.ui
                                BodyText {
                                    id: speakerLabel
                                    text: isDriver ? "TAY ĐUA" : "KỸ SƯ AI"
                                    color: isDriver ? root.textMuted : root.primary
                                    font.pixelSize: 12*root.ui
                                    font.weight: Font.DemiBold
                                    anchors.top: parent.top
                                    anchors.right: isDriver ? parent.right : undefined
                                    anchors.left: isDriver ? undefined : parent.left
                                }
                                Rectangle {
                                    id: bubble
                                    width: parent.width * (isDriver ? .72 : .78)
                                    height: bubbleText.implicitHeight + 24*root.ui
                                    radius: 14*root.ui
                                    color: isDriver ? root.raised : "#2b2d35"
                                    border.color: isDriver ? "transparent" : "#596178"
                                    anchors.top: speakerLabel.bottom
                                    anchors.topMargin: 5*root.ui
                                    anchors.right: isDriver ? parent.right : undefined
                                    anchors.left: isDriver ? undefined : parent.left
                                    BodyText {
                                        id: bubbleText
                                        text: modelData.text
                                        width: parent.width - 28*root.ui
                                        anchors.centerIn: parent
                                        wrapMode: Text.Wrap
                                    }
                                }
                            }
                        }
                        Column { visible: conversationList.count === 0; anchors.centerIn: parent; spacing: 8*root.ui
                            HeadingText { text: "Sẵn sàng nhận lệnh radio"; anchors.horizontalCenter: parent.horizontalCenter }
                            MutedText { text: "Giữ PTT hoặc nhập câu hỏi cho kỹ sư AI"; anchors.horizontalCenter: parent.horizontalCenter }
                        }
                    }
                    RowLayout { width: parent.width; spacing: 10*root.ui
                        TextField { id: command; Layout.fillWidth: true; placeholderText: "Nhập khẩu lệnh hoặc giữ nút vô lăng để nói..."; color: root.textMain; placeholderTextColor: root.textMuted; font.pixelSize: 14*root.ui; background: Rectangle { radius: 8*root.ui; color: root.bg; border.color: Qt.rgba(.25,.28,.33,.45) } onAccepted: { if(text.trim()) backend.askText(text.trim()); text="" } }
                        AppButton { text: "Gửi"; highlighted: true; onClicked: { if(command.text.trim()) backend.askText(command.text.trim()); command.text="" } }
                        AppButton { text: "GIỮ ĐỂ NÓI"; onPressed: backend.beginPushToTalk(); onReleased: backend.endPushToTalk() }
                    }
                }
                ColumnLayout { Layout.preferredWidth: 490; Layout.alignment: Qt.AlignTop; spacing: 16*root.ui
                    Card { Layout.fillWidth: true; Layout.preferredHeight: 246*root.ui; title: "Cài đặt nhanh buồng lái"
                        SettingRow {
                            id: dashboardEngineerMode
                            width: parent.width
                            title: "Chế độ Kỹ sư"
                            subtitle: backend.responseStyle === "Tối giản"
                                ? "Ngắn gọn, ưu tiên tốc độ đua"
                                : backend.responseStyle === "Chi tiết"
                                    ? "Phân tích đầy đủ cho từng tình huống"
                                    : "Cân bằng giữa tốc độ và thông tin"
                            value: backend.responseStyle === "Tối giản" ? "Ngắn" : backend.responseStyle
                            valueInteractive: true
                            onValueClicked: {
                                var styles = ["Tối giản", "Tiêu chuẩn", "Chi tiết"]
                                var current = styles.indexOf(backend.responseStyle)
                                backend.setResponseStyle(styles[(current + 1 + styles.length) % styles.length])
                            }
                        }
                        SettingRow { width: parent.width; title: "Tự động hạ âm game"; subtitle: "Giảm tiếng động cơ khi Radio nói (-12dB)"; showSwitch: true; checked: backend.audioDuckingEnabled; onToggled: checked => backend.setAudioDuckingEnabled(checked) }
                        Item { width: parent.width; height: 42*root.ui
                            RowLayout { anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                                BodyText { text: "Âm lượng Kỹ sư AI" }
                                Item { Layout.fillWidth: true }
                                MutedText { text: Math.round(backend.ttsVolume * 100) + "%" }
                            }
                            AppSlider {
                                width: parent.width - 18*root.ui
                                anchors.horizontalCenter: parent.horizontalCenter
                                anchors.bottom: parent.bottom
                                from: 0
                                to: 1
                                value: backend.ttsVolume
                                onMoved: backend.setTtsVolume(value)
                            }
                        }
                    }
                    Card { Layout.fillWidth: true; Layout.preferredHeight: 145*root.ui; title: "TRẠNG THÁI LIÊN KẾT HỆ THỐNG"
                        Row { spacing: 8*root.ui; StatusPill { text: backend.connected?"ACC UDP 9000":"SIMULATOR OFF"; statusColor: backend.connected?root.green:root.amber } StatusPill { text: "DIRECTINPUT"; statusColor: root.green } StatusPill { text: backend.apiState; statusColor: backend.apiConfigured?root.green:root.amber } }
                    }
                }
            }
        } }
    }

    Component { id: engineerPage
        PageScroll { Column { width: parent.width; padding: 28*root.ui; spacing: 14*root.ui
            HeadingText { text: "Thiết lập Kỹ sư & Cảnh giới" }
            MutedText { text: "Tùy chỉnh cá tính giọng nói, đề xuất chiến thuật chủ động và cảnh báo khoảng cách xe." }
            RowLayout { width: parent.width-56*root.ui; spacing: 12*root.ui
                Repeater { model: [{t: backend.ttsBackend === "VieNeu-TTS" ? "Động cơ VieNeu v3 Turbo" : "Động cơ Piper", s:backend.ttsStatus, c:root.green},{t:"Radar không gian",s:"Luồng ACC gốc",c:root.green},{t:"PhoWhisper ASR",s:"Q5 • Cục bộ",c:root.amber}]
                    delegate: Card { required property var modelData; Layout.fillWidth: true; height: 78*root.ui; title: modelData.t; BodyText { text: modelData.s; color: modelData.c; font.pixelSize: 12*root.ui } }
                }
            }
            MutedText { text: "KỸ SƯ ĐUA XE AI" }
            Card { width: parent.width-56*root.ui; title: ""
                Item { width:parent.width; height:54*root.ui
                    Column { anchors.left:parent.left; anchors.verticalCenter:parent.verticalCenter; spacing:2*root.ui
                        BodyText{text:"Động cơ Giọng nói (TTS Backend)"; font.family:"Segoe UI"; font.weight:Font.DemiBold}
                        MutedText{text:"Lựa chọn hệ thống phát giọng nói của kỹ sư"}
                    }
                    AppComboBox {
                        id:engTtsEngineCombo; width:340*root.ui; anchors.right:parent.right; anchors.verticalCenter:parent.verticalCenter
                        model: ["Piper (Mặc định · Tối ưu độ trễ)", "VieNeu-TTS v3 Turbo (Tiếng Việt · Native)"]
                        currentIndex: backend.ttsBackend === "VieNeu-TTS" ? 1 : 0
                        onActivated: backend.setTtsBackend(index === 1 ? "VieNeu-TTS" : "Piper")
                    }
                    Rectangle { anchors.left:parent.left; anchors.right:parent.right; anchors.bottom:parent.bottom; height:1; color:Qt.rgba(.25,.28,.33,.35) }
                }
                Item {
                    width: parent.width
                    height: 54 * root.ui
                    visible: backend.ttsBackend === "VieNeu-TTS"
                    Column {
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 2 * root.ui
                        BodyText { text: "Giọng Kỹ sư AI (VieNeu)"; font.family: "Segoe UI"; font.weight: Font.DemiBold }
                        MutedText { text: "Lựa chọn chất giọng và phong cách đàm thoại của kỹ sư" }
                    }
                    AppComboBox {
                        id: engTtsVoiceCombo
                        width: 340 * root.ui
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        textRole: "name"
                        model: backend.availableTtsVoices
                        currentIndex: {
                            for (var i = 0; i < backend.availableTtsVoices.length; ++i) {
                                if (backend.availableTtsVoices[i].id === backend.selectedTtsVoice)
                                    return i
                            }
                            return 0
                        }
                        onActivated: backend.setTtsVoice(backend.availableTtsVoices[index].id)
                    }
                    Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 1; color: Qt.rgba(.25, .28, .33, .35) }
                }
                Item {
                    width: parent.width
                    implicitHeight: 54 * root.ui
                    BodyText { text: "Tên gọi của tay đua"; anchors.left: parent.left; anchors.top: parent.top; font.family: "Segoe UI"; font.weight: Font.DemiBold }
                    MutedText { text: "Cách Kỹ sư AI xưng hô với bạn qua radio"; anchors.left: parent.left; anchors.top: parent.top; anchors.topMargin: 23*root.ui; width: parent.width - 240*root.ui; elide: Text.ElideRight }
                    TextField {
                        id: driverNameInput
                        width: 200 * root.ui
                        height: 36 * root.ui
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        text: backend.driverName
                        color: root.textMain
                        horizontalAlignment: Text.AlignRight
                        font.family: "Segoe UI"
                        font.pixelSize: 13 * root.ui
                        placeholderText: "Nhập tên..."
                        placeholderTextColor: root.textMuted
                        background: Rectangle {
                            color: root.bg
                            radius: 8 * root.ui
                            border.color: driverNameInput.activeFocus ? root.primary : root.border
                            border.width: driverNameInput.activeFocus ? 1.5 : 1
                        }
                        onEditingFinished: {
                            backend.setDriverName(driverNameInput.text)
                        }
                    }
                    Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 1; color: Qt.rgba(.25,.28,.33,.35) }
                }
                Item {
                    width: parent.width
                    implicitHeight: 54 * root.ui
                    BodyText {
                        text: "Phong cách phản hồi"
                        anchors.left: parent.left
                        anchors.top: parent.top
                        font.family: "Segoe UI"
                        font.weight: Font.DemiBold
                    }
                    MutedText {
                        text: "Độ súc tích của thông tin vô tuyến"
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.topMargin: 23 * root.ui
                        width: parent.width - 290 * root.ui
                        elide: Text.ElideRight
                    }
                    Rectangle {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        height: 34 * root.ui
                        width: 246 * root.ui
                        radius: 8 * root.ui
                        color: root.lowest
                        border.color: root.border
                        border.width: 1

                        Row {
                            anchors.centerIn: parent
                            spacing: 3 * root.ui
                            Repeater {
                                model: ["Tối giản", "Tiêu chuẩn", "Chi tiết"]
                                delegate: Rectangle {
                                    id: pillBtn
                                    required property string modelData
                                    property bool selected: backend.responseStyle === modelData
                                    width: 78 * root.ui
                                    height: 28 * root.ui
                                    radius: 6 * root.ui
                                    color: selected ? root.raised : (pillHover.containsMouse ? Qt.rgba(1, 1, 1, 0.06) : "transparent")
                                    border.color: selected ? Qt.rgba(1, 1, 1, 0.2) : "transparent"
                                    border.width: selected ? 1 : 0

                                    Text {
                                        anchors.centerIn: parent
                                        text: pillBtn.modelData
                                        color: pillBtn.selected ? root.textMain : (pillHover.containsMouse ? root.textMain : root.textMuted)
                                        font.family: "Segoe UI"
                                        font.pixelSize: 12 * root.ui
                                        font.weight: pillBtn.selected ? Font.DemiBold : Font.Normal
                                    }
                                    MouseArea {
                                        id: pillHover
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: backend.setResponseStyle(pillBtn.modelData)
                                    }
                                }
                            }
                        }
                    }
                    Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 1; color: Qt.rgba(.25,.28,.33,.35) }
                }
                Repeater { model: [{t:"Đề xuất chiến thuật chủ động",s:"Cho phép AI tự động đưa ra chỉ dẫn chiến thuật",x:true},{t:"Cập nhật nhiên liệu",s:"Thời điểm nhả ga và lượng xăng đến đích",x:true},{t:"Cảnh báo Lốp & Độ bám",s:"Nhiệt độ, độ mòn và độ bám",x:true},{t:"Phân tích Delta vòng đua",s:"So sánh chênh lệch thời gian",x:true},{t:"Tự động tính lại chiến thuật Pit",s:"Điều chỉnh cửa sổ vào pit",x:true}]
                    delegate: SettingRow { required property var modelData; width: parent.width; title:modelData.t; subtitle:modelData.s; value:modelData.v||""; showSwitch:modelData.x===true }
                }
            }
            MutedText { text: "CẢNH GIỚI ÂM THANH (SPOTTER)" }
            Card { width: parent.width-56*root.ui; title: ""
                Repeater { model: [{t:"Kích hoạt Cảnh giới",s:"Cảnh báo âm thanh cho các xe kế bên"},{t:"Âm lượng Cảnh giới",s:"Âm lượng phát cục bộ"},{t:"Xe Trái / Xe Phải",s:"Radar 360 độ"},{t:"Cờ hiệu & Nguy hiểm chặng",s:"Cờ vàng, xanh lá và xanh dương"},{t:"Thông báo cơ học khẩn cấp",s:"Ưu tiên trước phản hồi AI"}]
                    delegate: SettingRow { required property var modelData; width: parent.width; title:modelData.t; subtitle:modelData.s; showSwitch:true }
                }
            }
            MutedText { text: "HÀNH VI TƯƠNG TÁC" }
            Card { width: parent.width-56*root.ui; title: ""
                SettingRow { width: parent.width; title:"Phím tắt Nói (PTT)"; subtitle:"Nhấn giữ để trò chuyện"; value:backend.directInputBinding }
                SettingRow { width: parent.width; title:"PTT bàn phím"; subtitle:"Phím cách (Spacebar)"; showSwitch:true; checked:backend.keyboardPttEnabled; onToggled: backend.setPushToTalkOptions(checked,backend.directInputPttEnabled) }
                SettingRow { width: parent.width; title:"PTT DirectInput"; subtitle:"Vô lăng hoặc tay cầm"; showSwitch:true; checked:backend.directInputPttEnabled; onToggled: backend.setPushToTalkOptions(backend.keyboardPttEnabled,checked) }
                AppButton { text:"Gán lại nút"; onClicked:backend.beginDirectInputMapping() }
            }
            Item { width:1; height:20*root.ui }
        } }
    }

    Component { id: telemetryPage
        PageScroll { Column { width:parent.width; padding:28*root.ui; spacing:16*root.ui
            Row { spacing:10*root.ui; StatusPill { text:backend.simulatorName; statusColor:backend.connected?root.green:root.amber } StatusPill { text:backend.connected?"Trực tiếp (Shared Memory)":"Đang chờ dữ liệu"; statusColor:backend.connected?root.green:root.amber } BodyText { text:"Trường đua: "+(backend.telemetry.track||"—"); anchors.verticalCenter:parent.verticalCenter } }
            RowLayout { width:parent.width-56*root.ui; spacing:16*root.ui
                Card { Layout.fillWidth:true; Layout.preferredWidth:720; Layout.preferredHeight:505*root.ui; title:"Động lực học & Thao tác lái"
                    RowLayout {
                        width: parent.width
                        spacing: 28 * root.ui
                        ColumnLayout {
                            Layout.preferredWidth: 150 * root.ui
                            spacing: 4 * root.ui
                            MutedText { text: "SỐ ĐANG GÀI"; Layout.fillWidth: true }
                            MonoText { text: backend.telemetry.gear || "—"; color: root.primary; font.pixelSize: 44*root.ui }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4 * root.ui
                            MutedText { text: "TỐC ĐỘ ĐỘNG CƠ"; Layout.fillWidth: true }
                            MonoText { text: (backend.telemetry.rpm || "—") + " RPM"; font.pixelSize: 26*root.ui }
                            ProgressLine { Layout.fillWidth: true; value: (backend.telemetry.rpm || 0)/8500; fillColor: root.amber }
                        }
                    }
                    SettingRow { width:parent.width; title:"Vận tốc di chuyển"; value:(backend.telemetry.speedKmh||"—")+" km/h" }
                    ProgressLine { width:parent.width; value:(backend.telemetry.speedKmh||0)/330 }
                    SettingRow { width:parent.width; title:"Bướm ga (Throttle)"; value:Math.round((backend.telemetry.throttle||0)*100)+"%" }
                    ProgressLine { width:parent.width; value:backend.telemetry.throttle||0; fillColor:root.green }
                    SettingRow { width:parent.width; title:"Áp lực phanh"; value:Math.round((backend.telemetry.brake||0)*100)+"%" }
                    ProgressLine { width:parent.width; value:backend.telemetry.brake||0; fillColor:"#ff9a96" }
                }
                ColumnLayout { Layout.preferredWidth:490; spacing:16*root.ui
                    Card { Layout.fillWidth:true; Layout.preferredHeight:400*root.ui; title:"Diễn biến chặng đua"
                        RowLayout { width:parent.width; MonoText{text:"P"+(backend.telemetry.position||"—");color:root.primary;font.pixelSize:34*root.ui} BodyText{text:"+ Vị trí";color:root.green} Item{Layout.fillWidth:true} BodyText{text:"Vòng "+(backend.telemetry.currentLap||"—")} }
                        ProgressLine { width:parent.width; value:backend.telemetry.lapProgress||0 }
                        SettingRow { width:parent.width; title:"KHOẢNG CÁCH XE TRƯỚC"; value:(backend.telemetry.gapAheadSeconds||"—")+" s" }
                        SettingRow { width:parent.width; title:"KHOẢNG CÁCH XE SAU"; value:(backend.telemetry.gapBehindSeconds||"—")+" s" }
                        MutedText { text:backend.latestEvent||"Khô ráo • Telemetry thời gian thực" }
                    }
                    Card { Layout.fillWidth:true; Layout.preferredHeight:90*root.ui; title:"Thời gian vòng mục tiêu"; MonoText { text:"--:--.---"; anchors.right:parent.right } }
                }
            }
            Card { width:parent.width-56*root.ui; height:235*root.ui; title:"Tính toán Nhiên liệu & Ma trận Chiến thuật"
                RowLayout { width:parent.width; spacing:10*root.ui
                    Repeater { model:[{t:"MỨC NHIÊN LIỆU",v:(backend.telemetry.fuelLiters||"—")+" L"},{t:"TIÊU THỤ TRUNG BÌNH",v:"— L / vòng"},{t:"KHẢ NĂNG CHẠY",v:"— Vòng"},{t:"KHUYẾN NGHỊ VÀO PIT",v:"Theo chiến thuật"}]
                        delegate: Rectangle { required property var modelData; Layout.fillWidth:true; height:140*root.ui; radius:8*root.ui;color:root.raised; Column{anchors.fill:parent;anchors.margins:14*root.ui;spacing:14*root.ui;MutedText{text:modelData.t} MonoText{text:modelData.v;color:modelData.t==="KHẢ NĂNG CHẠY"?root.primary:root.textMain}} }
                    }
                }
            }
        } }
    }

    Component { id: aiPage
        PageScroll {
            id: aiScroll
            readonly property bool isModified: {
                if (typeof endpoint === "undefined" || !endpoint ||
                    typeof modelName === "undefined" || !modelName ||
                    typeof key === "undefined" || !key) return false;
                return (endpoint.text.trim() !== backend.apiBaseUrl.trim())
                    || (modelName.text.trim() !== backend.apiModel.trim())
                    || (key.text !== backend.apiKey)
                    || (typeof streamingSwitch !== "undefined" && streamingSwitch && streamingSwitch.checked !== backend.apiStreaming)
                    || (typeof reasoningSwitch !== "undefined" && reasoningSwitch && reasoningSwitch.checked !== backend.apiReasoning)
                    || (typeof temperatureControl !== "undefined" && temperatureControl && Math.abs(temperatureControl.value - backend.apiTemperature) > 0.01)
                    || (typeof tokenLimit !== "undefined" && tokenLimit && Math.round(tokenLimit.value) !== backend.apiMaximumTokens);
            }

            Column { width:parent.width; padding:28*root.ui; spacing:14*root.ui
                HeadingText { text:"Máy chủ AI & Mô hình Cục bộ" }
                MutedText { text:"Cấu hình cổng dịch vụ, mô hình và tham số suy luận chiến thuật thời gian thực." }
                RowLayout { width:parent.width-56*root.ui; spacing:16*root.ui
                    ColumnLayout { Layout.fillWidth:true; Layout.preferredWidth:1; Layout.alignment:Qt.AlignTop; spacing:14*root.ui
                        MutedText { text:"TRẠNG THÁI MÁY CHỦ & KẾT NỐI" }
                        Card { Layout.fillWidth:true; Layout.preferredHeight:215*root.ui; title:"OpenAI Compatible API"
                            StatusPill {
                                text: backend.apiState
                                statusColor: backend.apiState === "Connected" ? root.green :
                                             backend.apiState === "Requesting" ? root.primary :
                                             (!backend.apiConfigured || backend.apiState === "Unavailable") ? root.amber : "#ff6b6b"
                            }
                            SettingRow { width:parent.width; title:"MÔ HÌNH SUY LUẬN"; value:backend.apiModel }
                            MutedText { text:backend.apiDetail; elide:Text.ElideRight; width:parent.width }
                        }
                        MutedText { text:"THÔNG SỐ MÁY CHỦ & GIAO THỨC" }
                        Card { Layout.fillWidth:true; Layout.preferredHeight:410*root.ui; title:"Cấu hình kết nối"
                            MutedText { text:"Cổng Endpoint API" }
                            TextField { id:endpoint; width:parent.width; text:backend.apiBaseUrl; color:root.textMain; background:Rectangle{color:root.bg;radius:8*root.ui;border.color:root.border} }
                            MutedText { text:"Mô hình suy luận" }
                            TextField { id:modelName; width:parent.width; text:backend.apiModel; color:root.textMain; background:Rectangle{color:root.bg;radius:8*root.ui;border.color:root.border} }
                            MutedText { text:"Khóa API" }
                            TextField { id:key; width:parent.width; text:backend.apiKey; echoMode:TextInput.Password; color:root.textMain; background:Rectangle{color:root.bg;radius:8*root.ui;border.color:root.border} }
                            Row {
                                spacing: 10 * root.ui
                                AppButton {
                                    id: testBtn
                                    text: backend.apiState === "Requesting" ? "Đang kết nối..." : "Kiểm tra Kết nối"
                                    enabled: aiScroll.isModified && backend.apiState !== "Requesting"
                                    highlighted: aiScroll.isModified
                                    SequentialAnimation on scale {
                                        running: aiScroll.isModified
                                        loops: Animation.Infinite
                                        PropertyAnimation { to: 1.03; duration: 600; easing.type: Easing.InOutQuad }
                                        PropertyAnimation { to: 1.0; duration: 600; easing.type: Easing.InOutQuad }
                                    }
                                    onClicked: backend.testApiConnection(endpoint.text, key.text, modelName.text)
                                }
                                AppButton {
                                    id: saveBtn
                                    text: "Lưu cấu hình"
                                    highlighted: !aiScroll.isModified
                                    onClicked: backend.saveAiSettings("OpenAI Compatible", endpoint.text, key.text, modelName.text, streamingSwitch.checked, backend.apiTimeoutMilliseconds, Math.round(tokenLimit.value), temperatureControl.value, reasoningSwitch.checked)
                                }
                            }
                        }
                }
                ColumnLayout { Layout.fillWidth:true; Layout.preferredWidth:1; Layout.alignment:Qt.AlignTop; spacing:14*root.ui
                    MutedText { text:"KIẾN TRÚC SUY LUẬN & ĐƯỜNG TRUYỀN" }
                    Card { Layout.fillWidth:true; Layout.preferredHeight:215*root.ui; title:""
                        SettingRow { width:parent.width; title:"Hỗ trợ Gọi hàm (Tool Calling)"; subtitle:"Công cụ telemetry và chiến thuật"; showSwitch:true }
                        SettingRow { id:streamingSwitch; width:parent.width; title:"Truyền dữ liệu dạng luồng (Streaming)"; subtitle:"Luồng phản hồi SSE"; showSwitch:true; checked:backend.apiStreaming }
                        SettingRow { id:reasoningSwitch; width:parent.width; title:"Chế độ Suy luận Chuyên sâu"; subtitle:"Khóa tối ưu độ trễ thấp (Reasoning/Thinking)"; showSwitch:true; checked:backend.apiReasoning; onToggled:(checked) => backend.setApiReasoning(checked) }
                    }
                    MutedText { text:"THAM SỐ SUY LUẬN CHIẾN THUẬT" }
                    Card { Layout.fillWidth:true; Layout.preferredHeight:410*root.ui; title:"Tham số Nâng cao"
                        RowLayout { width:parent.width; BodyText{text:"Độ ngẫu nhiên (Temperature)"} Item{Layout.fillWidth:true} MutedText{text:temperatureControl.value.toFixed(2)} }
                        AppSlider{id:temperatureControl; width:parent.width; from:0; to:1; value:backend.apiTemperature}
                        RowLayout { width:parent.width; BodyText{text:"Số Token Xuất Tối đa"} Item{Layout.fillWidth:true} MutedText{text:Math.round(tokenLimit.value)} }
                        AppSlider{id:tokenLimit; width:parent.width; from:16; to:128; stepSize:1; value:backend.apiMaximumTokens}
                        SettingRow{width:parent.width; title:"Bộ nhớ Ngữ cảnh hội thoại"; value:"4 lượt"}
                        SettingRow{width:parent.width; title:"Thời gian Chờ Phản hồi"; value:backend.apiTimeoutMilliseconds+" ms"}
                        SettingRow{width:parent.width; title:"Quy tắc Dự phòng Cục bộ"; subtitle:"Cảnh báo khi máy chủ AI trễ"; showSwitch:true}
                    }
                }
            }
            Item{width:1; height:20*root.ui}
        } }
    }

    Component { id: settingsPage
        PageScroll { Column { width:parent.width; padding:28*root.ui; spacing:12*root.ui
            HeadingText{text:"Cài đặt"} MutedText{text:"Quản lý tích hợp trò chơi mô phỏng, DirectInput và định tuyến âm thanh."}
            MutedText{text:"TÙY CHỌN CHUNG"}
            Card { width:Math.min(1040*root.ui,parent.width-56*root.ui); title:""
                Repeater{model:[{t:"Khởi động cùng Windows",s:"Tự động chạy dịch vụ kỹ sư nền",c:false},{t:"Thu nhỏ vào Khay hệ thống",s:"Tiếp tục chạy ngầm trong phiên đua",c:true},{t:"Thu nhỏ khi đóng cửa sổ",s:"Giữ các dịch vụ đang hoạt động",c:true},{t:"Tăng tốc Phần cứng (GPU)",s:"Qt Quick renderer đang hoạt động",c:true}];delegate:SettingRow{required property var modelData;width:parent.width;title:modelData.t;subtitle:modelData.s;showSwitch:true;checked:modelData.c}}
            }
            MutedText{text:"GÁN PHÍM DIRECTINPUT (VÔ LĂNG & BÀN ĐẠP)"}
            Card { width:Math.min(1040*root.ui,parent.width-56*root.ui); title:""
                SettingRow{width:parent.width;title:"Thiết bị phát hiện";subtitle:backend.directInputStatus;value:backend.directInputBinding}
                SettingRow{width:parent.width;title:"PTT bàn phím";subtitle:"Phím cách (Spacebar)";showSwitch:true;checked:backend.keyboardPttEnabled;onToggled:backend.setPushToTalkOptions(checked,backend.directInputPttEnabled)}
                SettingRow{width:parent.width;title:"Nút đàm thoại PTT";subtitle:"Nhấn giữ trên vô lăng";showSwitch:true;checked:backend.directInputPttEnabled;onToggled:backend.setPushToTalkOptions(backend.keyboardPttEnabled,checked)}
                AppButton{text:"Gán lại";onClicked:backend.beginDirectInputMapping()}
            }
            MutedText{text:"BỘ MÁY GIỌNG NÓI & ĐỊNH TUYẾN ÂM THANH"}
            Card { width:Math.min(1040*root.ui,parent.width-56*root.ui); title:""
                Item { width:parent.width; height:54*root.ui
                    BodyText{text:"Động cơ Giọng nói (TTS Backend)";anchors.left:parent.left;anchors.verticalCenter:parent.verticalCenter;font.family:"Segoe UI";font.weight:Font.DemiBold}
                    AppComboBox {
                        id:ttsEngineCombo; width:390*root.ui; anchors.right:parent.right; anchors.verticalCenter:parent.verticalCenter
                        model: ["Piper (Mặc định · Tối ưu độ trễ)", "VieNeu-TTS v3 Turbo (Tiếng Việt · Native)"]
                        currentIndex: backend.ttsBackend === "VieNeu-TTS" ? 1 : 0
                        onActivated: backend.setTtsBackend(index === 1 ? "VieNeu-TTS" : "Piper")
                    }
                    Rectangle { anchors.left:parent.left; anchors.right:parent.right; anchors.bottom:parent.bottom; height:1; color:Qt.rgba(.25,.28,.33,.35) }
                }
                Item {
                    width: parent.width
                    height: 54 * root.ui
                    BodyText {
                        text: "Thiết bị Thu âm Mặc định"
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        font.family: "Segoe UI"
                        font.weight: Font.DemiBold
                    }
                    AppComboBox {
                        id: inputDevice
                        width: 390 * root.ui
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        textRole: "name"
                        model: backend.audioInputDevices
                        currentIndex: {
                            for (var i = 0; i < backend.audioInputDevices.length; ++i) {
                                if (backend.audioInputDevices[i].id === backend.selectedAudioInputId)
                                    return i
                            }
                            return 0
                        }
                        onPressedChanged: if (pressed) backend.refreshAudioInputDevices()
                        onActivated: backend.setAudioInputDevice(backend.audioInputDevices[index].id)
                    }
                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 1
                        color: Qt.rgba(.25, .28, .33, .35)
                    }
                }
                Item { width:parent.width; height:54*root.ui
                    BodyText{text:"Thiết bị Phát âm thanh Mặc định";anchors.left:parent.left;anchors.verticalCenter:parent.verticalCenter;font.family:"Segoe UI";font.weight:Font.DemiBold}
                    AppComboBox { id:outputDevice; width:390*root.ui; anchors.right:parent.right; anchors.verticalCenter:parent.verticalCenter; model:backend.audioOutputDevices; currentIndex:Math.max(0,backend.audioOutputDevices.indexOf(backend.selectedAudioOutput)); onActivated:backend.setAudioOutputDevice(currentText) }
                    Rectangle { anchors.left:parent.left; anchors.right:parent.right; anchors.bottom:parent.bottom; height:1; color:Qt.rgba(.25,.28,.33,.35) }
                }
                SettingRow{width:parent.width;title:"Tự động giảm âm lượng trò chơi";subtitle:"Giảm âm game khi kỹ sư AI phát giọng nói (-12dB)";showSwitch:true;checked:backend.audioDuckingEnabled;onToggled:checked => backend.setAudioDuckingEnabled(checked)}
            }
            MutedText{text:"BẢO TRÌ & NHẬT KÝ HOẠT ĐỘ"}
            Card { width:Math.min(1040*root.ui,parent.width-56*root.ui); title:""
                SettingRow{width:parent.width;title:"Trạng thái hệ thống";value:backend.connectionText}
                SettingRow{width:parent.width;title:"Máy chủ AI";subtitle:backend.apiDetail;value:backend.apiState}
                SettingRow{visible:backend.mockAvailable;width:parent.width;title:"Mock telemetry";showSwitch:true;checked:backend.mockEnabled;onToggled:backend.setUseMockTelemetry(checked)}
            }
            Item{width:1;height:28*root.ui}
        } }
    }

    MouseArea { width:7; anchors.left:parent.left; anchors.top:parent.top; anchors.bottom:parent.bottom; cursorShape:Qt.SizeHorCursor; onPressed:root.startSystemResize(Qt.LeftEdge) }
    MouseArea { width:7; anchors.right:parent.right; anchors.top:parent.top; anchors.bottom:parent.bottom; cursorShape:Qt.SizeHorCursor; onPressed:root.startSystemResize(Qt.RightEdge) }
    MouseArea { height:7; anchors.left:parent.left; anchors.right:parent.right; anchors.bottom:parent.bottom; cursorShape:Qt.SizeVerCursor; onPressed:root.startSystemResize(Qt.BottomEdge) }
}
