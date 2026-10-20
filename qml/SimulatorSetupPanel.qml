pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: panel
    required property UiTheme theme
    required property var setup
    required property bool live
    property bool telemetryConnected: false
    readonly property var setupState: setup ? setup.status : ({})
    readonly property bool canConfigure: live && !!setup && !setup.busy && !!setupState.processCheckAvailable && !setupState.running && !telemetryConnected
    spacing: 12

    component Label: Text {
        color: panel.theme.text
        font.family: panel.theme.uiFont
        font.pixelSize: panel.theme.pageTextSize(14)
        font.weight: panel.theme.uiWeight
        wrapMode: Text.Wrap
        textFormat: Text.PlainText
        Layout.fillWidth: true
    }
    component Action: Button {
        id: action
        implicitHeight: 44
        leftPadding: 14
        rightPadding: 14
        font.family: panel.theme.uiFont
        font.pixelSize: panel.theme.pageTextSize(14)
        font.weight: panel.theme.sectionWeight
        Accessible.name: text
        enabled: panel.canConfigure
        contentItem: Text { text: action.text; font: action.font; color: action.enabled ? panel.theme.text : panel.theme.muted; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
        background: Rectangle { color: action.hovered && action.enabled ? panel.theme.raised : panel.theme.canvas; border.color: action.activeFocus ? panel.theme.outline : panel.theme.divider; radius: 3 }
    }

    Label { text: "Assetto Corsa · Python companion"; font.weight: panel.theme.sectionWeight }
    Label { objectName: "acSetupStatus"; text: panel.setupState.ac ? panel.setupState.ac.status : "Chưa kiểm tra AC." }
    Label {
        text: panel.setupState.ac && panel.setupState.ac.path ? panel.setupState.ac.path : "Chưa có đường dẫn AC."
        color: panel.theme.muted
        wrapMode: Text.WrapAnywhere
    }
    RowLayout {
        Layout.fillWidth: true
        Action { objectName: "chooseAcFolderButton"; text: "Chọn thư mục AC"; onClicked: panel.setup.chooseAcFolder() }
        Action { objectName: "installAcCompanionButton"; text: "Cài / cập nhật companion"; onClicked: panel.setup.installAc() }
    }
    Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: panel.theme.divider }
    Label { text: "Assetto Corsa Competizione · UDP"; font.weight: panel.theme.sectionWeight }
    Label { objectName: "accSetupStatus"; text: panel.setupState.acc ? panel.setupState.acc.status : "Chưa kiểm tra ACC." }
    Label { text: panel.setupState.acc ? panel.setupState.acc.path : ""; color: panel.theme.muted; wrapMode: Text.WrapAnywhere }
    Action { objectName: "configureAccUdpButton"; text: "Thiết lập ACC UDP"; enabled: panel.canConfigure && !!panel.setupState.acc && panel.setupState.acc.detected; onClicked: panel.setup.configureAcc() }
    Label { visible: panel.live && !!panel.setupState.running; text: "Thoát AC / ACC để cài hoặc sửa cấu hình game."; color: panel.theme.muted }
    Label { visible: !!panel.setup && !panel.setupState.processCheckAvailable; text: "Chưa kiểm tra được tiến trình game; thao tác sửa cấu hình đang khóa."; color: panel.theme.muted }
    Label { text: panel.setup ? panel.setup.message : ""; visible: text.length > 0 }
    Label { visible: !!panel.setup && panel.setup.busy; text: "Đang cài companion…" }
    Action { objectName: "refreshSimulatorSetupButton"; text: "Kiểm tra lại"; enabled: panel.live && !!panel.setup && !panel.setup.busy; onClicked: panel.setup.refresh() }
    Label { text: "Cấu hình sẵn sàng chưa xác nhận game đang gửi dữ liệu. Khởi động lại game sau khi thiết lập, vào track rồi kiểm tra kết nối trên Cockpit."; color: panel.theme.muted }
}
